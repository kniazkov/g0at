/** @file cleanup_service.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent owner watcher and sole process/directory cleanup broker.
 */
#include "allocate.h"
#include "cleanup_service_internal.h"
#include "os_thread.h"
#include "process_channel.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define CLEANUP_FRAME_LIMIT (1024 * 1024)
#define CLEANUP_PROTOCOL 0x47544331u

enum { MESSAGE_READY = 1, MESSAGE_ACK, MESSAGE_RUN, MESSAGE_RESULT, MESSAGE_CLEAN, MESSAGE_DONE };

typedef struct {
    uint32_t magic, type, size;
} frame_t;

struct cleanup_service_t {
    os_mutex_t *mutex;
    char *executable, *directory;
    child_process_t *process;
    uintptr_t channel;
    bool attempted, ready, broken, finishing;
    cleanup_status_t startup;
    cleanup_result_t final;
    atomic_uint_fast64_t finish_deadline;
    atomic_bool finish_timeout;
};

static char *copy_string(const char *text) {
    if (!text)
        return NULL;
    size_t size = strlen(text) + 1;
    char *copy = ALLOC(size);
    memcpy(copy, text, size);
    return copy;
}

static channel_status_t send_frame(uintptr_t channel,
                                   unsigned type,
                                   const void *data,
                                   size_t size,
                                   child_deadline_t deadline) {
    if (size > CLEANUP_FRAME_LIMIT)
        return CHANNEL_ERROR;
    frame_t frame = {CLEANUP_PROTOCOL, type, (uint32_t)size};
    channel_status_t status = write_process_channel(channel, &frame, sizeof(frame), deadline);
    if (status == CHANNEL_OK)
        status = write_process_channel(channel, data, size, deadline);
    return status;
}

static channel_status_t receive_frame(uintptr_t channel,
                                      unsigned *type,
                                      void **data,
                                      size_t *size,
                                      child_deadline_t deadline) {
    *data = NULL;
    frame_t frame;
    channel_status_t status = read_process_channel(channel, &frame, sizeof(frame), deadline);
    if (status != CHANNEL_OK)
        return status;
    if (frame.magic != CLEANUP_PROTOCOL || frame.size > CLEANUP_FRAME_LIMIT)
        return CHANNEL_ERROR;
    *size = frame.size;
    *type = frame.type;
    *data = ALLOC(frame.size + 1);
    status = read_process_channel(channel, *data, frame.size, deadline);
    if (status != CHANNEL_OK) {
        FREE(*data);
        *data = NULL;
    }
    return status;
}

static cleanup_status_t transport_status(channel_status_t status) {
    return status == CHANNEL_TIMEOUT ? CLEANUP_TIMEOUT : CLEANUP_CONNECTION_LOST;
}

cleanup_service_t *create_cleanup_service(const char *executable) {
    cleanup_service_t *s = CALLOC(sizeof(*s));
    if (create_os_mutex(&s->mutex) != OS_THREAD_OK) {
        FREE(s);
        return NULL;
    }
    atomic_init(&s->finish_deadline, 0);
    atomic_init(&s->finish_timeout, false);
    s->executable = copy_string(executable);
    return s;
}

static cleanup_status_t ensure_locked(cleanup_service_t *s) {
    if (s->attempted)
        return s->startup;
    s->attempted = true;
    s->startup = CLEANUP_START_FAILED;
    child_deadline_t deadline = child_deadline_after(CLEANUP_START_MS);
    uintptr_t pair[2];
    if (!create_process_channel(pair))
        return s->startup;
    s->channel = pair[0];
    char time[32];
    snprintf(time, sizeof(time), "%llu", (unsigned long long)deadline);
    const char *args[] = {"goat-cleanup",
                          CLEANUP_SERVICE_ARGUMENT,
                          CHILD_PARENT_ARGUMENT,
                          CHILD_SERVICE_ARGUMENT,
                          time,
                          NULL};
    child_options_t options = {.executable = s->executable,
                               .arguments = args,
                               .empty_stdin = true,
                               .capture_output = true,
                               .parent_watch = true,
                               .survive_parent = true,
                               .service_channel = pair[1]};
    s->process = start_child_process(&options);
    close_process_channel(pair[1]);
    unsigned type = 0;
    void *data = NULL;
    size_t size = 0;
    channel_status_t status = receive_frame(s->channel, &type, &data, &size, deadline);
    if (status == CHANNEL_OK && type == MESSAGE_READY && size > 1 && ((char *)data)[size - 1] == 0
        && !memchr(data, 0, size - 1) && validate_fs_path(data, size - 1, NULL)) {
        s->directory = data;
        data = NULL;
        status = send_frame(s->channel, MESSAGE_ACK, NULL, 0, deadline);
        if (status == CHANNEL_OK && child_monotonic_ms() < deadline) {
            s->ready = true;
            s->startup = CLEANUP_OK;
        }
    }
    FREE(data);
    if (!s->ready) {
        close_process_channel(s->channel);
        s->channel = 0;
        /* Late READY can never admit work; the service has the same deadline and exits. */
        s->startup = status == CHANNEL_TIMEOUT || child_monotonic_ms() >= deadline
                         ? CLEANUP_TIMEOUT
                         : CLEANUP_START_FAILED;
    }
    return s->startup;
}

