/** @file process_exit_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent no-drain external watchdog for process exit contracts (#377).
 */
#ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#endif
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/language_execution.h"
#include "test/support/process_watch.h"
#include "test/support/registry.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include "lib/platform/windows_target.h"
#    include "lib/process/windows_command_line.h"

#    include <windows.h>
#else
#    include <fcntl.h>
#    include <signal.h>
#    include <sys/socket.h>
#    include <sys/wait.h>
#    include <time.h>
#    include <unistd.h>
#endif
#define REQUIRE(x) scenario_require(r, (x), #x)

typedef struct {
    scenario_run_t *run;
#ifdef _WIN32
    HANDLE process, input, output[2];
#else
    pid_t pid;
    int input, output[2];
#endif
    bool started, reaped, normal;
    unsigned long status;
    unsigned char *bytes[2];
    size_t sizes[2];
    FILE *captures[2], *log;
} owner_t;

typedef struct {
    const char *normal, *failure, *delayed, *kind;
    unsigned serial;
} config_t;

static uint64_t now(scenario_run_t *r) {
    uint64_t value = process_watch_now_ms();
    REQUIRE(value != UINT64_MAX);
    return value;
}

static uint64_t deadline(scenario_run_t *r, uint64_t duration) {
    uint64_t clock = child_monotonic_ms(), end = scenario_run_deadline(r);
    uint64_t remaining = end > clock ? end - clock : 0;
    if (remaining > duration)
        remaining = duration;
    return now(r) + remaining;
}

static void pause_ms(unsigned delay) {
#ifdef _WIN32
    Sleep(delay);
#else
    struct timespec wait = {(time_t)(delay / 1000), (long)(delay % 1000) * 1000000};
    while (nanosleep(&wait, &wait) && errno == EINTR) {
    }
#endif
}
#ifdef _WIN32
static void close_native(HANDLE *h) {
    if (*h && *h != INVALID_HANDLE_VALUE)
        CloseHandle(*h);
    *h = NULL;
}
#else
static void close_native(int *fd) {
    if (*fd >= 0)
        close(*fd);
    *fd = -1;
}
#endif
static bool poll_owner(owner_t *o) {
    if (o->reaped)
        return true;
#ifdef _WIN32
    DWORD result = WaitForSingleObject(o->process, 0);
    if (result == WAIT_TIMEOUT)
        return true;
    DWORD code = 0;
    if (result != WAIT_OBJECT_0 || !GetExitCodeProcess(o->process, &code))
        return false;
    o->status = code;
    o->normal = true;
    o->reaped = true;
#else
    int status = 0;
    pid_t result = waitpid(o->pid, &status, WNOHANG);
    if (result == 0 || (result < 0 && errno == EINTR))
        return true;
    if (result != o->pid)
        return false;
    o->normal = WIFEXITED(status);
    o->status = o->normal ? (unsigned long)WEXITSTATUS(status) : (unsigned long)WTERMSIG(status);
    o->reaped = true;
#endif
    return true;
}

/* Failure teardown captures only after OS exit. Never let diagnostics unblock a
 * writer while the exit deadline is being measured. No fail-fast API in cleanup. */
static bool cleanup_capture(owner_t *o, size_t channel) {
    if (!o->reaped || !o->captures[channel])
        return true;
    unsigned char buffer[8192];
    uint64_t end = process_watch_now_ms() + 1000;
    for (;;) {
        if (process_watch_now_ms() >= end)
            return false;
#ifdef _WIN32
        if (!o->output[channel])
            return true;
        DWORD available = 0, count = 0;
        if (!PeekNamedPipe(o->output[channel], NULL, 0, NULL, &available, NULL))
            return GetLastError() == ERROR_BROKEN_PIPE;
        if (!available)
            return true;
        DWORD take = available < sizeof(buffer) ? available : (DWORD)sizeof(buffer);
        if (!ReadFile(o->output[channel], buffer, take, &count, NULL))
            return false;
        size_t size = count;
#else
        if (o->output[channel] < 0)
            return true;
        int flags = fcntl(o->output[channel], F_GETFL);
        if (flags < 0 || fcntl(o->output[channel], F_SETFL, flags | O_NONBLOCK) < 0)
            return false;
        ssize_t count = read(o->output[channel], buffer, sizeof(buffer));
        if (!count)
            return true;
        if (count < 0) {
            if (errno == EINTR)
                continue;
            return errno == EAGAIN || errno == EWOULDBLOCK;
        }
        size_t size = (size_t)count;
#endif
        if (fwrite(buffer, 1, size, o->captures[channel]) != size)
            return false;
    }
}

