/** @file module_loading_owner.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Full-output owner pumping and independent cleaner/owner observation (#369).
 */
#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif
#include "module_loading_owner.h"

#include "lib/platform/windows_target.h"
#ifdef _WIN32
#    include "lib/process/windows_command_line.h"

#    include <wchar.h>
#    include <windows.h>
#else
#    include <errno.h>
#    include <fcntl.h>
#    include <poll.h>
#    include <signal.h>
#    include <sys/socket.h>
#    include <sys/wait.h>
#    include <unistd.h>
#endif

#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "lib/strings/string_ext.h"
#include "test/support/language_execution.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#define REQUIRE(o, x) scenario_require((o)->run, (x), #x)

typedef struct {
    unsigned char *data;
    size_t size;
    FILE *file;
} stream_t;

struct loading_owner_t {
    scenario_run_t *run;
    child_result_t result;
#ifdef _WIN32
    HANDLE process, input, output[2];
#else
    pid_t pid;
    int input, output[2];
#endif
    bool started, reaped;
    char *directory;
    stream_t out, err;
    FILE *log;
    size_t line_offset, input_offset, input_size;
    bool eof, ended;
    process_watch_t **watches;
    size_t watch_count;
};

static bool free_doc(void *p) {
    yyjson_doc_free(p);
    return true;
}

static uint64_t deadline(loading_owner_t *o, uint64_t maximum) {
    uint64_t now = child_monotonic_ms(), end = scenario_run_deadline(o->run),
             remaining = end > now ? end - now : 0;
    if (remaining > maximum)
        remaining = maximum;
    uint64_t watch = process_watch_now_ms();
    return watch > UINT64_MAX - remaining ? UINT64_MAX : watch + remaining;
}

static const child_result_t *raw_poll(loading_owner_t *o);
static void raw_stop(loading_owner_t *o);
static void raw_close(loading_owner_t *o);

static bool close_owner(void *arg) {
    loading_owner_t *o = arg;
    bool ok = true;
    if (o->started && !o->reaped)
        raw_stop(o);
    if (o->log) {
        if (fprintf(o->log,
                    "final status=%d exit=%d signal=%d system=%lu cleanup=%lu reaped=%d\n",
                    (int)o->result.status,
                    o->result.exit_code,
                    o->result.signal_number,
                    (unsigned long)o->result.system_error,
                    (unsigned long)o->result.cleanup_error,
                    o->reaped)
                < 0
            || fclose(o->log))
            ok = false;
    }
    if (o->result.cleanup_error)
        ok = false;
    raw_close(o);
    /* Only handles pinned at live scenario barriers may be signalled here.
     * Never reopen a stale cleaner.pid during failure cleanup: a PID could have
     * been reused. Unobserved cleaners retain their real owner-death protocol,
     * as in the original Python launcher; killing the owner releases that edge. */
    uint64_t now = process_watch_now_ms(),
             end = now > UINT64_MAX - 40000 ? UINT64_MAX : now + 40000;
    unsigned long error = 0;
    for (size_t i = 0; i < o->watch_count; i++) {
        if (process_watch_wait(o->watches[i], end, &error) != PROCESS_WATCH_OK) {
            ok = false;
            process_watch_kill(o->watches[i], &error);
            process_watch_wait(o->watches[i], end, &error);
        }
        if (process_watch_close(&o->watches[i], &error) != PROCESS_WATCH_OK)
            ok = false;
    }
    if (o->out.file && fclose(o->out.file))
        ok = false;
    if (o->err.file && fclose(o->err.file))
        ok = false;
    free(o->out.data);
    free(o->err.data);
    free(o->directory);
    free(o->watches);
    free(o);
    return ok;
}

static bool sink(stream_t *s, const unsigned char *data, size_t size) {
    if (size > SIZE_MAX - s->size - 1)
        return false;
    unsigned char *next = realloc(s->data, s->size + size + 1);
    if (!next)
        return false;
    s->data = next;
    memcpy(s->data + s->size, data, size);
    s->size += size;
    s->data[s->size] = 0;
    return fwrite(data, 1, size, s->file) == size && !fflush(s->file);
}

