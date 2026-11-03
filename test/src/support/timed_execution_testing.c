/** @file timed_execution_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Timed capture contracts, including binary boundaries and owned-tree cleanup.
 */
#ifndef _WIN32
#    define _POSIX_C_SOURCE 200809L
#endif
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "lib/platform/windows_target.h"
#include "test/support/execution.h"
#include "test/support/monotonic.h"
#include "test/support/process_watch.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#    include <wchar.h>
#    include <windows.h>
#else
#    include <pthread.h>
#    include <time.h>
#    include <unistd.h>
#endif
/* Unlike assert(), these checks remain enabled in Release builds. */
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "Failed line %d: %s\n", __LINE__, #x);                                 \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
static const unsigned char binary[] = {' ', 0, 'x', '\r', '\n', 255, '\n'};

static void pause_ms(unsigned ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec delay = {ms / 1000, (long)(ms % 1000) * 1000000};
    nanosleep(&delay, NULL);
#endif
}

static char *path(const char *work, const char *name) {
    fs_error_t error = {0};
    char *p = join_fs_path(work, name, &error);
    clear_fs_error(&error);
    return p;
}

static bool present(const char *p) {
    FILE *f = FOPEN(p, "rb");
    return f && fclose(f) == 0;
}

static int leaf(void) {
    FILE *f = FOPEN("descendant.pid", "wb");
    CHECK(f);
#ifdef _WIN32
    CHECK(fprintf(f, "%lu\n", (unsigned long)GetCurrentProcessId()) > 0);
#else
    CHECK(fprintf(f, "%lu\n", (unsigned long)getpid()) > 0);
#endif
    CHECK(!fclose(f));
    for (;;)
        pause_ms(10);
}

static int worker(const char *mode) {
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
    if (!strcmp(mode, "leaf"))
        return leaf();
    if (!strncmp(mode, "descendant-", 11)) {
        /* Raw spawn ensures containment is not supplied by a nested tested API. */
#ifdef _WIN32
        wchar_t exe[32768], command[33000];
        CHECK(GetModuleFileNameW(NULL, exe, 32768));
        CHECK(swprintf(command, 33000, L"\"%ls\" --worker leaf", exe) > 0);
        STARTUPINFOW startup = {.cb = sizeof(startup)};
        PROCESS_INFORMATION process = {0};
        CHECK(CreateProcessW(exe,
                             command,
                             NULL,
                             NULL,
                             TRUE,
                             CREATE_NO_WINDOW,
                             NULL,
                             NULL,
                             &startup,
                             &process));
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
#else
        pid_t pid = fork();
        CHECK(pid >= 0);
        if (!pid) {
            CHECK(setsid() >= 0);
            return leaf();
        }
#endif
        /* The observer pins the live identity before allowing the owner to exit. */
        while (!present("observer-ready"))
            pause_ms(1);
        CHECK(fwrite(binary, 1, sizeof(binary), stdout) == sizeof(binary));
        CHECK(!fflush(stdout));
        if (!strcmp(mode, "descendant-exit"))
            return 0;
        for (;;)
            pause_ms(10);
    }
    if (!strcmp(mode, "timeout")) {
        CHECK(fwrite(binary, 1, sizeof(binary), stderr) == sizeof(binary));
        CHECK(!fflush(stderr));
        for (;;)
            pause_ms(10);
    }
    if (!strcmp(mode, "crash")) {
#ifdef _WIN32
        TerminateProcess(GetCurrentProcess(), 0xc0000005);
#else
        raise(SIGKILL);
#endif
        return 99;
    }
    if (!strcmp(mode, "boundary") || !strncmp(mode, "truncate-", 9)) {
        size_t out = CHILD_OUTPUT_LIMIT + (!strcmp(mode, "truncate-out") ? 1u : 0u);
        size_t err = CHILD_OUTPUT_LIMIT + (!strcmp(mode, "truncate-err") ? 1u : 0u);
        for (size_t i = 0; i < out || i < err; ++i) {
            if (i < out)
                CHECK(fputc((int)(i % 251), stdout) != EOF);
            if (i < err)
                CHECK(fputc((int)(250 - i % 251), stderr) != EOF);
        }
        return 0;
    }
    if (!strcmp(mode, "duration"))
        pause_ms(100);
    CHECK(fwrite(binary, 1, sizeof(binary), stdout) == sizeof(binary));
    CHECK(fwrite(binary, 1, sizeof(binary), stderr) == sizeof(binary));
    return !strcmp(mode, "nonzero") ? 23 : 0;
}

