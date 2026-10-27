/** @file scenario_execution_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent fail-fast, binary diagnostics and deadline self-tests.
 */
#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "lib/platform/windows_target.h"
#include "test/support/execution.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#    include <windows.h>
#else
#    include <unistd.h>
#endif
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "Failed line %d: %s\n", __LINE__, #x);                                 \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
static const unsigned char binary[] = {' ', 0, 'x', '\n', '\n', ' ', '\r', '\n', 255};

static void pause_ms(unsigned ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

static char *copy_text(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p)
        memcpy(p, s, n);
    return p;
}

typedef struct {
    const char *mode;
    char *work;
    unsigned steps, cleaned;
} state_t;

static bool cleanup(void *user) {
    state_t *s = user;
    s->cleaned++;
    return strcmp(s->mode, "cleanup-failure") != 0;
}

static void body(scenario_run_t *run, void *user) {
    state_t *s = user;
    s->work = copy_text(scenario_work_directory(scenario_run_context(run)));
    scenario_require(run, s->work != NULL, "copy workspace");
    scenario_defer(run, cleanup, s);
    if (!strcmp(s->mode, "cleanup-failure"))
        scenario_defer(run, cleanup, s);
    s->steps++;
    if (!strcmp(s->mode, "mismatch"))
        scenario_require(run, false, "intentional mismatch");
    if (!strcmp(s->mode, "binary-mismatch")) {
        unsigned char actual[sizeof(binary)];
        memcpy(actual, binary, sizeof(binary));
        actual[2] = 'y';
        scenario_require_bytes(run,
                               "binary",
                               binary,
                               sizeof(binary),
                               actual,
                               sizeof(actual),
                               false);
    }
    if (!strcmp(s->mode, "crlf") || !strcmp(s->mode, "strict-crlf"))
        scenario_require_bytes(run, "lines", "a\r\n\r\n", 5, "a\n\n", 3, !strcmp(s->mode, "crlf"));
    if (!strcmp(s->mode, "success"))
        scenario_require_bytes(run,
                               "binary",
                               binary,
                               sizeof(binary),
                               binary,
                               sizeof(binary),
                               false);
    const char *worker_mode = NULL;
    if (!strcmp(s->mode, "start-error"))
        worker_mode = "binary";
    if (!strcmp(s->mode, "exit"))
        worker_mode = "exit";
    if (!strcmp(s->mode, "crash"))
        worker_mode = "crash";
    if (!strcmp(s->mode, "timeout"))
        worker_mode = "hang";
    if (!strcmp(s->mode, "command"))
        worker_mode = "binary";
    if (!strcmp(s->mode, "cumulative"))
        worker_mode = "delay";
    if (worker_mode) {
        const char *args[] =
            {"scenario_execution_testing", "--worker", worker_mode, "space argument", "", NULL};
        child_options_t command = {0};
        command.arguments = args;
        if (!strcmp(s->mode, "start-error"))
            command.executable = "./nonexistent-scenario-executable";
        command.empty_stdin = true;
        scenario_command(run, &command, 0);
        if (!strcmp(s->mode, "cumulative")) {
            s->steps++;
            scenario_command(run, &command, 0);
        }
    }
    s->steps++;
}

static bool exists(const char *path) {
    fs_info_t info;
    fs_error_t error = {0};
    bool ok = inspect_fs_path(path, false, &info, &error);
    clear_fs_error(&error);
    return ok;
}

static int worker(const char *mode) {
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    if (!strcmp(mode, "exit"))
        return 23;
    if (!strcmp(mode, "crash")) {
#ifdef _WIN32
        TerminateProcess(GetCurrentProcess(), 0xc0000005);
#else
        raise(SIGKILL);
#endif
        return 99;
    }
    if (!strcmp(mode, "hang")) {
        pause_ms(10000);
        return 0;
    }
    if (!strcmp(mode, "delay")) {
        pause_ms(350);
        return 0;
    }
    CHECK(fwrite(binary, 1, sizeof(binary), stdout) == sizeof(binary));
    CHECK(fwrite(binary, 1, sizeof(binary), stderr) == sizeof(binary));
    return 0;
}

static bool artifact(const char *work, const char *name, const void *expected, size_t size) {
    fs_error_t error = {0};
    char *path = join_fs_path(work, name, &error);
    FILE *file = path ? FOPEN(path, "rb") : NULL;
    unsigned char buffer[256];
    size_t count = file ? fread(buffer, 1, sizeof(buffer), file) : 0;
    bool ok = file && !ferror(file) && count == size && !memcmp(buffer, expected, size);
    if (file && fclose(file))
        ok = false;
    FREE(path);
    clear_fs_error(&error);
    return ok;
}

static bool log_contains(const char *work, const char *needle) {
    fs_error_t error = {0};
    char *path = join_fs_path(work, "execution.log", &error);
    FILE *file = path ? FOPEN(path, "rb") : NULL;
    char buffer[4096];
    size_t count = file ? fread(buffer, 1, sizeof(buffer) - 1, file) : 0;
    buffer[count] = 0;
    bool ok = file && !ferror(file) && strstr(buffer, needle);
    if (file && fclose(file))
        ok = false;
    FREE(path);
    clear_fs_error(&error);
    return ok;
}

/* Removal is intentionally immediate and single-attempt: a returned child result
 * must mean its processes no longer hold the workspace open. */