cleanup_status_t ensure_cleanup_service(cleanup_service_t *s) {
    if (!s || lock_os_mutex(s->mutex) != OS_THREAD_OK)
        return CLEANUP_INVALID;
    cleanup_status_t status =
        (s->finishing || atomic_load(&s->finish_deadline)) ? CLEANUP_CLOSED : ensure_locked(s);
    unlock_os_mutex(s->mutex);
    return status;
}

const char *cleanup_service_directory(const cleanup_service_t *s) {
    return s ? s->directory : NULL;
}

/* Bounded NUL-delimited argv follows an absolute monotonic deadline and argc. */
typedef struct {
    uint64_t deadline;
    uint32_t argc, executable_size, directory_size;
} run_header_t;

cleanup_status_t
run_cleanup_process(cleanup_service_t *s, const child_options_t *options, child_result_t *result) {
    if (!s || !options || !result || !options->arguments || !options->arguments[0]
        || options->capture_output || options->empty_stdin || options->result_channel
        || options->parent_watch || options->survive_parent || options->service_channel)
        return CLEANUP_INVALID;
    size_t count = 0, size = sizeof(run_header_t);
    size_t executable_size = options->executable ? strlen(options->executable) + 1 : 0;
    if (executable_size > CLEANUP_FRAME_LIMIT - size)
        return CLEANUP_INVALID;
    size += executable_size;
    size_t directory_size = options->working_directory ? strlen(options->working_directory) + 1 : 0;
    if (directory_size > CLEANUP_FRAME_LIMIT - size)
        return CLEANUP_INVALID;
    size += directory_size;
    while (options->arguments[count]) {
        size_t length = strlen(options->arguments[count]) + 1;
        if (length > CLEANUP_FRAME_LIMIT - size || count == 4096)
            return CLEANUP_INVALID;
        size += length;
        count++;
    }
    unsigned char *data = CALLOC(size);
    run_header_t header = {options->deadline,
                           (uint32_t)count,
                           (uint32_t)executable_size,
                           (uint32_t)directory_size};
    memcpy(data, &header, sizeof(header));
    size_t offset = sizeof(header);
    if (executable_size) {
        memcpy(data + offset, options->executable, executable_size);
        offset += executable_size;
    }
    if (directory_size) {
        memcpy(data + offset, options->working_directory, directory_size);
        offset += directory_size;
    }
    for (size_t i = 0; i < count; i++) {
        size_t length = strlen(options->arguments[i]) + 1;
        memcpy(data + offset, options->arguments[i], length);
        offset += length;
    }
    if (lock_os_mutex(s->mutex) != OS_THREAD_OK) {
        FREE(data);
        return CLEANUP_INVALID;
    }
    cleanup_status_t status =
        (s->finishing || atomic_load(&s->finish_deadline)) ? CLEANUP_CLOSED : ensure_locked(s);
    if (status == CLEANUP_OK && s->broken)
        status = CLEANUP_CONNECTION_LOST;
    if (status == CLEANUP_OK) {
        channel_status_t sent =
            send_frame(s->channel, MESSAGE_RUN, data, size, child_deadline_after(CLEANUP_START_MS));
        void *reply = NULL;
        unsigned type = 0;
        size_t bytes = 0;
        /* Child owns its deadline and reports timeout including completed tree shutdown. */
        channel_status_t received =
            sent == CHANNEL_OK ? receive_frame(s->channel, &type, &reply, &bytes, 0) : sent;
        if (received == CHANNEL_OK && type == MESSAGE_RESULT && bytes == sizeof(*result))
            memcpy(result, reply, bytes);
        else {
            s->broken = true;
            status = transport_status(received);
        }
        FREE(reply);
    }
    unlock_os_mutex(s->mutex);
    FREE(data);
    return status;
}