typedef struct {
    const char *mode;
    char *work;
    bool reached, observer_started, observer_ok;
    unsigned cleaned;
    uint64_t elapsed;
    process_watch_t *watch;
#ifdef _WIN32
    HANDLE observer;
#else
    pthread_t observer;
#endif
} state_t;
#ifdef _WIN32
static DWORD WINAPI observe(void *opaque) {
#else
static void *observe(void *opaque) {
#endif
    state_t *s = opaque;
    char *pid_path = path(s->work, "descendant.pid");
    char *ready_path = path(s->work, "observer-ready");
    uint64_t deadline = process_watch_now_ms() + 5000;
    unsigned long long pid = 0;
    while (pid_path && process_watch_now_ms() < deadline) {
        FILE *f = FOPEN(pid_path, "rb");
        bool ok = f && fscanf(f, "%llu", &pid) == 1;
        if (f)
            fclose(f);
        if (ok)
            break;
        pause_ms(1);
    }
    unsigned long error = 0;
    bool alive = false;
    if (pid)
        s->watch = process_watch_open(pid, &error);
    if (s->watch && process_watch_alive(s->watch, &alive, &error) == PROCESS_WATCH_OK && alive) {
        FILE *f = ready_path ? FOPEN(ready_path, "wb") : NULL;
        s->observer_ok = f && fclose(f) == 0;
    }
    FREE(pid_path);
    FREE(ready_path);
    return 0;
}

static bool cleanup(void *opaque) {
    state_t *s = opaque;
    ++s->cleaned;
    return strcmp(s->mode, "cleanup-failure") != 0;
}

static bool join_observer(void *opaque) {
    state_t *s = opaque;
    if (!s->observer_started)
        return true;
#ifdef _WIN32
    bool ok = WaitForSingleObject(s->observer, 6000) == WAIT_OBJECT_0;
    if (ok)
        ok = CloseHandle(s->observer) != 0;
    return ok;
#else
    return pthread_join(s->observer, NULL) == 0;
#endif
}

static void body(scenario_run_t *run, void *opaque) {
    state_t *s = opaque;
    const char *work = scenario_work_directory(scenario_run_context(run));
    s->work = malloc(strlen(work) + 1);
    scenario_require(run, s->work != NULL, "copy workspace");
    strcpy(s->work, work);
    scenario_defer(run, cleanup, s);
    if (!strncmp(s->mode, "descendant-", 11)) {
        scenario_defer(run, join_observer, s);
#ifdef _WIN32
        s->observer = CreateThread(NULL, 0, observe, s, 0, NULL);
        s->observer_started = s->observer != NULL;
#else
        s->observer_started = pthread_create(&s->observer, NULL, observe, s) == 0;
#endif
        scenario_require(run, s->observer_started, "start independent observer");
    }
    const char *args[] = {"timed_execution_testing", "--worker", s->mode, NULL};
    child_options_t command = {.arguments = args};
    if (!strcmp(s->mode, "start-error"))
        command.executable = "./nonexistent-timed-executable";
    uint64_t before = 0, after = 0;
    scenario_require(run, test_monotonic_ns(&before), "read enclosing start clock");
    scenario_command_result_t result =
        scenario_command_capture_timed(run,
                                       &command,
                                       !strcmp(s->mode, "null-clock") ? NULL : &s->elapsed);
    scenario_require(run, test_monotonic_ns(&after) && after >= before, "read enclosing end clock");
    scenario_require(run,
                     s->elapsed > 0 && s->elapsed <= after - before,
                     "elapsed lies inside enclosing clock interval");
    if (!strcmp(s->mode, "duration"))
        scenario_require(run, s->elapsed >= 90000000, "timing includes child wait");
    scenario_require(run,
                     result.exit_code == (!strcmp(s->mode, "nonzero") ? 23 : 0),
                     "normal exit status preserved");
    if (!strcmp(s->mode, "boundary")) {
        scenario_require(run,
                         result.out_size == CHILD_OUTPUT_LIMIT
                             && result.err_size == CHILD_OUTPUT_LIMIT,
                         "exact capture limits must not truncate");
        for (size_t i = 0; i < CHILD_OUTPUT_LIMIT; ++i)
            scenario_require(run,
                             ((const unsigned char *)result.out)[i] == i % 251
                                 && ((const unsigned char *)result.err)[i] == 250 - i % 251,
                             "dual-stream binary payload preserved");
    } else if (strncmp(s->mode, "descendant-", 11)) {
        scenario_require_bytes(run,
                               "stdout",
                               binary,
                               sizeof(binary),
                               result.out,
                               result.out_size,
                               false);
        scenario_require_bytes(run,
                               "stderr",
                               binary,
                               sizeof(binary),
                               result.err,
                               result.err_size,
                               false);
    }
    s->reached = true;
}

static bool log_contains(const char *work, const char *needle) {
    char *p = path(work, "execution.log");
    char buffer[8192];
    FILE *f = p ? FOPEN(p, "rb") : NULL;
    size_t count = f ? fread(buffer, 1, sizeof(buffer) - 1, f) : 0;
    buffer[count] = 0;
    bool ok = f && !ferror(f) && strstr(buffer, needle);
    if (f && fclose(f))
        ok = false;
    FREE(p);
    return ok;
}

static bool artifact_matches(const char *work,
                             const char *name,
                             bool pattern,
                             bool err,
                             const void *bytes,
                             size_t size) {
    char *p = path(work, name);
    FILE *f = p ? FOPEN(p, "rb") : NULL;
    bool ok = f != NULL;
    for (size_t i = 0; ok && i < size; ++i) {
        int expected =
            pattern ? (err ? 250 - i % 251 : i % 251) : ((const unsigned char *)bytes)[i];
        ok = fgetc(f) == expected;
    }
    if (ok)
        ok = fgetc(f) == EOF && !ferror(f);
    if (f && fclose(f))
        ok = false;
    FREE(p);
    return ok;
}

static int run_test(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "--worker"))
        return worker(argv[2]);
    if (argc != 4)
        return 2;
    const char *m = argv[1];
    if (!strcmp(m, "clock")) {
        uint64_t a = 0, b = 0;
        CHECK(!test_monotonic_ns(NULL));
        CHECK(test_monotonic_ns(&a) && a > 0);
        for (unsigned i = 0; i < 10000; ++i) {
            CHECK(test_monotonic_ns(&b) && b >= a);
            a = b;
        }
        return 0;
    }
    const char *cases[] = {"success",
                           "nonzero",
                           "boundary",
                           "duration",
                           "truncate-out",
                           "truncate-err",
                           "timeout",
                           "crash",
                           "start-error",
                           "null-clock",
                           "cleanup-failure",
                           "descendant-exit",
                           "descendant-timeout"};
    size_t index = 0;
    while (index < sizeof(cases) / sizeof(*cases) && strcmp(m, cases[index]))
        ++index;
    CHECK(index < sizeof(cases) / sizeof(*cases));
    bool expected = index < 4 || !strcmp(m, "descendant-exit");
    state_t state = {.mode = m};
    scenario_options_t options = {m, argv[2], argv[3], true, stderr};
    uint64_t started = child_monotonic_ms();
    uint64_t timeout = !strcmp(m, "timeout")              ? 300
                       : !strcmp(m, "descendant-timeout") ? 6000
                                                          : 10000;
    bool actual = scenario_execute(&options, timeout, body, &state);
    CHECK(actual == expected);
    CHECK(child_monotonic_ms() - started < timeout + 8000);
    CHECK(state.work && state.cleaned == 1);
    CHECK(state.reached == (expected || !strcmp(m, "cleanup-failure")));
    if (!strcmp(m, "null-clock")) {
        CHECK(log_contains(state.work, "missing timed capture output"));
        CHECK(!log_contains(state.work, "command 1"));
    } else {
        CHECK(log_contains(state.work, "elapsed_ns="));
        CHECK(log_contains(state.work, "clock_ok=1"));
        /* Never accept a cleanup failure as an expected timeout/truncation failure. */
        CHECK(log_contains(state.work, "cleanup=0\n"));
    }
    if (!strncmp(m, "truncate-", 9) || !strcmp(m, "boundary")) {
        CHECK(artifact_matches(state.work,
                               "command-1.stdout",
                               true,
                               false,
                               NULL,
                               CHILD_OUTPUT_LIMIT));
        CHECK(
            artifact_matches(state.work, "command-1.stderr", true, true, NULL, CHILD_OUTPUT_LIMIT));
        CHECK(log_contains(state.work, !strcmp(m, "boundary") ? "capture_ok=1" : "capture_ok=0"));
    }
    if (!strcmp(m, "timeout") || !strcmp(m, "descendant-timeout")) {
        char status[64];
        snprintf(status, sizeof(status), "result status=%d", CHILD_TIMED_OUT);
        CHECK(log_contains(state.work, status));
    }
    if (!strcmp(m, "timeout"))
        CHECK(
            artifact_matches(state.work, "command-1.stderr", false, true, binary, sizeof(binary)));
    if (!strcmp(m, "start-error")) {
        char status[64];
        snprintf(status, sizeof(status), "result status=%d", CHILD_START_ERROR);
        CHECK(log_contains(state.work, status));
    }
    if (!strcmp(m, "crash")) {
#ifdef _WIN32
        CHECK(log_contains(state.work, "3221225477"));
#else
        CHECK(log_contains(state.work, "signal=9"));
#endif
    }
    if (!strcmp(m, "cleanup-failure"))
        CHECK(log_contains(state.work, "resource cleanup failed"));
    if (!strncmp(m, "descendant-", 11)) {
        unsigned long error = 0;
        bool alive = true;
        CHECK(state.observer_ok && state.watch);
#ifdef _WIN32
        /* Job active-process accounting can reach zero before a terminated
         * descendant's process object is signaled. The root-only wait in the
         * child API cannot synchronize that final teardown after a normal root
         * exit. Require OS exit notification on the identity pinned while alive,
         * within a bounded observation budget; never kill or reopen by PID. */
        uint64_t observed = process_watch_now_ms();
        CHECK(observed != UINT64_MAX && observed <= UINT64_MAX - 5000);
        CHECK(process_watch_wait(state.watch, observed + 5000, &error) == PROCESS_WATCH_OK);
#endif
        CHECK(process_watch_alive(state.watch, &alive, &error) == PROCESS_WATCH_OK && !alive);
        CHECK(process_watch_close(&state.watch, &error) == PROCESS_WATCH_OK);
        CHECK(
            artifact_matches(state.work, "command-1.stdout", false, false, binary, sizeof(binary)));
    }
    fs_error_t error = {0};
    /* Immediate single-attempt removal detects lingering handles on Windows. */
    CHECK(remove_fs_tree(state.work, &error));
    clear_fs_error(&error);
    free(state.work);
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