/* Test-only raw transport: no Job Object/subreaper owns the cleaner under test.
 * The production launcher would reap/kill detached cleaners on owner exit and
 * make their lifetime impossible to observe independently. */
#ifdef _WIN32
static void close_native(HANDLE *handle) {
    if (*handle && *handle != INVALID_HANDLE_VALUE)
        CloseHandle(*handle);
    *handle = NULL;
}

static wchar_t *wide_text(loading_owner_t *o, const char *text) {
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    REQUIRE(o, count > 0);
    wchar_t *value = language_owned(o->run, ALLOC((size_t)count * sizeof(*value)));
    REQUIRE(o, MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, value, count) == count);
    return value;
}

static int env_compare(const void *a, const void *b) {
    return _wcsicmp(*(const wchar_t *const *)a, *(const wchar_t *const *)b);
}

static wchar_t *wide_environment(loading_owner_t *o, const char *const *env) {
    size_t count = 0, length = 1;
    while (env[count])
        count++;
    wchar_t **entries = language_owned(o->run, ALLOC((count + 1) * sizeof(*entries)));
    for (size_t i = 0; i < count; i++) {
        entries[i] = wide_text(o, env[i]);
        length += wcslen(entries[i]) + 1;
    }
    qsort(entries, count, sizeof(*entries), env_compare);
    wchar_t *block = language_owned(o->run, ALLOC((length + 1) * sizeof(*block)));
    size_t at = 0;
    for (size_t i = 0; i < count; i++) {
        size_t n = wcslen(entries[i]) + 1;
        memcpy(block + at, entries[i], n * sizeof(*block));
        at += n;
    }
    block[at] = block[at + 1] = 0;
    return block;
}

static void raw_start(loading_owner_t *o, const char *const *args, const char *const *env) {
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE inherited[3] = {NULL, NULL, NULL};
    STARTUPINFOEXW startup = {0};
    PROCESS_INFORMATION process = {0};
    SIZE_T bytes = 0;
    char *quoted = create_windows_command_line(args);
    REQUIRE(o, quoted);
    wchar_t *command = wide_text(o, quoted);
    FREE(quoted);
    wchar_t *environment = wide_environment(o, env);
    wchar_t *executable = wide_text(o, args[0]);
    bool ok = CreatePipe(&inherited[0], &o->input, &sa, 4096)
              && SetHandleInformation(o->input, HANDLE_FLAG_INHERIT, 0)
              && CreatePipe(&o->output[0], &inherited[1], &sa, 4096)
              && SetHandleInformation(o->output[0], HANDLE_FLAG_INHERIT, 0)
              && CreatePipe(&o->output[1], &inherited[2], &sa, 4096)
              && SetHandleInformation(o->output[1], HANDLE_FLAG_INHERIT, 0);
    bool initialized = false;
    if (ok) {
        InitializeProcThreadAttributeList(NULL, 1, 0, &bytes);
        startup.lpAttributeList = malloc(bytes);
        ok = startup.lpAttributeList != NULL;
        if (ok)
            initialized = InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &bytes);
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
                            EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
                            environment,
                            NULL,
                            &startup.StartupInfo,
                            &process);
    }
    unsigned long error = ok ? 0 : GetLastError();
    for (size_t i = 0; i < 3; i++)
        close_native(&inherited[i]);
    if (initialized)
        DeleteProcThreadAttributeList(startup.lpAttributeList);
    free(startup.lpAttributeList);
    if (ok) {
        o->process = process.hProcess;
        CloseHandle(process.hThread);
        o->started = true;
        o->result.status = CHILD_RUNNING;
    } else {
        o->result.system_error = error;
        o->result.status = CHILD_START_ERROR;
    }
    REQUIRE(o, ok);
}

static void drain(loading_owner_t *o, size_t channel) {
    HANDLE *handle = &o->output[channel];
    unsigned char buffer[8192];
    for (unsigned budget = 0; *handle && budget < 64; budget++) {
        DWORD available = 0, count = 0;
        if (!PeekNamedPipe(*handle, NULL, 0, NULL, &available, NULL)) {
            DWORD error = GetLastError();
            if (error != ERROR_BROKEN_PIPE)
                o->result.system_error = error;
            close_native(handle);
            break;
        }
        if (!available)
            break;
        DWORD want = available < sizeof(buffer) ? available : sizeof(buffer);
        if (!ReadFile(*handle, buffer, want, &count, NULL)) {
            o->result.system_error = GetLastError();
            close_native(handle);
            break;
        }
        if (!sink(channel ? &o->err : &o->out, buffer, count)) {
            o->result.system_error = ERROR_WRITE_FAULT;
            break;
        }
    }
}