static bool close_owner(void *arg) {
    owner_t *o = arg;
    bool ok = true;
    if (o->started && !o->reaped) {
#ifdef _WIN32
        if (!TerminateProcess(o->process, 137) && GetLastError() != ERROR_ACCESS_DENIED)
            ok = false;
#else
        if (kill(o->pid, SIGKILL) && errno != ESRCH)
            ok = false;
#endif
        uint64_t end = process_watch_now_ms() + 8000;
        while (!o->reaped) {
            if (!poll_owner(o)) {
                ok = false;
                break;
            }
            if (o->reaped)
                break;
            if (process_watch_now_ms() >= end) {
                ok = false;
                break;
            }
            pause_ms(1);
        }
    }
    close_native(&o->input);
    for (size_t i = 0; i < 2; i++) {
        if (!cleanup_capture(o, i))
            ok = false;
        close_native(&o->output[i]);
        if (o->captures[i] && fclose(o->captures[i]))
            ok = false;
        free(o->bytes[i]);
    }
#ifdef _WIN32
    close_native(&o->process);
#endif
    if (o->log) {
        if (fprintf(o->log,
                    "final reaped=%d normal=%d status=%lu\n",
                    o->reaped,
                    o->normal,
                    o->status)
            < 0)
            ok = false;
        if (fclose(o->log))
            ok = false;
    }
    free(o);
    return ok;
}
#ifdef _WIN32
static wchar_t *wide(scenario_run_t *r, const char *text) {
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    REQUIRE(count > 0);
    wchar_t *value = language_owned(r, ALLOC((size_t)count * sizeof(*value)));
    REQUIRE(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, value, count) == count);
    return value;
}
#endif
static owner_t *
launch(scenario_run_t *r, config_t *c, const char *exe, const char *mode, const char *marker) {
    owner_t *o = calloc(1, sizeof(*o));
    REQUIRE(o);
    o->run = r;
#ifndef _WIN32
    o->input = o->output[0] = o->output[1] = -1;
#endif
    scenario_defer(r, close_owner, o);
    const char *root = scenario_work_directory(scenario_run_context(r));
    char name[64];
    unsigned serial = ++c->serial;
    for (size_t i = 0; i < 2; i++) {
        snprintf(name, sizeof(name), "owner-%u-%s.bin", serial, i ? "stderr" : "stdout");
        o->captures[i] = FOPEN(language_join(r, root, name), "wb");
        REQUIRE(o->captures[i]);
    }
    snprintf(name, sizeof(name), "owner-%u.log", serial);
    o->log = FOPEN(language_join(r, root, name), "wb");
    REQUIRE(o->log);
    REQUIRE(fprintf(o->log,
                    "executable=%s\nmode=%s\nmarker=%s\nno reads before owner exit\n",
                    exe,
                    mode,
                    marker ? marker : "none")
            > 0);
    REQUIRE(!fflush(o->log));
    const char *args[] = {exe, mode, marker, NULL};
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE inherited[3] = {0};
    SIZE_T size = 0;
    STARTUPINFOEXW startup = {0};
    PROCESS_INFORMATION process = {0};
    bool initialized = false;
    char *quoted = create_windows_command_line(args);
    REQUIRE(quoted);
    language_owned(r, quoted);
    wchar_t *command = wide(r, quoted), *executable = wide(r, exe);
    bool ok = CreatePipe(&inherited[0], &o->input, &sa, 0)
              && SetHandleInformation(o->input, HANDLE_FLAG_INHERIT, 0)
              && CreatePipe(&o->output[0], &inherited[1], &sa, 0)
              && SetHandleInformation(o->output[0], HANDLE_FLAG_INHERIT, 0)
              && CreatePipe(&o->output[1], &inherited[2], &sa, 0)
              && SetHandleInformation(o->output[1], HANDLE_FLAG_INHERIT, 0);
    if (ok) {
        InitializeProcThreadAttributeList(NULL, 1, 0, &size);
        startup.lpAttributeList = malloc(size);
        ok = startup.lpAttributeList != NULL;
        if (ok)
            initialized = InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &size);
        ok = ok && initialized;
    }
    if (ok)
        ok = UpdateProcThreadAttribute(startup.lpAttributeList,
                                       0,
                                       PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                       inherited,
                                       sizeof(inherited),
                                       NULL,
                                       NULL);
    if (ok) {
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = inherited[0];
        startup.StartupInfo.hStdOutput = inherited[1];
        startup.StartupInfo.hStdError = inherited[2];
        ok = CreateProcessW(executable,
                            command,
                            NULL,
                            NULL,
                            TRUE,
                            EXTENDED_STARTUPINFO_PRESENT,
                            NULL,
                            NULL,
                            &startup.StartupInfo,
                            &process);
    }
    for (size_t i = 0; i < 3; i++)
        close_native(&inherited[i]);
    if (initialized)
        DeleteProcThreadAttributeList(startup.lpAttributeList);
    free(startup.lpAttributeList);
    if (ok) {
        o->process = process.hProcess;
        CloseHandle(process.hThread);
        o->started = true;
    }
    REQUIRE(ok);
