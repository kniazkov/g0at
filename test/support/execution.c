/** @file execution.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Test execution policy, independent of production interpreter code.
 */
#include "test/support/execution.h"

#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "lib/process/process_channel.h"

#include <inttypes.h>
#include <setjmp.h>
#include <string.h>

typedef struct cleanup_t {
    bool (*function)(void *);
    void *resource;
    struct cleanup_t *next;
} cleanup_t;

struct scenario_run_t {
    scenario_context_t *context;
    child_deadline_t deadline;
    jmp_buf abort_body;
    cleanup_t *cleanup;
    FILE *log, *out, *err;
    child_process_t *child;
    unsigned command, comparison;
    bool failed;
    const char *barrier;
    size_t barrier_offset;
    bool barrier_released;
    const unsigned char *barrier_input;
    size_t barrier_input_size, barrier_input_offset;
    unsigned char release_byte;
};

static void fail(scenario_run_t *run, const char *message) {
    run->failed = true;
    if (run->log) {
        fprintf(run->log, "FAIL: %s\n", message ? message : "required check failed");
        fflush(run->log);
    }
    longjmp(run->abort_body, 1);
}

static FILE *artifact(scenario_run_t *run, const char *name) {
    fs_error_t error = {0};
    char *path = join_fs_path(scenario_work_directory(run->context), name, &error);
    FILE *file = path ? FOPEN(path, "wb") : NULL;
    FREE(path);
    clear_fs_error(&error);
    return file;
}

static bool close_file(FILE **file) {
    if (!*file)
        return true;
    bool ok = !ferror(*file);
    int status = fclose(*file);
    *file = NULL;
    return status == 0 && ok;
}

scenario_context_t *scenario_run_context(scenario_run_t *run) {
    return run->context;
}

child_deadline_t scenario_run_deadline(const scenario_run_t *run) {
    return run->deadline;
}

void scenario_checkpoint(scenario_run_t *run) {
    if (child_monotonic_ms() >= run->deadline)
        fail(run, "scenario deadline exceeded");
}

void scenario_require(scenario_run_t *run, bool condition, const char *message) {
    scenario_checkpoint(run);
    if (!condition)
        fail(run, message);
}

void scenario_with_timeout(scenario_run_t *run,
                           uint64_t timeout_ms,
                           scenario_body_t body,
                           void *user) {
    scenario_require(run, timeout_ms != 0 && body != NULL, "invalid substep timeout/body");
    child_deadline_t outer = run->deadline, inner = child_deadline_after(timeout_ms);
    if (inner < outer)
        run->deadline = inner;
    scenario_checkpoint(run);
    body(run, user);
    scenario_checkpoint(run);
    run->deadline = outer;
}

void scenario_defer(scenario_run_t *run, bool (*cleanup)(void *), void *resource) {
    if (!cleanup)
        fail(run, "missing resource cleanup callback");
    cleanup_t *entry = ALLOC(sizeof(*entry));
    if (!entry) {
        cleanup(resource);
        fail(run, "cannot register resource cleanup");
    }
    entry->function = cleanup;
    entry->resource = resource;
    entry->next = run->cleanup;
    run->cleanup = entry;
    scenario_checkpoint(run);
}

static bool save_bytes(scenario_run_t *run, const char *name, const void *bytes, size_t size) {
    FILE *file = artifact(run, name);
    if (!file)
        return false;
    bool ok = !size || fwrite(bytes, 1, size, file) == size;
    return fclose(file) == 0 && ok;
}

static unsigned next_byte(const unsigned char *bytes, size_t size, size_t *offset, bool text) {
    unsigned value = bytes[(*offset)++];
    if (text && value == '\r' && *offset < size && bytes[*offset] == '\n') {
        ++*offset;
        value = '\n';
    }
    return value;
}