static const child_result_t *raw_poll(loading_owner_t *o) {
    if (o->input && o->input_offset < o->input_size) {
        DWORD count = 0;
        if (!WriteFile(o->input,
                       "G\n" + o->input_offset,
                       (DWORD)(o->input_size - o->input_offset),
                       &count,
                       NULL))
            o->result.system_error = GetLastError();
        else
            o->input_offset += count;
    }
    if (o->eof && o->input_offset == o->input_size)
        close_native(&o->input);
    drain(o, 0);
    drain(o, 1);
    if (!o->reaped) {
        DWORD state = WaitForSingleObject(o->process, 0);
        if (state == WAIT_OBJECT_0) {
            DWORD code;
            if (!GetExitCodeProcess(o->process, &code))
                o->result.system_error = GetLastError();
            else {
                o->result.exit_code = (int)code;
                o->result.status = CHILD_EXITED;
                o->reaped = true;
            }
            drain(o, 0);
            drain(o, 1);
        } else if (state != WAIT_TIMEOUT)
            o->result.system_error = GetLastError();
        else if (child_monotonic_ms() >= scenario_run_deadline(o->run))
            o->result.status = CHILD_TIMED_OUT;
    }
    return &o->result;
}

static void raw_stop(loading_owner_t *o) {
    if (o->process && !o->reaped) {
        DWORD state = WaitForSingleObject(o->process, 0);
        if (state == WAIT_TIMEOUT) {
            if (!TerminateProcess(o->process, 137)
                && WaitForSingleObject(o->process, 0) != WAIT_OBJECT_0)
                o->result.cleanup_error = GetLastError();
            state = WaitForSingleObject(o->process, 30000);
        }
        if (state != WAIT_OBJECT_0)
            o->result.cleanup_error = state == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
        else {
            o->reaped = true;
            DWORD code;
            if (GetExitCodeProcess(o->process, &code)) {
                o->result.status = CHILD_EXITED;
                o->result.exit_code = (int)code;
            }
            drain(o, 0);
            drain(o, 1);
        }
    }
}

static void raw_close(loading_owner_t *o) {
    close_native(&o->input);
    close_native(&o->output[0]);
    close_native(&o->output[1]);
    close_native(&o->process);
}
#else
static void close_native(int *fd) {
    if (*fd >= 0)
        close(*fd);
    *fd = -1;
}

static void raw_start(loading_owner_t *o, const char *const *args, const char *const *env) {
    int in[2] = {-1, -1}, out[2] = {-1, -1}, err[2] = {-1, -1};
    bool ok = !socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, in) && !pipe2(out, O_CLOEXEC)
              && !pipe2(err, O_CLOEXEC);
    if (ok)
        ok = fcntl(in[1], F_SETFL, O_NONBLOCK) == 0 && fcntl(out[0], F_SETFL, O_NONBLOCK) == 0
             && fcntl(err[0], F_SETFL, O_NONBLOCK) == 0;
    long maximum = sysconf(_SC_OPEN_MAX);
    if (maximum < 0)
        maximum = 65536;
    pid_t pid = ok ? fork() : -1;
    int error = errno;
    if (!pid) {
        if (dup2(in[0], STDIN_FILENO) < 0 || dup2(out[1], STDOUT_FILENO) < 0
            || dup2(err[1], STDERR_FILENO) < 0 || fcntl(STDIN_FILENO, F_SETFD, 0) < 0
            || fcntl(STDOUT_FILENO, F_SETFD, 0) < 0 || fcntl(STDERR_FILENO, F_SETFD, 0) < 0)
            _exit(126);
        for (long fd = 3; fd < maximum; fd++)
            close((int)fd);
        execve(args[0], (char *const *)args, (char *const *)env);
        _exit(127);
    }
    close_native(&in[0]);
    close_native(&out[1]);
    close_native(&err[1]);
    o->input = in[1];
    o->output[0] = out[0];
    o->output[1] = err[0];
    o->pid = pid;
    if (pid > 0) {
        o->started = true;
        o->result.status = CHILD_RUNNING;
    } else {
        o->result.system_error = error;
        o->result.status = CHILD_START_ERROR;
    }
    REQUIRE(o, pid > 0);
}