cleanup_result_t finish_cleanup_service(cleanup_service_t *s) {
    cleanup_result_t result = {.status = CLEANUP_INVALID};
    if (!s)
        return result;
    if (atomic_load(&s->finish_timeout)) {
        result.status = CLEANUP_TIMEOUT;
        return result;
    }
    uint_fast64_t expected = 0;
    child_deadline_t deadline = child_deadline_after(CLEANUP_WAIT_MS);
    if (!atomic_compare_exchange_strong(&s->finish_deadline, &expected, deadline))
        deadline = expected;
    os_thread_status_t locked;
    while ((locked = try_lock_os_mutex(s->mutex)) == OS_THREAD_BUSY) {
        if (child_monotonic_ms() >= deadline) {
            atomic_store(&s->finish_timeout, true);
            result.status = CLEANUP_TIMEOUT;
            return result;
        }
        process_pause(1);
    }
    if (locked != OS_THREAD_OK)
        return result;
    if (atomic_load(&s->finish_timeout)) {
        result.status = CLEANUP_TIMEOUT;
        goto done;
    }
    if (s->finishing) {
        result = s->final;
        goto done;
    }
    s->finishing = true;
    result.status = !s->attempted ? CLEANUP_OK
                    : !s->ready   ? s->startup
                    : s->broken   ? CLEANUP_CONNECTION_LOST
                                  : CLEANUP_OK;
    if (s->ready && !s->broken) {
        channel_status_t status = send_frame(s->channel, MESSAGE_CLEAN, NULL, 0, deadline);
        void *reply = NULL;
        unsigned type = 0;
        size_t size = 0;
        if (status == CHANNEL_OK)
            status = receive_frame(s->channel, &type, &reply, &size, deadline);
        if (status == CHANNEL_OK && type == MESSAGE_DONE && size == sizeof(result))
            memcpy(&result, reply, size);
        else
            result.status = transport_status(status);
        FREE(reply);
        s->broken = status != CHANNEL_OK;
    }
    s->final = result;
done:
    unlock_os_mutex(s->mutex);
    return result;
}

void destroy_cleanup_service(cleanup_service_t *s) {
    if (!s)
        return;
    close_process_channel(s->channel);
    release_child_service(s->process);
    destroy_os_mutex(s->mutex);
    FREE(s->directory);
    FREE(s->executable);
    FREE(s);
}

cleanup_result_t
cleanup_retry_directory(const char *path, const cleanup_retry_ops_t *ops, uint64_t start) {
    cleanup_result_t result = {.status = CLEANUP_DELETE_FAILED};
    uint64_t deadline =
        start > UINT64_MAX - CLEANUP_RETRY_MS ? UINT64_MAX : start + CLEANUP_RETRY_MS;
    fs_error_t error = {0};
    for (;;) {
        bool removed = ops->remove(ops->context, path, &error);
        if (removed || error.status == FS_NOT_FOUND) {
            result = (cleanup_result_t){.status = CLEANUP_OK};
            break;
        }
        result.file_status = error.status;
        result.native_code = error.native_code;
        /* Invalid paths/encoding and unsupported operations cannot become valid by waiting. */
        if (error.status == FS_INVALID || error.status == FS_UNSUPPORTED)
            break;
        uint64_t now = ops->now(ops->context);
        if (now >= deadline)
            break;
        unsigned wait = deadline - now > 50 ? 50 : (unsigned)(deadline - now);
        ops->pause(ops->context, wait);
        if (ops->now(ops->context) >= deadline)
            break;
        clear_fs_error(&error);
    }
    clear_fs_error(&error);
    return result;
}

static uint64_t real_now(void *unused) {
    (void)unused;
    return child_monotonic_ms();
}

static void real_pause(void *unused, unsigned milliseconds) {
    (void)unused;
    process_pause(milliseconds);
}

static bool real_remove(void *unused, const char *path, fs_error_t *error) {
    (void)unused;
    return remove_fs_tree(path, error);
}

static child_process_t *start_managed(void *data, size_t size) {
    if (size < sizeof(run_header_t))
        return NULL;
    run_header_t header;
    memcpy(&header, data, sizeof(header));
    if (!header.argc || header.argc > 4096 || header.executable_size > size - sizeof(header))
        return NULL;
    char *cursor = (char *)data + sizeof(header), *end = (char *)data + size;
    char *executable = NULL;
    if (header.executable_size) {
        if (!memchr(cursor, 0, header.executable_size)
            || strlen(cursor) + 1 != header.executable_size)
            return NULL;
        executable = cursor;
        cursor += header.executable_size;
    }
    char *directory = NULL;
    if (header.directory_size) {
        if (header.directory_size > (size_t)(end - cursor)
            || !memchr(cursor, 0, header.directory_size)
            || strlen(cursor) + 1 != header.directory_size)
            return NULL;
        directory = cursor;
        cursor += header.directory_size;
    }
    const char **args = CALLOC((header.argc + 1) * sizeof(*args));
    bool valid = true;
    for (unsigned i = 0; i < header.argc; i++) {
        char *zero = memchr(cursor, 0, (size_t)(end - cursor));
        if (!zero) {
            valid = false;
            break;
        }
        args[i] = cursor;
        cursor = zero + 1;
    }
    child_process_t *process = NULL;
    if (valid && cursor == end) {
        child_options_t options = {.executable = executable,
                                   .working_directory = directory,
                                   .arguments = args,
                                   .deadline = header.deadline,
                                   .empty_stdin = true,
                                   .capture_output = true};
        process = start_child_process(&options);
    }
    FREE(args);
    return process;
}