void scenario_require_bytes(scenario_run_t *run,
                            const char *label,
                            const void *expected,
                            size_t expected_size,
                            const void *actual,
                            size_t actual_size,
                            bool normalize_crlf) {
    scenario_checkpoint(run);
    if ((expected_size && !expected) || (actual_size && !actual))
        fail(run, "invalid byte span");
    unsigned index = ++run->comparison;
    char name[80];
    snprintf(name, sizeof(name), "comparison-%u.expected", index);
    if (!save_bytes(run, name, expected, expected_size))
        fail(run, "cannot save expected bytes");
    snprintf(name, sizeof(name), "comparison-%u.actual", index);
    if (!save_bytes(run, name, actual, actual_size))
        fail(run, "cannot save actual bytes");
    fprintf(run->log,
            "comparison %u: %s; expected=%zu actual=%zu CRLF=%d\n",
            index,
            label ? label : "bytes",
            expected_size,
            actual_size,
            normalize_crlf);
    size_t left = 0, right = 0;
    while (left < expected_size && right < actual_size) {
        if (next_byte(expected, expected_size, &left, normalize_crlf)
            != next_byte(actual, actual_size, &right, normalize_crlf))
            fail(run, "byte mismatch (see comparison artifacts)");
    }
    if (left != expected_size || right != actual_size)
        fail(run, "byte length mismatch (see comparison artifacts)");
    if (ferror(run->log) || fflush(run->log))
        fail(run, "cannot persist comparison diagnostics");
    scenario_checkpoint(run);
}

static bool output_sink(void *opaque, const unsigned char *bytes, size_t size) {
    scenario_run_t *run = opaque;
    if (fwrite(bytes, 1, size, run->out) != size)
        return false;
    for (size_t i = 0; run->barrier && run->barrier[run->barrier_offset] && i < size; i++) {
        if (bytes[i] != (unsigned char)run->barrier[run->barrier_offset])
            return false;
        run->barrier_offset++;
    }
    return true;
}

static child_input_status_t
barrier_input(void *opaque, unsigned char *data, size_t capacity, size_t *size) {
    scenario_run_t *run = opaque;
    *size = 0;
    if (run->barrier_input_offset < run->barrier_input_size) {
        size_t remaining = run->barrier_input_size - run->barrier_input_offset;
        *size = remaining < capacity ? remaining : capacity;
        if (!*size)
            return CHILD_INPUT_ERROR;
        memcpy(data, run->barrier_input + run->barrier_input_offset, *size);
        run->barrier_input_offset += *size;
        return CHILD_INPUT_DATA;
    }
    if (!run->barrier_released)
        return CHILD_INPUT_WAIT;
    if (run->barrier_released && run->barrier) {
        if (!capacity)
            return CHILD_INPUT_ERROR;
        data[0] = run->release_byte;
        *size = 1;
        run->barrier = NULL;
        return CHILD_INPUT_DATA;
    }
    return CHILD_INPUT_EOF;
}

static bool error_sink(void *opaque, const unsigned char *bytes, size_t size) {
    scenario_run_t *run = opaque;
    return fwrite(bytes, 1, size, run->err) == size;
}

static void log_string(FILE *file, const char *label, const char *value) {
    size_t size = value ? strlen(value) : 0;
    fprintf(file, "%s[%zu]=", label, size);
    if (size)
        fwrite(value, 1, size, file);
    fputc('\n', file);
}

static bool free_capture(void *data) {
    FREE(data);
    return true;
}