static void drain(loading_owner_t *o, size_t channel) {
    int *fd = &o->output[channel];
    unsigned char buffer[8192];
    for (unsigned budget = 0; *fd >= 0 && budget < 64; budget++) {
        ssize_t count = read(*fd, buffer, sizeof(buffer));
        if (count > 0) {
            if (!sink(channel ? &o->err : &o->out, buffer, (size_t)count)) {
                o->result.system_error = EIO;
                break;
            }
        } else if (!count) {
            close_native(fd);
            break;
        } else if (errno == EINTR)
            continue;
        else if (errno == EAGAIN || errno == EWOULDBLOCK)
            break;
        else {
            o->result.system_error = errno;
            close_native(fd);
            break;
        }
    }
}

static const child_result_t *raw_poll(loading_owner_t *o) {
    if (o->input >= 0 && o->input_offset < o->input_size) {
        ssize_t count =
            send(o->input, "G\n" + o->input_offset, o->input_size - o->input_offset, MSG_NOSIGNAL);
        if (count > 0)
            o->input_offset += (size_t)count;
        else if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            o->result.system_error = errno;
    }
    if (o->eof && o->input_offset == o->input_size)
        close_native(&o->input);
    drain(o, 0);
    drain(o, 1);
    if (!o->reaped) {
        int status;
        pid_t result = waitpid(o->pid, &status, WNOHANG);
        if (result == o->pid) {
            o->reaped = true;
            if (WIFEXITED(status)) {
                o->result.status = CHILD_EXITED;
                o->result.exit_code = WEXITSTATUS(status);
            } else {
                o->result.status = CHILD_SIGNALED;
                o->result.signal_number = WTERMSIG(status);
            }
            drain(o, 0);
            drain(o, 1);
        } else if (result < 0 && errno != EINTR)
            o->result.system_error = errno;
        else if (child_monotonic_ms() >= scenario_run_deadline(o->run))
            o->result.status = CHILD_TIMED_OUT;
    }
    return &o->result;
}

static void raw_stop(loading_owner_t *o) {
    if (o->pid > 0 && !o->reaped) {
        if (kill(o->pid, SIGKILL) && errno != ESRCH)
            o->result.cleanup_error = errno;
        uint64_t end = process_watch_now_ms() + 30000;
        for (;;) {
            int status;
            pid_t result = waitpid(o->pid, &status, WNOHANG);
            if (result == o->pid) {
                o->reaped = true;
                if (WIFEXITED(status)) {
                    o->result.status = CHILD_EXITED;
                    o->result.exit_code = WEXITSTATUS(status);
                } else {
                    o->result.status = CHILD_SIGNALED;
                    o->result.signal_number = WTERMSIG(status);
                }
                drain(o, 0);
                drain(o, 1);
                break;
            }
            if (result < 0 && errno != EINTR) {
                o->result.cleanup_error = errno;
                break;
            }
            if (process_watch_now_ms() >= end) {
                o->result.cleanup_error = ETIMEDOUT;
                break;
            }
            drain(o, 0);
            drain(o, 1);
            poll(NULL, 0, 1);
        }
    }
}

static void raw_close(loading_owner_t *o) {
    close_native(&o->input);
    close_native(&o->output[0]);
    close_native(&o->output[1]);
}
#endif

static bool environment_name(const char *value, const char *name) {
    size_t n = strlen(name);
#ifdef _WIN32
    return !_strnicmp(value, name, n) && value[n] == '=';
#else
    return !strncmp(value, name, n) && value[n] == '=';
#endif
}