static int serve(uintptr_t watch, uintptr_t channel, child_deadline_t startup_deadline) {
    if (!watch || !channel || child_parent_state(watch) != 0 || !startup_deadline
        || child_monotonic_ms() >= startup_deadline) {
        close_process_channel(channel);
        return 1;
    }
    fs_error_t error = {0};
    char *base = temporary_fs_directory(&error);
    char *prefix = base ? join_fs_path(base, "goat-run", &error) : NULL;
    char *directory = prefix ? create_fs_temporary(prefix, true, NULL, &error) : NULL;
    FREE(base);
    FREE(prefix);
    clear_fs_error(&error);
    if (!directory) {
        close_process_channel(channel);
        return 1;
    }
    bool admitted = false, cleaning = false, connected = true, tree_failed = false;
    child_process_t *process = NULL;
    if (send_frame(channel, MESSAGE_READY, directory, strlen(directory) + 1, startup_deadline)
        == CHANNEL_OK) {
        unsigned type = 0;
        void *data = NULL;
        size_t size = 0;
        admitted = receive_frame(channel, &type, &data, &size, startup_deadline) == CHANNEL_OK
                   && type == MESSAGE_ACK && !size;
        FREE(data);
    }
    if (!admitted)
        cleaning = true;
    while (!cleaning) {
        int owner = child_parent_state(watch);
        if (owner != 0) {
            tree_failed |= owner < 0;
            cleaning = true;
            break;
        }
        if (process) {
            const child_result_t *result = poll_child_process(process);
            if (result->status != CHILD_RUNNING) {
                tree_failed |= result->cleanup_error != 0;
                if (connected
                    && send_frame(channel,
                                  MESSAGE_RESULT,
                                  result,
                                  sizeof(*result),
                                  child_deadline_after(1000))
                           != CHANNEL_OK)
                    connected = false;
                destroy_child_process(process);
                process = NULL;
            }
        }
        if (!connected) {
            close_process_channel(channel);
            channel = 0;
            process_pause(10);
            continue;
        }
        channel_status_t status = wait_process_channel(channel, child_deadline_after(10));
        if (status == CHANNEL_TIMEOUT)
            continue;
        if (status != CHANNEL_OK) {
            connected = false;
            continue;
        }
        unsigned type = 0;
        void *data = NULL;
        size_t size = 0;
        status = receive_frame(channel, &type, &data, &size, child_deadline_after(1000));
        if (status != CHANNEL_OK)
            connected = false;
        else if (type == MESSAGE_CLEAN && !size)
            cleaning = true;
        else if (type == MESSAGE_RUN && !process && !tree_failed) {
            process = start_managed(data, size);
            if (!process)
                connected = false;
        } else
            connected = false;
        FREE(data);
    }
    uint64_t cleanup_started = child_monotonic_ms();
    if (process) {
        const child_result_t *result = stop_child_process(process);
        tree_failed |= result->cleanup_error != 0;
        destroy_child_process(process);
    }
    cleanup_retry_ops_t ops = {real_now, real_pause, real_remove, NULL};
    cleanup_result_t result = tree_failed
                                  ? (cleanup_result_t){.status = CLEANUP_TREE_FAILED}
                                  : cleanup_retry_directory(directory, &ops, cleanup_started);
    if (connected && admitted)
        (void)
            send_frame(channel, MESSAGE_DONE, &result, sizeof(result), child_deadline_after(1000));
    close_process_channel(channel);
    FREE(directory);
    return result.status == CLEANUP_OK ? 0 : 1;
}

static bool parse_number(const char *text, uint64_t *value) {
    if (!text || !*text || *text < '0' || *text > '9')
        return false;
    char *end;
    errno = 0;
    unsigned long long number = strtoull(text, &end, 10);
    if (errno || *end)
        return false;
    *value = (uint64_t)number;
    return true;
}

bool cleanup_service_main(int argc, char **argv, int *exit_code) {
    if (argc < 2 || strcmp(argv[1], CLEANUP_SERVICE_ARGUMENT))
        return false;
    *exit_code = 1;
    uint64_t watch, channel, deadline;
    if (argc != 5 || !parse_number(argv[2], &watch) || !parse_number(argv[3], &channel)
        || !parse_number(argv[4], &deadline) || watch > UINTPTR_MAX || channel > UINTPTR_MAX)
        return true;
    *exit_code = serve((uintptr_t)watch, (uintptr_t)channel, deadline);
    child_close_channel((uintptr_t)watch);
    return true;
}