static scenario_command_result_t command(scenario_run_t *run,
                                         const child_options_t *options,
                                         bool check_exit,
                                         int64_t expected_exit,
                                         const char *barrier,
                                         scenario_body_t action,
                                         void *user,
                                         bool handshake,
                                         unsigned char release_byte,
                                         uint64_t hold_ms) {
    scenario_checkpoint(run);
    if (run->child)
        fail(run, "nested scenario commands are not supported");
    if (!options || !options->arguments || !options->arguments[0] || options->survive_parent
        || options->service_channel || options->result_channel || options->parent_watch
        || options->stdin_callback || options->stdout_callback || options->stderr_callback)
        fail(run, "unsupported or invalid scenario command options");
    child_options_t child = *options;
    child.deadline = run->deadline;
    child.capture_output = false;
    child.stdout_callback = output_sink;
    child.stderr_callback = error_sink;
    child.callback_context = run;
    if (!child.working_directory)
        child.working_directory = scenario_work_directory(run->context);
    if (!child.stdin_data)
        child.empty_stdin = true;
    run->barrier = barrier;
    run->barrier_offset = 0;
    run->barrier_released = false;
    run->barrier_input = NULL;
    run->barrier_input_size = run->barrier_input_offset = 0;
    run->release_byte = release_byte;
    if (barrier) {
        if (!*barrier || (!handshake && (!action || child.stdin_data))
            || (child.stdin_size && !child.stdin_data))
            fail(run, "invalid barrier command");
        run->barrier_input = child.stdin_data;
        run->barrier_input_size = child.stdin_size;
        child.stdin_data = NULL;
        child.stdin_size = 0;
        child.stdin_callback = barrier_input;
    }
    unsigned index = ++run->command;
    if (check_exit)
        fprintf(run->log, "command %u expected_exit=%" PRId64 "\n", index, expected_exit);
    else
        fprintf(run->log, "command %u expected_exit=any-normal\n", index);
    log_string(run->log, "executable", child.executable);
    log_string(run->log, "cwd", child.working_directory);
    for (size_t i = 0; child.arguments[i]; ++i)
        log_string(run->log, "argv", child.arguments[i]);
    if (ferror(run->log) || fflush(run->log))
        fail(run, "cannot persist command diagnostics");
    char name[80];
    snprintf(name, sizeof(name), "command-%u.stdout", index);
    run->out = artifact(run, name);
    snprintf(name, sizeof(name), "command-%u.stderr", index);
    run->err = artifact(run, name);
    if (!run->out || !run->err)
        fail(run, "cannot open command output artifacts");
    run->child = start_child_process(&child);
    if (!run->child)
        fail(run, "cannot start scenario command");
    if (barrier) {
        while (run->barrier[run->barrier_offset]) {
            const child_result_t *progress = poll_child_process(run->child);
            if (!progress || progress->status != CHILD_RUNNING)
                fail(run, "child ended before synchronization barrier");
            scenario_checkpoint(run);
        }
        /* The mutation is outside the child's callbacks; fail-fast unwinding is safe. */
        if (action)
            action(run, user);
        if (handshake) {
            uint64_t started = child_monotonic_ms();
            do {
                scenario_checkpoint(run);
                const child_result_t *progress = poll_child_process(run->child);
                if (!progress || progress->status != CHILD_RUNNING)
                    fail(run, "child ended before handshake release");
                if (child_monotonic_ms() - started >= hold_ms)
                    break;
                process_pause(1);
            } while (true);
            fprintf(run->log, "handshake hold_ms=%" PRIu64 "\n", child_monotonic_ms() - started);
        }
        scenario_checkpoint(run);
        run->barrier_released = true;
    }
    const child_result_t *result = wait_child_process(run->child);
    bool ok = result && result->status == CHILD_EXITED
              && (!check_exit || result->exit_code == expected_exit) && !result->cleanup_error;
    if (result)
        fprintf(run->log,
                "result status=%d exit=%" PRId64 " signal=%d system=%lu cleanup=%lu\n",
                result->status,
                result->exit_code,
                result->signal_number,
                result->system_error,
                result->cleanup_error);
    int64_t exit_code = result ? result->exit_code : 0;
    destroy_child_process(run->child);
    run->child = NULL;
    bool out_ok = close_file(&run->out), err_ok = close_file(&run->err);
    if (!out_ok || !err_ok || ferror(run->log) || fflush(run->log))
        fail(run, "cannot persist command output or result");
    if (!ok)
        fail(run, "command failed (see status and output artifacts)");
    scenario_checkpoint(run);
    scenario_command_result_t captured = {exit_code, NULL, NULL, 0, 0};
    if (!check_exit) {
        fs_error_t error = {0};
        void *data = NULL;
        size_t size = 0;
        snprintf(name, sizeof(name), "command-%u.stdout", index);
        char *path = join_fs_path(scenario_work_directory(run->context), name, &error);
        bool read_ok = path && read_fs_regular_file(path, SIZE_MAX - 1, &data, &size, &error);
        FREE(path);
        clear_fs_error(&error);
        if (!read_ok)
            fail(run, "cannot read complete stdout artifact");
        scenario_defer(run, free_capture, data);
        captured.out = data;
        captured.out_size = size;
        snprintf(name, sizeof(name), "command-%u.stderr", index);
        path = join_fs_path(scenario_work_directory(run->context), name, &error);
        read_ok = path && read_fs_regular_file(path, SIZE_MAX - 1, &data, &size, &error);
        FREE(path);
        clear_fs_error(&error);
        if (!read_ok)
            fail(run, "cannot read complete stderr artifact");
        scenario_defer(run, free_capture, data);
        captured.err = data;
        captured.err_size = size;
    }
    return captured;
}

