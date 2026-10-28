#ifndef _WIN32
#    define _POSIX_C_SOURCE 200809L
#endif
/** @file process_watch_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent native fixtures for the process observer; no Goat process operations.
 */
#include "test/support/process_watch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <windows.h>
#else
#    include <dirent.h>
#    include <errno.h>
#    include <poll.h>
#    include <signal.h>
#    include <sys/wait.h>
#    include <unistd.h>
#endif

typedef struct {
    uint64_t pid;
#ifdef _WIN32
    HANDLE process, ready, release;
#else
    int ready, release;
#endif
} fixture_t;

static unsigned long error;
static const char *self;
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "line %d: %s (OS error %lu)\n", __LINE__, #condition, error);          \
            success = false;                                                                       \
            goto done;                                                                             \
        }                                                                                          \
    } while (0)

static int fixture_body(fixture_t *f, uint64_t owner) {
    process_watch_t *watch = NULL;
    if (owner) {
        watch = process_watch_open(owner, &error);
        if (!watch)
            return 10;
    }
#ifdef _WIN32
    if (!SetEvent(f->ready))
        return 11;
#else
    if (write(f->ready, "R", 1) != 1)
        return 11;
#endif
    int result = 0;
    if (watch) {
        if (process_watch_wait(watch, process_watch_now_ms() + 10000, &error) != PROCESS_WATCH_OK)
            result = 12;
        if (process_watch_close(&watch, &error) != PROCESS_WATCH_OK)
            result = 13;
    } else {
#ifdef _WIN32
        if (WaitForSingleObject(f->release, 10000) != WAIT_OBJECT_0)
            result = 14;
#else
        struct pollfd p = {f->release, POLLIN, 0};
        if (poll(&p, 1, 10000) != 1)
            result = 14;
        else {
            char byte;
            if (read(f->release, &byte, 1) != 1)
                result = 15;
        }
#endif
    }
    return result;
}