static const char *const *environment(loading_owner_t *o, const char *cleaner_fault) {
    const char *const *base = language_environment_override(o->run, "TMPDIR", o->directory);
    size_t count = 0;
    while (base[count])
        count++;
    const char **values = language_owned(o->run, ALLOC((count + 6) * sizeof(*values)));
    size_t used = 0;
    const char *keys[] = {"TMP",
                          "TEMP",
                          "GOAT_LOADING_MARKERS",
                          "GOAT_LOADING_NATIVE_MARKER",
                          "GOAT_LOADING_CLEANER_FAULT"};
    size_t extras = cleaner_fault ? 5 : 4;
    for (size_t i = 0; i < count; i++) {
        bool replaced = false;
        for (size_t j = 0; j < extras; j++)
            replaced |= environment_name(base[i], keys[j]);
        if (!replaced)
            values[used++] = base[i];
    }
    const char *marker = language_join(o->run, o->directory, "native.calls");
    const char *replacements[] = {o->directory, o->directory, o->directory, marker, cleaner_fault};
    for (size_t i = 0; i < extras; i++) {
        size_t n = strlen(keys[i]) + strlen(replacements[i]) + 2;
        char *entry = language_owned(o->run, ALLOC(n));
        snprintf(entry, n, "%s=%s", keys[i], replacements[i]);
        values[used++] = entry;
    }
    values[used] = NULL;
    return values;
}

loading_owner_t *loading_owner_start(scenario_run_t *r,
                                     const char *runner,
                                     const char *root,
                                     const char *name,
                                     const char *archive,
                                     const char *mode,
                                     const char *fault,
                                     const char *cleaner_fault) {
    loading_owner_t *o = calloc(1, sizeof(*o));
    scenario_require(r, o != NULL, "allocate loading owner");
#ifndef _WIN32
    o->input = o->output[0] = o->output[1] = -1;
#endif
    o->run = r;
    scenario_defer(r, close_owner, o);
    size_t n = strlen(name) + 7;
    char *leaf = language_owned(r, ALLOC(n));
    snprintf(leaf, n, "owner-%s", name);
    const char *directory = language_join(r, root, leaf);
    o->directory = malloc(strlen(directory) + 1);
    REQUIRE(o, o->directory);
    strcpy(o->directory, directory);
    language_directory(r, o->directory);
    o->out.file = FOPEN(language_join(r, o->directory, "stdout.bin"), "wb");
    o->err.file = FOPEN(language_join(r, o->directory, "stderr.bin"), "wb");
    REQUIRE(o, o->out.file && o->err.file);
    const char *args[] = {runner, archive, mode ? mode : "auto", fault ? fault : "run", NULL};
    o->log = FOPEN(language_join(r, o->directory, "command.log"), "wb");
    REQUIRE(o, o->log);
    for (size_t i = 0; args[i]; i++) {
        REQUIRE(o, fprintf(o->log, "argv[%zu]=%s\n", strlen(args[i]), args[i]) >= 0);
    }
    REQUIRE(o,
            fprintf(o->log,
                    "TMPDIR/TMP/TEMP=%s\ncleaner_fault=%s\n",
                    o->directory,
                    cleaner_fault ? cleaner_fault : "(inherited)")
                >= 0);
    REQUIRE(o, !fflush(o->log));
    raw_start(o, args, environment(o, cleaner_fault));
    return o;
}

static const child_result_t *pump(loading_owner_t *o) {
    const child_result_t *result = raw_poll(o);
    REQUIRE(o,
            result->status == CHILD_RUNNING || result->status == CHILD_EXITED
                || result->status == CHILD_SIGNALED);
    REQUIRE(o, !result->system_error);
    REQUIRE(o, !result->cleanup_error);
    return result;
}

static process_watch_result_t line_ready(void *arg, unsigned long *error) {
    (void)error;
    loading_owner_t *o = arg;
    const child_result_t *result = pump(o);
    for (size_t i = o->line_offset; i < o->out.size; i++)
        if (o->out.data[i] == '\n')
            return PROCESS_WATCH_OK;
    return result->status == CHILD_RUNNING ? PROCESS_WATCH_TIMEOUT : PROCESS_WATCH_ERROR;
}