void scenario_command(scenario_run_t *run, const child_options_t *options, int64_t expected_exit) {
    (void)command(run, options, true, expected_exit, NULL, NULL, NULL, false, 'G', 0);
}

scenario_command_result_t scenario_command_capture(scenario_run_t *run,
                                                   const child_options_t *options) {
    return command(run, options, false, 0, NULL, NULL, NULL, false, 'G', 0);
}

scenario_command_result_t scenario_command_barrier(scenario_run_t *run,
                                                   const child_options_t *options,
                                                   const char *prefix,
                                                   scenario_body_t action,
                                                   void *user) {
    if (!prefix || !*prefix || !action)
        fail(run, "invalid barrier command");
    return command(run, options, false, 0, prefix, action, user, false, 'G', 0);
}

scenario_command_result_t scenario_command_handshake(scenario_run_t *run,
                                                     const child_options_t *options,
                                                     const char *prefix,
                                                     unsigned char release_byte,
                                                     uint64_t hold_ms) {
    if (!prefix || !*prefix)
        fail(run, "invalid handshake command");
    return command(run, options, false, 0, prefix, NULL, NULL, true, release_byte, hold_ms);
}

bool scenario_execute(const scenario_options_t *options,
                      uint64_t timeout_ms,
                      scenario_body_t body,
                      void *user) {
    if (!timeout_ms || !body)
        return false;
    scenario_run_t *run = CALLOC(sizeof(*run));
    if (!run)
        return false;
    run->deadline = child_deadline_after(timeout_ms);
    run->context = scenario_create(options);
    if (!run->context) {
        FREE(run);
        return false;
    }
    run->log = artifact(run, "execution.log");
    if (setjmp(run->abort_body) == 0) {
        if (!run->log)
            fail(run, "cannot create execution log");
        scenario_checkpoint(run);
        body(run, user);
        scenario_checkpoint(run);
    }
    if (run->child) {
        const child_result_t *result = stop_child_process(run->child);
        if (!result || result->cleanup_error)
            run->failed = true;
        destroy_child_process(run->child);
    }
    if (!close_file(&run->out))
        run->failed = true;
    if (!close_file(&run->err))
        run->failed = true;
    while (run->cleanup) {
        cleanup_t *entry = run->cleanup;
        run->cleanup = entry->next;
        if (!entry->function(entry->resource)) {
            run->failed = true;
            if (run->log)
                fprintf(run->log, "FAIL: resource cleanup failed\n");
        }
        FREE(entry);
    }
    if (run->log)
        fprintf(run->log, "%s\n", run->failed ? "FAIL" : "PASS");
    if (!close_file(&run->log))
        run->failed = true;
    bool success = scenario_finish(run->context, !run->failed);
    FREE(run);
    return success;
}

static void supervise_body(scenario_run_t *run, void *user) {
    scenario_command(run, user, 0);
}

bool scenario_supervise(const scenario_options_t *options,
                        uint64_t timeout_ms,
                        const child_options_t *host) {
    return scenario_execute(options, timeout_ms, supervise_body, (void *)host);
}