static bool spawn_fixture(fixture_t *f, uint64_t owner) {
    memset(f, 0, sizeof(*f));
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    f->ready = CreateEventA(&sa, TRUE, FALSE, NULL);
    f->release = CreateEventA(&sa, TRUE, FALSE, NULL);
    if (!f->ready || !f->release)
        return false;
    char command[32768];
    snprintf(command,
             sizeof(command),
             "\"%s\" --fixture %llu %llu %llu",
             self,
             (unsigned long long)(uintptr_t)f->ready,
             (unsigned long long)(uintptr_t)f->release,
             (unsigned long long)owner);
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    if (!CreateProcessA(NULL, command, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    f->process = pi.hProcess;
    f->pid = pi.dwProcessId;
    return WaitForSingleObject(f->ready, 5000) == WAIT_OBJECT_0;
#else
    int ready[2], release[2];
    f->ready = f->release = -1;
    if (pipe(ready))
        return false;
    if (pipe(release)) {
        close(ready[0]);
        close(ready[1]);
        return false;
    }
    pid_t pid = fork();
    if (pid == 0) {
        close(ready[0]);
        close(release[1]);
        f->ready = ready[1];
        f->release = release[0];
        _exit(fixture_body(f, owner));
    }
    close(ready[1]);
    close(release[0]);
    f->ready = ready[0];
    f->release = release[1];
    if (pid < 0)
        return false;
    f->pid = (uint64_t)pid;
    struct pollfd p = {f->ready, POLLIN, 0};
    char byte;
    return poll(&p, 1, 5000) == 1 && read(f->ready, &byte, 1) == 1 && byte == 'R';
#endif
}

static bool release_fixture(fixture_t *f) {
#ifdef _WIN32
    return SetEvent(f->release) != 0;
#else
    return write(f->release, "Q", 1) == 1;
#endif
}

/* Reap only our own children. An unreaped POSIX child cannot have its PID reused;
 * Windows cleanup always uses the original CreateProcess handle. */
static bool dispose_fixture(fixture_t *f, bool expect_success) {
    bool ok = true;
#ifdef _WIN32
    if (f->process) {
        if (WaitForSingleObject(f->process, 0) != WAIT_OBJECT_0) {
            if (expect_success)
                ok = false;
            TerminateProcess(f->process, 98);
        }
        if (WaitForSingleObject(f->process, 5000) != WAIT_OBJECT_0)
            ok = false;
        DWORD code;
        if (expect_success && (!GetExitCodeProcess(f->process, &code) || code))
            ok = false;
        CloseHandle(f->process);
    }
    if (f->ready)
        CloseHandle(f->ready);
    if (f->release)
        CloseHandle(f->release);
#else
    if (f->pid) {
        int status = 0;
        pid_t result;
        do {
            result = waitpid((pid_t)f->pid, &status, WNOHANG);
        } while (result < 0 && errno == EINTR);
        if (result == 0) {
            if (expect_success)
                ok = false;
            kill((pid_t)f->pid, SIGKILL);
            do {
                result = waitpid((pid_t)f->pid, &status, 0);
            } while (result < 0 && errno == EINTR);
        }
        if (result < 0 || (expect_success && (!WIFEXITED(status) || WEXITSTATUS(status))))
            ok = false;
    }
    if (f->ready >= 0)
        close(f->ready);
    if (f->release >= 0)
        close(f->release);
#endif
    memset(f, 0, sizeof(*f));
#ifndef _WIN32
    f->ready = f->release = -1;
#endif
    return ok;
}

static size_t handles(void) {
#ifdef _WIN32
    DWORD count;
    return GetProcessHandleCount(GetCurrentProcess(), &count) ? count : (size_t)-1;
#else
    DIR *directory = opendir("/proc/self/fd");
    if (!directory)
        return (size_t)-1;
    size_t count = 0;
    while (readdir(directory))
        ++count;
    closedir(directory);
    return count;
#endif
}

static process_watch_result_t condition(void *context, unsigned long *native_error) {
    int *remaining = context;
    (void)native_error;
    return --*remaining <= 0 ? PROCESS_WATCH_OK : PROCESS_WATCH_TIMEOUT;
}

static process_watch_result_t condition_error(void *context, unsigned long *native_error) {
    (void)context;
    *native_error = 123;
    return PROCESS_WATCH_ERROR;
}

static bool condition_test(void) {
    bool success = true;
    int remaining = 3;
    CHECK(process_watch_until(condition, &remaining, process_watch_now_ms() + 1000, &error)
          == PROCESS_WATCH_OK);
    CHECK(remaining == 0);
    remaining = 1000000;
    uint64_t started = process_watch_now_ms();
    CHECK(process_watch_until(condition, &remaining, started + 25, &error)
          == PROCESS_WATCH_TIMEOUT);
    CHECK(process_watch_now_ms() >= started + 25);
    CHECK(process_watch_now_ms() - started < 5000);
    CHECK(process_watch_until(condition_error, NULL, process_watch_now_ms() + 1000, &error)
          == PROCESS_WATCH_ERROR);
    CHECK(error == 123);
    CHECK(process_watch_until(NULL, NULL, process_watch_now_ms(), &error) == PROCESS_WATCH_ERROR);
done:
    return success;
}

static bool process_test(const char *mode) {
    bool success = true, alive = false;
    fixture_t child = {0}, observer = {0};
#ifndef _WIN32
    child.ready = child.release = observer.ready = observer.release = -1;
#endif
    process_watch_t *watch = NULL, *other = NULL;
    size_t initial = handles();
    CHECK(initial != (size_t)-1);
    CHECK(spawn_fixture(&child, 0));
    watch = process_watch_open(child.pid, &error);
    CHECK(watch);
    CHECK(process_watch_alive(watch, &alive, &error) == PROCESS_WATCH_OK && alive);
    if (!strcmp(mode, "deadline")) {
        uint64_t started = process_watch_now_ms();
        CHECK(process_watch_wait(watch, started + 25, &error) == PROCESS_WATCH_TIMEOUT);
        CHECK(process_watch_now_ms() >= started + 25);
        CHECK(process_watch_now_ms() - started < 5000);
        CHECK(process_watch_alive(watch, &alive, &error) == PROCESS_WATCH_OK && alive);
    }
    if (!strcmp(mode, "close")) {
        CHECK(!process_watch_open(0, &error));
        CHECK(!process_watch_open(UINT64_MAX, &error));
        CHECK(process_watch_close(NULL, &error) == PROCESS_WATCH_ERROR);
        CHECK(process_watch_close(&watch, &error) == PROCESS_WATCH_OK && !watch);
        CHECK(process_watch_close(&watch, &error) == PROCESS_WATCH_OK);
        CHECK(process_watch_alive(watch, &alive, &error) == PROCESS_WATCH_ERROR);
        CHECK(process_watch_wait(watch, process_watch_now_ms(), &error) == PROCESS_WATCH_ERROR);
        CHECK(process_watch_kill(watch, &error) == PROCESS_WATCH_ERROR);
        watch = process_watch_open(child.pid, &error);
        CHECK(watch);
        for (int i = 0; i < 128; ++i) {
            other = process_watch_open(child.pid, &error);
            CHECK(other);
            CHECK(process_watch_close(&other, &error) == PROCESS_WATCH_OK);
        }
    }
    if (!strcmp(mode, "owner-death")) {
        CHECK(spawn_fixture(&observer, child.pid));
        other = process_watch_open(observer.pid, &error);
        CHECK(other);
        CHECK(process_watch_alive(other, &alive, &error) == PROCESS_WATCH_OK && alive);
        CHECK(process_watch_kill(watch, &error) == PROCESS_WATCH_OK);
        CHECK(process_watch_wait(other, process_watch_now_ms() + 5000, &error) == PROCESS_WATCH_OK);
        CHECK(dispose_fixture(&observer, true));
    } else if (!strcmp(mode, "kill")) {
        CHECK(process_watch_kill(watch, &error) == PROCESS_WATCH_OK);
    } else {
        CHECK(release_fixture(&child));
    }
    CHECK(process_watch_wait(watch, process_watch_now_ms() + 5000, &error) == PROCESS_WATCH_OK);
    CHECK(process_watch_alive(watch, &alive, &error) == PROCESS_WATCH_OK && !alive);
    CHECK(process_watch_wait(watch, process_watch_now_ms(), &error) == PROCESS_WATCH_OK);
    /* Native reap, then another live process must not be affected by the retained identity. */
    CHECK(dispose_fixture(&child, strcmp(mode, "kill") && strcmp(mode, "owner-death")));
    CHECK(spawn_fixture(&child, 0));
    CHECK(process_watch_alive(watch, &alive, &error) == PROCESS_WATCH_OK && !alive);
    process_watch_kill(watch,
                       &error); /* Dead identity: either already-dead success or native error. */
    CHECK(process_watch_close(&other, &error) == PROCESS_WATCH_OK);
    other = process_watch_open(child.pid, &error);
    CHECK(other);
    CHECK(process_watch_alive(other, &alive, &error) == PROCESS_WATCH_OK && alive);
    CHECK(release_fixture(&child));
    process_watch_result_t waited =
        process_watch_wait(other, process_watch_now_ms() + 5000, &error);
    process_watch_close(&other, &error);
    CHECK(waited == PROCESS_WATCH_OK);
    CHECK(dispose_fixture(&child, true));
done:
    process_watch_close(&other, &error);
    process_watch_close(&watch, &error);
    dispose_fixture(&observer, false);
    dispose_fixture(&child, false);
    if (handles() != initial) {
        fprintf(stderr, "native handle leak\n");
        success = false;
    }
    return success;
}

int main(int argc, char **argv) {
    self = argv[0];
#ifdef _WIN32
    if (argc == 5 && !strcmp(argv[1], "--fixture")) {
        fixture_t f = {0};
        f.ready = (HANDLE)(uintptr_t)strtoull(argv[2], NULL, 10);
        f.release = (HANDLE)(uintptr_t)strtoull(argv[3], NULL, 10);
        return fixture_body(&f, strtoull(argv[4], NULL, 10));
    }
#endif
    if (argc == 2 && !strcmp(argv[1], "fixture-stdio")) {
        puts("READY");
        fflush(stdout);
        while (getchar() != EOF) {
        }
        return 0;
    }
    if (argc != 2)
        return 2;
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif
    if (!strcmp(argv[1], "condition"))
        return condition_test() ? 0 : 1;
    const char *modes[] = {"alive-wait", "kill", "deadline", "close", "owner-death"};
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i)
        if (!strcmp(argv[1], modes[i]))
            return process_test(argv[1]) ? 0 : 1;
    return 2;
}