void loading_owner_wait_line(loading_owner_t *o, const char *expected) {
    unsigned long error = 0;
    REQUIRE(o, process_watch_until(line_ready, o, deadline(o, 40000), &error) == PROCESS_WATCH_OK);
    size_t end = o->line_offset;
    while (end < o->out.size && o->out.data[end] != '\n')
        end++;
    size_t size = end - o->line_offset;
    while (size
           && (o->out.data[o->line_offset + size - 1] == '\r'
               || o->out.data[o->line_offset + size - 1] == '\n'))
        size--;
    scenario_require_bytes(o->run,
                           "loading owner line",
                           expected,
                           strlen(expected),
                           o->out.data + o->line_offset,
                           size,
                           false);
    o->line_offset = end + 1;
}

void loading_owner_resume(loading_owner_t *o) {
    REQUIRE(o, !o->eof && !o->input_size);
    o->input_size = 2;
    pump(o);
}

static process_watch_result_t exited(void *arg, unsigned long *error) {
    (void)error;
    loading_owner_t *o = arg;
    const child_result_t *result = pump(o);
#ifdef _WIN32
    bool drained = !o->output[0] && !o->output[1];
#else
    bool drained = o->output[0] < 0 && o->output[1] < 0;
#endif
    return result->status == CHILD_RUNNING || !drained ? PROCESS_WATCH_TIMEOUT : PROCESS_WATCH_OK;
}

static void finish_transport(loading_owner_t *o) {
    o->eof = true;
    unsigned long error = 0;
    REQUIRE(o, process_watch_until(exited, o, deadline(o, 120000), &error) == PROCESS_WATCH_OK);
    o->ended = true;
}

loading_bytes_t loading_owner_output(loading_owner_t *o) {
    return (loading_bytes_t){o->out.data, o->out.size};
}

loading_bytes_t loading_owner_error(loading_owner_t *o) {
    return (loading_bytes_t){o->err.data, o->err.size};
}

const char *loading_owner_directory(loading_owner_t *o) {
    return o->directory;
}

void loading_owner_expect_output(loading_owner_t *o, const char *expected) {
    scenario_require_bytes(o->run,
                           "complete loading owner stdout",
                           expected,
                           strlen(expected),
                           o->out.data,
                           o->out.size,
                           true);
}

static bool missing(loading_owner_t *o, const char *path) {
    fs_info_t info;
    fs_error_t error = {0};
    bool exists = inspect_fs_path(path, true, &info, &error), absent = error.status == FS_NOT_FOUND;
    if (!exists && !absent) {
        fprintf(stderr,
                "loading owner path inspection failed: %s (status %d, native %lu): %s\n",
                path,
                (int)error.status,
                error.native_code,
                error.operation ? error.operation : "unknown operation");
        fflush(stderr);
    }
    clear_fs_error(&error);
    REQUIRE(o, exists || absent);
    return !exists && absent;
}

void loading_owner_absent(loading_owner_t *o, const char *path) {
    REQUIRE(o, missing(o, path));
}

typedef struct {
    loading_owner_t *owner;
    const char *path, *expected;
    char *value;
} condition_t;

static process_watch_result_t absent_ready(void *arg, unsigned long *error) {
    condition_t *c = arg;
    pump(c->owner);
    fs_info_t info;
    fs_error_t probe = {0};
    bool exists = inspect_fs_path(c->path, true, &info, &probe);
    process_watch_result_t result = PROCESS_WATCH_ERROR;
    if (exists)
        result = PROCESS_WATCH_TIMEOUT;
    else if (probe.status == FS_NOT_FOUND)
        result = PROCESS_WATCH_OK;
#ifdef _WIN32
    /* A delete-pending directory can reject CreateFile(FILE_READ_ATTRIBUTES)
     * while the cleaner/another observer still holds its last handle. This is
     * not proof of absence: keep polling within the original deadline until
     * NOT_FOUND. Persistent access denial must still fail the bounded wait. */
    else if (probe.native_code == ERROR_ACCESS_DENIED
             || probe.native_code == ERROR_SHARING_VIOLATION)
        result = PROCESS_WATCH_TIMEOUT;
#endif
    if (error)
        *error = probe.native_code;
    clear_fs_error(&probe);
    return result;
}

