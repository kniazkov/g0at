/** @file cleanup_service_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Direct service fixtures; no native consumer policy or module loader dependency.
 */
#include "lib/memory/allocate.h"
#include "lib/process/cleanup_service_internal.h"
#include "lib/process/process_channel.h"
#include "lib/strings/string_ext.h"
#include "lib/threads/os_thread.h"

#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include "lib/platform/windows_target.h"

#    include <windows.h>
#else
#    include <unistd.h>
#endif

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            FPRINTF(stderr, "check failed: %s (%d)\n", #test, __LINE__);                           \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static unsigned long current_pid(void) {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return (unsigned long)getpid();
#endif
}

static void marker(const char *path, unsigned long value) {
    FILE *file = FOPEN(path, "wb");
    if (!file)
        exit(90);
    FPRINTF(file, "%lu\n", value);
    if (FCLOSE(file))
        exit(91);
}

static void *ensure_thread(void *arg) {
    return (void *)(uintptr_t)(ensure_cleanup_service(arg) + 1);
}

typedef struct {
    uint64_t now, success;
    unsigned attempts, pauses;
    bool permanent;
} fake_t;

static uint64_t fake_now(void *context) {
    return ((fake_t *)context)->now;
}

static void fake_pause(void *context, unsigned milliseconds) {
    fake_t *fake = context;
    fake->now += milliseconds;
    fake->pauses++;
}

static bool fake_remove(void *context, const char *path, fs_error_t *error) {
    (void)path;
    fake_t *fake = context;
    fake->attempts++;
    if (!fake->permanent && fake->now >= fake->success)
        return true;
    error->status = FS_ACCESS;
    error->native_code = 17;
    return false;
}

typedef struct {
    uintptr_t channel;
    const unsigned char *data;
    size_t size;
    channel_status_t status;
} writer_t;

static void *channel_writer(void *context) {
    writer_t *writer = context;
    writer->status = write_process_channel(writer->channel,
                                           writer->data,
                                           writer->size,
                                           child_deadline_after(3000));
    return NULL;
}

static int channel_tests(void) {
    uintptr_t pair[2];
    CHECK(create_process_channel(pair));
    CHECK(wait_process_channel(pair[0], child_deadline_after(5)) == CHANNEL_TIMEOUT);
    size_t size = 1024 * 1024;
    unsigned char *data = ALLOC(size), *copy = ALLOC(size);
    for (size_t i = 0; i < size; i++)
        data[i] = (unsigned char)i;
    writer_t writer = {pair[1], data, size, CHANNEL_ERROR};
    os_thread_t *thread;
    CHECK(create_os_thread(&thread, channel_writer, &writer, 0) == OS_THREAD_OK);
    CHECK(read_process_channel(pair[0], copy, size, child_deadline_after(3000)) == CHANNEL_OK);
    CHECK(join_os_thread(thread, NULL) == OS_THREAD_OK);
    CHECK(destroy_os_thread(thread) == OS_THREAD_OK);
    CHECK(writer.status == CHANNEL_OK && !memcmp(data, copy, size));
    /* A peer which stops reading must not make a command send unbounded. */
    CHECK(write_process_channel(pair[1], data, size, child_deadline_after(20)) == CHANNEL_TIMEOUT);
    close_process_channel(pair[0]);
    CHECK(write_process_channel(pair[1], data, 1, child_deadline_after(100)) != CHANNEL_OK);
    close_process_channel(pair[1]);
    FREE(data);
    FREE(copy);
    return 0;
}