#else
    int input[2] = {-1, -1}, out[2] = {-1, -1}, err[2] = {-1, -1};
    bool ok = !socketpair(AF_UNIX, SOCK_STREAM, 0, input) && !pipe(out) && !pipe(err);
    if (!ok) {
        for (size_t i = 0; i < 2; i++) {
            close_native(input + i);
            close_native(out + i);
            close_native(err + i);
        }
        REQUIRE(ok);
    }
    pid_t pid = fork();
    if (pid == 0) {
        if (dup2(input[0], 0) < 0 || dup2(out[1], 1) < 0 || dup2(err[1], 2) < 0)
            _exit(126);
        for (size_t i = 0; i < 2; i++) {
            close(input[i]);
            close(out[i]);
            close(err[i]);
        }
        execv(exe, (char *const *)args);
        _exit(127);
    }
    close_native(&input[0]);
    close_native(&out[1]);
    close_native(&err[1]);
    o->input = input[1];
    o->output[0] = out[0];
    o->output[1] = err[0];
    o->pid = pid;
    o->started = pid > 0;
    REQUIRE(o->started);
    REQUIRE(fcntl(o->input, F_SETFL, fcntl(o->input, F_GETFL) | O_NONBLOCK) >= 0);
    for (size_t i = 0; i < 2; i++)
        REQUIRE(fcntl(o->output[i], F_SETFL, fcntl(o->output[i], F_GETFL) | O_NONBLOCK) >= 0);
#endif
    return o;
}

static void wait_owner(owner_t *o, uint64_t duration) {
    scenario_run_t *r = o->run;
    uint64_t end = deadline(r, duration);
    for (;;) {
        REQUIRE(poll_owner(o));
        if (o->reaped)
            break;
        REQUIRE(now(r) < end);
        pause_ms(1);
    }
}

static void ready(owner_t *o, const char *marker) {
    scenario_run_t *r = o->run;
    uint64_t end = deadline(r, 15000);
    for (;;) {
        fs_error_t e = {0};
        fs_info_t info;
        bool exists = inspect_fs_path(marker, true, &info, &e);
        bool missing = e.status == FS_NOT_FOUND;
        clear_fs_error(&e);
        REQUIRE(exists || missing);
        if (exists)
            return;
        REQUIRE(poll_owner(o));
        REQUIRE(!o->reaped);
        REQUIRE(now(r) < end);
        pause_ms(5);
    }
}

static void release(owner_t *o) {
    scenario_run_t *r = o->run;
#ifdef _WIN32
    DWORD count = 0;
    REQUIRE(WriteFile(o->input, "x", 1, &count, NULL) && count == 1);
#else
    uint64_t end = deadline(r, 8000);
    for (;;) {
        ssize_t count = send(o->input, "x", 1, MSG_NOSIGNAL);
        if (count == 1)
            break;
        REQUIRE(count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK));
        REQUIRE(now(r) < end);
        pause_ms(1);
    }
#endif
}