void loading_owner_wait_absent(loading_owner_t *o, const char *path) {
    condition_t c = {o, path, NULL, NULL};
    unsigned long error = 0;
    process_watch_result_t result =
        process_watch_until(absent_ready, &c, deadline(o, 40000), &error);
    if (result != PROCESS_WATCH_OK) {
        fprintf(stderr,
                "loading owner absence wait failed: %s (result %d, native %lu)\n",
                path,
                (int)result,
                error);
        fflush(stderr);
    }
    REQUIRE(o, result == PROCESS_WATCH_OK);
}

static process_watch_result_t mark_ready(void *arg, unsigned long *error) {
    (void)error;
    condition_t *c = arg;
    loading_owner_t *o = c->owner;
    pump(o);
    if (missing(o, c->path))
        return PROCESS_WATCH_TIMEOUT;
    size_t size;
    const char *bytes = language_read(o->run, c->path, &size);
    size_t first = 0, last = size;
    while (first < last && isspace((unsigned char)bytes[first]))
        first++;
    while (last > first && isspace((unsigned char)bytes[last - 1]))
        last--;
    if (last == first)
        return PROCESS_WATCH_TIMEOUT;
    c->value = language_owned(o->run, ALLOC(last - first + 1));
    memcpy(c->value, bytes + first, last - first);
    c->value[last - first] = 0;
    REQUIRE(o, strlen(c->value) == last - first);
    return PROCESS_WATCH_OK;
}

const char *loading_owner_mark(loading_owner_t *o, const char *name) {
    condition_t c = {o, language_join(o->run, o->directory, name), NULL, NULL};
    unsigned long error = 0;
    REQUIRE(o, process_watch_until(mark_ready, &c, deadline(o, 40000), &error) == PROCESS_WATCH_OK);
    return c.value;
}

loading_bytes_t loading_owner_calls(loading_owner_t *o) {
    const char *path = language_join(o->run, o->directory, "native.calls");
    if (missing(o, path))
        return (loading_bytes_t){"", 0};
    size_t size;
    const char *data = language_read(o->run, path, &size);
    return (loading_bytes_t){data, size};
}

void loading_owner_expect_calls(loading_owner_t *o, const char *expected) {
    loading_bytes_t calls = loading_owner_calls(o);
    scenario_require_bytes(o->run,
                           "loading native calls",
                           expected,
                           strlen(expected),
                           calls.data,
                           calls.size,
                           false);
}

static process_watch_result_t calls_ready(void *arg, unsigned long *error) {
    (void)error;
    condition_t *c = arg;
    pump(c->owner);
    loading_bytes_t calls = loading_owner_calls(c->owner);
    return calls.size == strlen(c->expected) && !memcmp(calls.data, c->expected, calls.size)
               ? PROCESS_WATCH_OK
               : PROCESS_WATCH_TIMEOUT;
}

void loading_owner_wait_calls(loading_owner_t *o, const char *expected) {
    condition_t c = {o, NULL, expected, NULL};
    unsigned long error = 0;
    REQUIRE(o,
            process_watch_until(calls_ready, &c, deadline(o, 40000), &error) == PROCESS_WATCH_OK);
}

void loading_owner_require_directory(loading_owner_t *o, const char *path, bool nonempty) {
    fs_error_t error = {0};
    fs_info_t info;
    bool ok = inspect_fs_path(path, true, &info, &error);
    clear_fs_error(&error);
    REQUIRE(o, ok && info.kind == FS_DIRECTORY);
    if (nonempty) {
        fs_entries_t list = {0};
        ok = list_fs_directory(path, &list, &error);
        clear_fs_error(&error);
        size_t count = list.count;
        clear_fs_entries(&list);
        REQUIRE(o, ok && count);
    }
}

static process_watch_result_t directory_ready(void *arg, unsigned long *error) {
    (void)error;
    condition_t *c = arg;
    loading_owner_t *o = c->owner;
    pump(o);
    fs_entries_t entries = {0};
    fs_error_t e = {0};
    bool ok = list_fs_directory(o->directory, &entries, &e);
    clear_fs_error(&e);
    REQUIRE(o, ok);
    char *found = NULL;
    for (size_t i = 0; i < entries.count; i++)
        if (!strncmp(entries.names[i], "goat-run-", 9)) {
            found = join_fs_path(o->directory, entries.names[i], &e);
            break;
        }
    clear_fs_entries(&entries);
    clear_fs_error(&e);
    if (!found)
        return PROCESS_WATCH_TIMEOUT;
    c->value = language_owned(o->run, found);
    loading_owner_require_directory(o, found, true);
    return PROCESS_WATCH_OK;
}