static bool remove_work(const char *work) {
    fs_error_t error = {0};
    bool ok = remove_fs_tree(work, &error);
    if (!ok)
        fprintf(stderr,
                "Workspace cleanup failed: operation=%s path=%s status=%d native=%lu\n",
                error.operation ? error.operation : "unknown",
                error.path ? error.path : work,
                error.status,
                error.native_code);
    clear_fs_error(&error);
    return ok;
}

static int supervised(scenario_options_t options, FILE *diagnostics, bool crash) {
    const char *args[] =
        {"scenario_execution_testing", "--worker", crash ? "crash" : "hang", "", "", NULL};
    child_options_t host = {0};
    host.arguments = args;
    uint64_t before = child_monotonic_ms();
    CHECK(!scenario_supervise(&options, 250, &host));
    CHECK(child_monotonic_ms() - before < 8000);
    CHECK(!fflush(diagnostics) && !fseek(diagnostics, 0, SEEK_SET));
    char report[16384];
    size_t count = fread(report, 1, sizeof(report) - 1, diagnostics);
    report[count] = 0;
    char *work = strstr(report, "retained work: ");
    CHECK(work);
    work += strlen("retained work: ");
    work[strcspn(work, "\r\n")] = 0;
    CHECK(exists(work));
    CHECK(artifact(work, "command-1.stdout", "", 0));
    if (!crash) {
        char status[64];
        snprintf(status, sizeof(status), "result status=%d", CHILD_TIMED_OUT);
        CHECK(log_contains(work, status));
    }
    CHECK(log_contains(work, "cleanup=0\n"));
    CHECK(remove_work(work));
    return 0;
}

static int run_test(int argc, char **argv) {
    if (argc == 5 && !strcmp(argv[1], "--worker"))
        return worker(argv[2]);
    if (argc != 4)
        return 2;
    const char *mode = argv[1];
    FILE *diagnostics = tmpfile();
    CHECK(diagnostics);
    scenario_options_t options = {mode, argv[2], argv[3], false, diagnostics};
    if (!strcmp(mode, "host-hang") || !strcmp(mode, "host-crash")) {
        int result = supervised(options, diagnostics, !strcmp(mode, "host-crash"));
        CHECK(!fclose(diagnostics));
        return result;
    }
    state_t state = {mode, NULL, 0, 0};
    bool expected = !strcmp(mode, "success") || !strcmp(mode, "crlf") || !strcmp(mode, "command");
    options.keep_work = expected;
    uint64_t timeout = !strcmp(mode, "cumulative") ? 600 : !strcmp(mode, "timeout") ? 250 : 5000;
    if (!strcmp(mode, "invalid-timeout"))
        timeout = 0;
    uint64_t started = child_monotonic_ms();
    bool actual = scenario_execute(&options, timeout, body, &state);
    CHECK(actual == expected);
    CHECK(child_monotonic_ms() - started < 8000);
    if (!timeout) {
        CHECK(state.steps == 0 && state.cleaned == 0);
    } else {
        CHECK(state.cleaned == (!strcmp(mode, "cleanup-failure") ? 2u : 1u));
        CHECK(state.steps
              == ((expected || !strcmp(mode, "cleanup-failure")) ? 2u
                  : !strcmp(mode, "cumulative")                  ? 2u
                                                                 : 1u));
        CHECK(state.work && exists(state.work));
        if (!strcmp(mode, "success") || !strcmp(mode, "binary-mismatch")) {
            CHECK(artifact(state.work, "comparison-1.expected", binary, sizeof(binary)));
            unsigned char actual_bytes[sizeof(binary)];
            memcpy(actual_bytes, binary, sizeof(binary));
            if (!strcmp(mode, "binary-mismatch"))
                actual_bytes[2] = 'y';
            CHECK(artifact(state.work, "comparison-1.actual", actual_bytes, sizeof(actual_bytes)));
        }
        if (!strcmp(mode, "command")) {
            CHECK(artifact(state.work, "command-1.stdout", binary, sizeof(binary)));
            CHECK(artifact(state.work, "command-1.stderr", binary, sizeof(binary)));
            CHECK(log_contains(state.work, "argv[14]=space argument\n"));
            CHECK(log_contains(state.work, "argv[0]=\n"));
        }
        if (!strcmp(mode, "exit"))
            CHECK(log_contains(state.work, "exit=23"));
        if (!strcmp(mode, "timeout") || !strcmp(mode, "cumulative")) {
            char status[64];
            snprintf(status, sizeof(status), "result status=%d", CHILD_TIMED_OUT);
            CHECK(log_contains(state.work, status));
            CHECK(log_contains(state.work, "cleanup=0\n"));
        }
        if (!strcmp(mode, "crash")) {
#ifdef _WIN32
            CHECK(log_contains(state.work, "3221225477"));
#else
            CHECK(log_contains(state.work, "signal=9"));
#endif
        }
        CHECK(remove_work(state.work));
    }
    free(state.work);
    CHECK(fclose(diagnostics) == 0);
    return 0;
}

int main(int argc, char **argv) {
    fs_error_t error = {0};
    if (!fs_arguments(&argc, &argv, &error)) {
        clear_fs_error(&error);
        return 2;
    }
    int result = run_test(argc, argv);
    free_fs_arguments(argc, argv);
    clear_fs_error(&error);
    return result;
}