static int retry_tests(void) {
    fake_t fake = {.now = 100, .success = 100};
    cleanup_retry_ops_t ops = {fake_now, fake_pause, fake_remove, &fake};
    CHECK(cleanup_retry_directory("unused", &ops, fake.now).status == CLEANUP_OK);
    CHECK(fake.attempts == 1 && fake.pauses == 0);
    fake = (fake_t){.now = 100, .success = 250};
    cleanup_result_t recovered = cleanup_retry_directory("unused", &ops, fake.now);
    CHECK(recovered.status == CLEANUP_OK && recovered.file_status == FS_OK
          && !recovered.native_code);
    CHECK(fake.now == 250 && fake.attempts == 4);
    /* Repeated failures/partial progress and client exit never create a new budget. */
    fake = (fake_t){.now = 100, .permanent = true};
    cleanup_result_t result = cleanup_retry_directory("unused", &ops, fake.now);
    CHECK(result.status == CLEANUP_DELETE_FAILED && result.file_status == FS_ACCESS);
    CHECK(result.native_code == 17 && fake.now == 30100 && fake.pauses == 600);
    fake = (fake_t){.now = 25100, .permanent = true};
    CHECK(cleanup_retry_directory("unused", &ops, 100).status == CLEANUP_DELETE_FAILED);
    CHECK(fake.now == 30100 && fake.pauses == 100);
    fake = (fake_t){.now = UINT64_MAX - 75, .permanent = true};
    CHECK(cleanup_retry_directory("unused", &ops, fake.now).status == CLEANUP_DELETE_FAILED);
    CHECK(fake.now == UINT64_MAX && fake.pauses == 2);
    return 0;
}

static int owner(const char *mode, const char *executable) {
    size_t baseline = get_allocated_memory_size();
    cleanup_service_t *service =
        create_cleanup_service(!strcmp(mode, "missing") ? "/missing/goat-cleaner" : executable);
    CHECK(service && cleanup_service_directory(service) == NULL);
    if (!strcmp(mode, "lazy")) {
        CHECK(finish_cleanup_service(service).status == CLEANUP_OK);
        destroy_cleanup_service(service);
        CHECK(get_allocated_memory_size() == baseline);
        puts("lazy");
        return 0;
    }
    os_thread_t *threads[8];
    for (unsigned i = 0; i < 8; i++)
        CHECK(create_os_thread(&threads[i], ensure_thread, service, 0) == OS_THREAD_OK);
    cleanup_status_t status = CLEANUP_INVALID;
    for (unsigned i = 0; i < 8; i++) {
        void *value;
        CHECK(join_os_thread(threads[i], &value) == OS_THREAD_OK);
        CHECK(destroy_os_thread(threads[i]) == OS_THREAD_OK);
        cleanup_status_t current = (cleanup_status_t)((uintptr_t)value - 1);
        if (!i)
            status = current;
        CHECK(current == status);
    }
    if (!strcmp(mode, "missing") || !strcmp(mode, "failure")) {
        CHECK(status == CLEANUP_START_FAILED || status == CLEANUP_TIMEOUT);
        CHECK(ensure_cleanup_service(service) == status);
        printf("failed %d\n", status);
        FFLUSH(stdout);
        /* A late response must not change the cached failed attempt. */
        (void)getchar();
        CHECK(ensure_cleanup_service(service) == status);
        destroy_cleanup_service(service);
        CHECK(get_allocated_memory_size() == baseline);
        return 0;
    }
    CHECK(status == CLEANUP_OK);
    const char *directory = cleanup_service_directory(service);
    CHECK(directory && *directory);
    char *file = join_fs_path(directory, "данные 😀.tmp", NULL);
    CHECK(file);
    marker(file, 42);
    printf("ready %s\n", directory);
    FFLUSH(stdout);
    char command[32];
    while (FGETS(command, sizeof(command), stdin)) {
        if (!strcmp(command, "finish\n")) {
            cleanup_result_t result = finish_cleanup_service(service);
            CHECK(finish_cleanup_service(service).status == result.status);
            printf("finished %d\n", result.status);
            FFLUSH(stdout);
        } else if (!strcmp(command, "detach\n")) {
            destroy_cleanup_service(service);
            service = NULL;
            puts("detached");
            FFLUSH(stdout);
            CHECK(FGETS(command, sizeof(command), stdin));
            marker(file, 43);
            puts("still-owner");
            FFLUSH(stdout);
            CHECK(FGETS(command, sizeof(command), stdin));
            break;
        } else if (!strcmp(command, "check\n")) {
            CHECK(ensure_cleanup_service(service) == CLEANUP_OK);
            puts("alive");
            FFLUSH(stdout);
        } else if (!strcmp(command, "exit\n")) {
            FREE(file);
            destroy_cleanup_service(service);
            return 23;
        } else if (!strcmp(command, "quit\n"))
            break;
        else if (!strcmp(command, "short\n") || !strcmp(command, "run\n")
                 || !strcmp(command, "timeout\n")) {
            bool short_run = !strcmp(command, "short\n");
            const char *args[] = {"fixture", short_run ? "answer" : "compiler", directory, NULL};
            child_options_t options = {
                .executable = executable,
                .working_directory = directory,
                .arguments = args,
                .deadline = !strcmp(command, "timeout\n") ? child_deadline_after(200) : 0};
            child_result_t *result = ALLOC(sizeof(*result));
            status = run_cleanup_process(service, &options, result);
            if (status == CLEANUP_OK) {
                if (result->cleanup_error)
                    FPRINTF(stderr,
                            "tree error %lu status %d\n",
                            result->cleanup_error,
                            result->status);
                CHECK(!result->cleanup_error);
                if (short_run)
                    CHECK(result->status == CHILD_EXITED && result->exit_code == 7
                          && result->out.size == 6 && !memcmp(result->out.data, "answer", 6));
            }
            printf("ran %d %d\n", status, status == CLEANUP_OK ? (int)result->status : -1);
            FFLUSH(stdout);
            FREE(result);
        }
    }
    FREE(file);
    destroy_cleanup_service(service);
    CHECK(get_allocated_memory_size() == baseline);
    return 0;
}