static bool contains(const unsigned char *data, size_t size, const char *needle) {
    size_t length = strlen(needle);
    for (size_t i = 0; i + length <= size; i++)
        if (!memcmp(data + i, needle, length))
            return true;
    return false;
}

static void capture(owner_t *o) {
    scenario_run_t *r = o->run;
    REQUIRE(o->reaped);
    uint64_t end = deadline(r, 8000);
    for (size_t channel = 0; channel < 2; channel++)
        for (;;) {
            REQUIRE(now(r) < end);
            unsigned char buffer[8192];
            size_t size = 0;
            bool pending = false;
#ifdef _WIN32
            if (!o->output[channel])
                break;
            DWORD available = 0, count = 0;
            if (!PeekNamedPipe(o->output[channel], NULL, 0, NULL, &available, NULL)) {
                REQUIRE(GetLastError() == ERROR_BROKEN_PIPE);
                close_native(&o->output[channel]);
                break;
            }
            if (!available)
                pending = true;
            else {
                DWORD take = available < sizeof(buffer) ? available : (DWORD)sizeof(buffer);
                REQUIRE(ReadFile(o->output[channel], buffer, take, &count, NULL));
                size = count;
            }
#else
            if (o->output[channel] < 0)
                break;
            ssize_t count = read(o->output[channel], buffer, sizeof(buffer));
            if (count == 0) {
                close_native(&o->output[channel]);
                break;
            }
            if (count < 0) {
                REQUIRE(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR);
                pending = true;
            } else
                size = (size_t)count;
#endif
            if (pending) {
                REQUIRE(now(r) < end);
                pause_ms(1);
                continue;
            }
            REQUIRE(size < SIZE_MAX - o->sizes[channel]);
            unsigned char *next = realloc(o->bytes[channel], o->sizes[channel] + size + 1);
            REQUIRE(next);
            o->bytes[channel] = next;
            memcpy(next + o->sizes[channel], buffer, size);
            o->sizes[channel] += size;
            next[o->sizes[channel]] = 0;
            REQUIRE(fwrite(buffer, 1, size, o->captures[channel]) == size);
            REQUIRE(!fflush(o->captures[channel]));
        }
}

static double coordinated(scenario_run_t *r, config_t *c, const char *mode) {
    char name[64];
    snprintf(name, sizeof(name), "ready-%u", c->serial + 1);
    const char *marker = language_join(r, scenario_work_directory(scenario_run_context(r)), name);
    owner_t *o = launch(r, c, c->normal, mode, marker);
    ready(o, marker);
    uint64_t started = now(r);
    if (!strcmp(mode, "shared-budget")) {
        uint64_t release_at = started + 800;
        while (now(r) < release_at) {
            scenario_checkpoint(r);
            uint64_t remaining = release_at - now(r);
            pause_ms(remaining > 5 ? 5 : (unsigned)remaining);
        }
    }
    if (!strcmp(mode, "pipe-error"))
        close_native(&o->output[0]);
    release(o);
    wait_owner(o, 8000);
    double elapsed = (double)(now(r) - started) / 1000.0;
    close_native(&o->input);
    capture(o);
    REQUIRE(fprintf(o->log, "elapsed=%.3f normal=%d status=%lu\n", elapsed, o->normal, o->status)
            > 0);
    REQUIRE(!fflush(o->log));
    REQUIRE(o->normal);
    unsigned long code = !strcmp(mode, "fast")                                     ? 255
                         : (!strcmp(mode, "error") || !strcmp(mode, "pipe-error")) ? 77
                                                                                   : 73;
    REQUIRE(!strcmp(mode, "race") ? (o->status == 0 || o->status == 7) : o->status == code);
    size_t combined_size = o->sizes[0] + o->sizes[1];
    unsigned char *combined = language_owned(r, ALLOC(combined_size + 1));
    if (o->sizes[0])
        memcpy(combined, o->bytes[0], o->sizes[0]);
    if (o->sizes[1])
        memcpy(combined + o->sizes[0], o->bytes[1], o->sizes[1]);
    REQUIRE(!contains(combined, combined_size, "ATEXIT"));
    if (!strcmp(mode, "fast")) {
        const char *expected[] = {"stdout-without-newline", "stderr-without-newline"};
        for (size_t i = 0; i < 2; i++)
            scenario_require_bytes(r,
                                   "unflushed output",
                                   expected[i],
                                   strlen(expected[i]),
                                   o->bytes[i],
                                   o->sizes[i],
                                   false);
    } else if (!strncmp(mode, "write-", 6) || !strncmp(mode, "lock-", 5))
        REQUIRE(elapsed >= 0.75 && elapsed < 5);
    else if (!strcmp(mode, "shared-budget"))
        REQUIRE(elapsed >= 0.75 && elapsed < 1.65);
    return elapsed;
}