const char *loading_owner_extraction(loading_owner_t *o) {
    condition_t c = {o, NULL, NULL, NULL};
    unsigned long error = 0;
    REQUIRE(o,
            process_watch_until(directory_ready, &c, deadline(o, 40000), &error)
                == PROCESS_WATCH_OK);
    return c.value;
}

static process_watch_t *watch(loading_owner_t *o, const char *marker) {
    const char *pid = loading_owner_mark(o, marker);
    char *end;
    unsigned long long number = strtoull(pid, &end, 10);
    REQUIRE(o, *pid && !*end && number);
    unsigned long error = 0;
    process_watch_t *w = process_watch_open((uint64_t)number, &error);
    REQUIRE(o, w);
    process_watch_t **next = realloc(o->watches, (o->watch_count + 1) * sizeof(*next));
    if (!next) {
        process_watch_close(&w, &error);
        REQUIRE(o, next);
    }
    o->watches = next;
    o->watches[o->watch_count++] = w;
    return w;
}

process_watch_t *loading_owner_watch_cleaner(loading_owner_t *o) {
    return watch(o, "cleaner.pid");
}

process_watch_t *loading_owner_watch_process(loading_owner_t *o) {
    return watch(o, "owner.pid");
}

void loading_owner_watch_alive(loading_owner_t *o, process_watch_t *w) {
    unsigned long error = 0;
    bool alive = false;
    REQUIRE(o, process_watch_alive(w, &alive, &error) == PROCESS_WATCH_OK && alive);
}

void loading_owner_watch_kill(loading_owner_t *o, process_watch_t *w) {
    unsigned long error = 0;
    REQUIRE(o, process_watch_kill(w, &error) == PROCESS_WATCH_OK);
}

void loading_owner_watch_wait(loading_owner_t *o, process_watch_t *w) {
    unsigned long error = 0;
    REQUIRE(o, process_watch_wait(w, deadline(o, 40000), &error) == PROCESS_WATCH_OK);
}

void loading_owner_kill_and_reap(loading_owner_t *o) {
    process_watch_t *w = loading_owner_watch_process(o);
    loading_owner_watch_alive(o, w);
    loading_owner_watch_kill(o, w);
    loading_owner_watch_wait(o, w);
    finish_transport(o); /* No production process state is accepted as proof of death. */
}

yyjson_val *loading_owner_end(loading_owner_t *o, int code, bool cleanup, bool explicit_exit) {
    finish_transport(o);
    const child_result_t *result = pump(o);
    REQUIRE(o, result->status == CHILD_EXITED && result->exit_code == code);
    size_t count = 0;
    yyjson_val *record = NULL;
    for (size_t offset = 0; offset < o->err.size;) {
        size_t end = offset;
        while (end < o->err.size && o->err.data[end] != '\n')
            end++;
        size_t size = end - offset;
        while (size && o->err.data[offset + size - 1] == '\r')
            size--;
        if (size >= 7 && !memcmp(o->err.data + offset, "RESULT ", 7)) {
            yyjson_doc *doc = yyjson_read((const char *)o->err.data + offset + 7, size - 7, 0);
            REQUIRE(o, doc);
            scenario_defer(o->run, free_doc, doc);
            record = yyjson_doc_get_root(doc);
            REQUIRE(o, yyjson_is_obj(record));
            count++;
        }
        offset = end < o->err.size ? end + 1 : end;
    }
    if (explicit_exit) {
        REQUIRE(o, !count && !o->err.size);
        return NULL;
    }
    REQUIRE(o, count == 1);
    if (cleanup) {
        yyjson_val *directory = yyjson_obj_get(record, "directory");
        if (yyjson_is_str(directory) && yyjson_get_len(directory)) {
            REQUIRE(o, strlen(yyjson_get_str(directory)) == yyjson_get_len(directory));
            loading_owner_wait_absent(o, yyjson_get_str(directory));
            loading_owner_mark(o, "cleaner.done");
        }
    }
    return record;
}