static int fixture_main(int argc, char **argv) {
    if (argc == 5 && !strcmp(argv[1], CLEANUP_SERVICE_ARGUMENT)) {
        char *pid_path = fs_environment("GOAT_TEST_CLEANER_PID", NULL);
        if (pid_path)
            marker(pid_path, current_pid());
        FREE(pid_path);
        const char *fault = getenv("GOAT_TEST_CLEANER_FAULT");
        if (fault && !strcmp(fault, "exit"))
            return 3;
        if (fault) {
            uintptr_t watch = (uintptr_t)strtoull(argv[2], NULL, 10);
            uintptr_t channel = (uintptr_t)strtoull(argv[3], NULL, 10);
            if (!strcmp(fault, "late")) {
                uint64_t deadline = strtoull(argv[4], NULL, 10);
                while (child_monotonic_ms() <= deadline)
                    process_pause(1);
                uint32_t frame[] = {0x47544331u, 1, 6};
                (void)
                    write_process_channel(channel, frame, sizeof(frame), child_deadline_after(100));
                (void)write_process_channel(channel, "/late", 6, child_deadline_after(100));
            }
            child_wait_parent(watch, 0);
            return 0;
        }
        int code;
        CHECK(cleanup_service_main(argc, argv, &code));
        char *done = fs_environment("GOAT_TEST_CLEANER_DONE", NULL);
        if (done)
            marker(done, (unsigned long)code);
        FREE(done);
        return code;
    }
    if (argc == 2 && !strcmp(argv[1], "channel"))
        return channel_tests();
    if (argc == 2 && !strcmp(argv[1], "retry"))
        return retry_tests();
    if (argc >= 2 && !strcmp(argv[1], "answer")) {
        marker("relative-output", 42);
        FWRITE("answer", 1, 6, stdout);
        return 7;
    }
    if (argc == 3 && (!strcmp(argv[1], "compiler") || !strcmp(argv[1], "worker"))) {
        char *path =
            join_fs_path(argv[2], !strcmp(argv[1], "compiler") ? "compiler" : "worker", NULL);
        marker(path, current_pid());
        FREE(path);
        if (!strcmp(argv[1], "compiler")) {
            const char *args[] = {"fixture", "worker", argv[2], NULL};
            child_options_t options = {.arguments = args};
            child_process_t *child = start_child_process(&options);
            CHECK(child);
            wait_child_process(child);
            destroy_child_process(child);
        } else
            for (;;)
                process_pause(100);
        return 0;
    }
    if (argc == 2)
        return owner(argv[1], NULL);
    if (argc == 3)
        return owner(argv[1], argv[2]);
    return 2;
}

int main(int argc, char **argv) {
    fs_error_t error = {0};
    if (!fs_arguments(&argc, &argv, &error))
        return 2;
    int code = fixture_main(argc, argv);
    free_fs_arguments(argc, argv);
    clear_fs_error(&error);
    return code;
}