static void body(scenario_run_t *r, void *arg) {
    config_t *c = arg;
    const char *kind = c->kind;
    if (!strcmp(kind, "arbiter") || !strcmp(kind, "unprepared") || !strcmp(kind, "prepare-race")
        || !strcmp(kind, "failed-prepare") || !strcmp(kind, "delayed-prepare")) {
        bool failed = !strcmp(kind, "failed-prepare"), delayed = !strcmp(kind, "delayed-prepare"),
             unprepared = !strcmp(kind, "unprepared");
        uint64_t started = now(r);
        owner_t *o = launch(r,
                            c,
                            failed    ? c->failure
                            : delayed ? c->delayed
                                      : c->normal,
                            (failed || delayed) ? "failed-prepare" : kind,
                            NULL);
        wait_owner(o, delayed ? 8000 : (failed || unprepared) ? 5000 : 10000);
        double elapsed = (double)(now(r) - started) / 1000.0;
        REQUIRE(o->normal
                && o->status
                       == (unsigned long)((failed || delayed) ? 91
                                          : unprepared        ? 83
                                                              : 0));
        if (delayed)
            REQUIRE(elapsed >= 0.75 && elapsed < 5);
        capture(o);
    } else if (!strcmp(kind, "fast-three")) {
        double minimum = 100;
        for (unsigned i = 0; i < 3; i++) {
            double elapsed = coordinated(r, c, "fast");
            if (elapsed < minimum)
                minimum = elapsed;
        }
        REQUIRE(minimum < 0.9);
    } else
        coordinated(r, c, !strncmp(kind, "race-", 5) ? "race" : kind);
}

int main(int argc, char **argv) {
#ifdef _WIN32
    fs_error_t e = {0};
    if (!fs_arguments(&argc, &argv, &e)) {
        clear_fs_error(&e);
        return 1;
    }
#endif
    int result = 1;
    if (argc == 7 || (argc == 8 && !strcmp(argv[7], "--keep-work"))) {
        char *paths[7] = {0};
        bool valid = true;
        for (size_t i = 2; i < 7; i++) {
            fs_error_t e = {0};
            paths[i] = absolute_fs_path(argv[i], &e);
            clear_fs_error(&e);
            valid &= paths[i] != NULL;
        }
        const char *names[] = {"arbiter",         "unprepared",    "prepare-race", "failed-prepare",
                               "delayed-prepare", "fast-three",    "error",        "pipe-error",
                               "write-stdout",    "write-stderr",  "write-both",   "lock-stdout",
                               "lock-stderr",     "shared-budget", "race-00",      "race-01",
                               "race-02",         "race-03",       "race-04",      "race-05",
                               "race-06",         "race-07"};

        enum { COUNT = sizeof(names) / sizeof(names[0]) };

        config_t configs[COUNT];
        scenario_definition_t defs[COUNT];
        char ids[COUNT][128];
        for (size_t i = 0; i < COUNT; i++) {
            snprintf(ids[i], sizeof(ids[i]), "integration.process_exit.%s", names[i]);
            configs[i] = (config_t){paths[2], paths[3], paths[4], names[i], 0};
            defs[i] = (scenario_definition_t){ids[i],
                                              !strcmp(names[i], "fast-three") ? 90000 : 40000,
                                              body,
                                              configs + i,
                                              NULL};
        }
        scenario_options_t options = {.build_directory = paths[5], .data_directory = paths[6]};
        const char *selection[] = {argv[1], argc == 8 ? "--keep-work" : NULL};
        if (valid)
            result = scenario_registry_main(defs, COUNT, argc == 8 ? 2 : 1, selection, &options);
        for (size_t i = 2; i < 7; i++)
            FREE(paths[i]);
    } else
        fputs("usage: process_exit_scenarios ID NORMAL FAILURE DELAYED BUILD DATA [--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
