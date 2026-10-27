/** @file child_process_windows.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Atomic Job Object membership before any child code, restricted handle inheritance.
 */
#include "lib/platform/windows_target.h"
#include "lib/process/child_process_internal.h"
#ifdef _WIN32
#    include "lib/memory/allocate.h"
#    include "lib/process/windows_command_line.h"

#    include <stdio.h>
#    include <string.h>
#    include <wchar.h>
#    include <windows.h>
#    ifndef PROC_THREAD_ATTRIBUTE_JOB_LIST
#        define PROC_THREAD_ATTRIBUTE_JOB_LIST ((DWORD_PTR)0x0002000D)
#    endif

struct child_process_t {
    child_result_t result;
    child_deadline_t deadline;
    HANDLE process, job, pipes[3];
    HANDLE input;
    OVERLAPPED input_write;
    unsigned char *input_data;
    size_t input_size, input_offset;
    bool input_pending;
    child_input_callback_t stdin_callback;
    child_output_callback_t output_callbacks[2];
    void *callback_context;
    bool capture_output, callback_failed;
    bool done, service;
};

uint64_t child_monotonic_ms(void) {
    return GetTickCount64();
}

static void close_handle(HANDLE *handle) {
    if (*handle && *handle != INVALID_HANDLE_VALUE)
        CloseHandle(*handle);
    *handle = NULL;
}

static wchar_t *wide(const char *text) {
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (!count)
        return NULL;
    wchar_t *result = ALLOC((size_t)count * sizeof(*result));
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, result, count)) {
        FREE(result);
        return NULL;
    }
    return result;
}

/* Full replacement environment; Windows names are case-insensitive. */
static wchar_t *environment_block(const char *const *environment) {
    size_t count = 0, size = 1;
    while (environment[count])
        count++;
    wchar_t **entries =
        CALLOC(allocation_size_multiply(allocation_size_add(count, 1), sizeof(*entries)));
    DWORD error = 0;
    for (size_t i = 0; i < count && !error; i++) {
        entries[i] = wide(environment[i]);
        if (!entries[i]) {
            error = GetLastError();
            break;
        }
        size = allocation_size_add(size, allocation_size_add(wcslen(entries[i]), 1));
        const wchar_t *equals = wcschr(entries[i], L'=');
        size_t name_size = (size_t)(equals - entries[i]);
        for (size_t j = 0; j < i; j++) {
            const wchar_t *other = wcschr(entries[j], L'=');
            if ((size_t)(other - entries[j]) == name_size
                && CompareStringOrdinal(entries[i],
                                        (int)name_size,
                                        entries[j],
                                        (int)name_size,
                                        TRUE)
                       == CSTR_EQUAL) {
                error = ERROR_INVALID_PARAMETER;
                break;
            }
        }
    }
    wchar_t *block = NULL;
    if (!error) {
        /* Insertion sort avoids a locale-dependent comparison callback. */
        for (size_t i = 1; i < count; i++) {
            wchar_t *entry = entries[i];
            size_t j = i;
            while (j
                   && CompareStringOrdinal(entries[j - 1],
                                           (int)(wcschr(entries[j - 1], L'=') - entries[j - 1]),
                                           entry,
                                           (int)(wcschr(entry, L'=') - entry),
                                           TRUE)
                          == CSTR_GREATER_THAN) {
                entries[j] = entries[j - 1];
                j--;
            }
            entries[j] = entry;
        }
        block = CALLOC(allocation_size_multiply(allocation_size_add(size, 1), sizeof(*block)));
        wchar_t *next = block;
        for (size_t i = 0; i < count; i++) {
            size_t length = wcslen(entries[i]) + 1;
            memcpy(next, entries[i], length * sizeof(*next));
            next += length;
        }
    }
    for (size_t i = 0; i < count; i++)
        FREE(entries[i]);
    FREE(entries);
    if (error)
        SetLastError(error);
    return block;
}

static void close_input(child_process_t *p) {
    if (p->input_pending) {
        CancelIoEx(p->input, &p->input_write);
        DWORD ignored;
        /* Cancellation must complete before freeing the buffer/OVERLAPPED. */
        GetOverlappedResult(p->input, &p->input_write, &ignored, TRUE);
        p->input_pending = false;
    }
    close_handle(&p->input);
    close_handle(&p->input_write.hEvent);
    FREE(p->input_data);
    p->input_data = NULL;
}

static DWORD create_input(child_process_t *p,
                          HANDLE *reader,
                          SECURITY_ATTRIBUTES *sa,
                          const child_options_t *options) {
    static LONG sequence;
    wchar_t name[128];
    swprintf(name,
             sizeof(name) / sizeof(*name),
             L"\\\\.\\pipe\\goat-input-%lu-%lu",
             (unsigned long)GetCurrentProcessId(),
             (unsigned long)InterlockedIncrement(&sequence));
    *reader = CreateNamedPipeW(name,
                               PIPE_ACCESS_INBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE,
                               PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT
                                   | PIPE_REJECT_REMOTE_CLIENTS,
                               1,
                               4096,
                               4096,
                               0,
                               sa);
    if (*reader == INVALID_HANDLE_VALUE)
        return GetLastError();
    p->input = CreateFileW(name, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (p->input == INVALID_HANDLE_VALUE)
        return GetLastError();
    /* The client is already connected: this cannot wait for another connection. */
    if (!ConnectNamedPipe(*reader, NULL) && GetLastError() != ERROR_PIPE_CONNECTED)
        return GetLastError();
    p->input_write.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!p->input_write.hEvent)
        return GetLastError();
    p->input_size = options->stdin_size;
    if (p->stdin_callback) {
        p->input_data = ALLOC(4096);
    } else if (p->input_size) {
        p->input_data = ALLOC(p->input_size);
        memcpy(p->input_data, options->stdin_data, p->input_size);
    } else {
        close_input(p);
    }
    return 0;
}

/* At most one bounded async write per turn; drain output and check deadlines between turns. */
static void pump_input(child_process_t *p) {
    if (!p->input || p->input == INVALID_HANDLE_VALUE)
        return;
    DWORD written = 0, error = 0;
    if (p->input_pending) {
        if (!GetOverlappedResult(p->input, &p->input_write, &written, FALSE)) {
            error = GetLastError();
            if (error == ERROR_IO_INCOMPLETE)
                return;
        }
        p->input_pending = false;
    } else {
        if (p->stdin_callback && p->input_offset == p->input_size) {
            size_t size = 0;
            child_input_status_t state =
                p->stdin_callback(p->callback_context, p->input_data, 4096, &size);
            if (state == CHILD_INPUT_WAIT && !size)
                return;
            if (state == CHILD_INPUT_EOF && !size) {
                close_input(p);
                return;
            }
            if (state != CHILD_INPUT_DATA || !size || size > 4096) {
                p->callback_failed = true;
                p->result.system_error = ERROR_INVALID_DATA;
                close_input(p);
                return;
            }
            p->input_size = size;
            p->input_offset = 0;
        }
        size_t remaining = p->input_size - p->input_offset;
        ResetEvent(p->input_write.hEvent);
        if (!WriteFile(p->input,
                       p->input_data + p->input_offset,
                       remaining > 4096 ? 4096 : (DWORD)remaining,
                       &written,
                       &p->input_write)) {
            error = GetLastError();
            if (error == ERROR_IO_PENDING) {
                p->input_pending = true;
                return;
            }
        }
    }
    if (error) {
        if (error != ERROR_BROKEN_PIPE && error != ERROR_NO_DATA) {
            p->result.system_error = error;
            p->callback_failed = true;
        }
        close_input(p);
        return;
    }
    p->input_offset += written;
    if (p->input_offset == p->input_size && !p->stdin_callback)
        close_input(p);
}

static wchar_t *executable_path(const char *name) {
    if (!name) {
        wchar_t *path = ALLOC(32768 * sizeof(*path));
        DWORD n = GetModuleFileNameW(NULL, path, 32768);
        if (n && n < 32768)
            return path;
        FREE(path);
        return NULL;
    }
    wchar_t *input = wide(name);
    if (!input)
        return NULL;
    DWORD n = SearchPathW(NULL, input, L".exe", 0, NULL, NULL);
    wchar_t *path = NULL;
    if (n) {
        path = ALLOC(((size_t)n + 1) * sizeof(*path));
        DWORD actual = SearchPathW(NULL, input, L".exe", n + 1, path, NULL);
        if (!actual || actual > n) {
            FREE(path);
            path = NULL;
        }
    }
    FREE(input);
    return path;
}

child_process_t *start_child_process(const child_options_t *options) {
    if (!child_options_valid(options))
        return NULL;
    child_process_t *p = CALLOC(sizeof(*p));
    p->result.exit_code = -1;
    p->deadline = options->deadline;
    p->service = options->survive_parent;
    p->stdin_callback = options->stdin_callback;
    p->output_callbacks[0] = options->stdout_callback;
    p->output_callbacks[1] = options->stderr_callback;
    p->callback_context = options->callback_context;
    p->capture_output = options->capture_output;
    HANDLE inherited[6] = {0};
    size_t count = 3;
    SECURITY_ATTRIBUTES sa = {.nLength = sizeof(sa), .bInheritHandle = TRUE};
    DWORD error = 0;
    wchar_t *executable = executable_path(options->executable), *command = NULL;
    if (!executable)
        error = GetLastError();
    wchar_t *directory = options->working_directory ? wide(options->working_directory) : NULL;
    if (options->working_directory && !directory)
        error = GetLastError();
    wchar_t *environment = options->environment ? environment_block(options->environment) : NULL;
    if (options->environment && !environment)
        error = GetLastError();
    STARTUPINFOEXW startup = {0};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    bool initialized = false;
    for (int i = 0; i < 3 && !error; i++) {
        if (!i && (options->stdin_data || options->stdin_callback)) {
            error = create_input(p, &inherited[i], &sa, options);
        } else if (i && (options->capture_output || p->output_callbacks[i - 1])) {
            if (!CreatePipe(&p->pipes[i - 1], &inherited[i], &sa, 4096)
                || !SetHandleInformation(p->pipes[i - 1], HANDLE_FLAG_INHERIT, 0))
                error = GetLastError();
        } else {
            HANDLE original = GetStdHandle(i == 0   ? STD_INPUT_HANDLE
                                           : i == 1 ? STD_OUTPUT_HANDLE
                                                    : STD_ERROR_HANDLE);
            if ((!i && options->empty_stdin) || !original || original == INVALID_HANDLE_VALUE) {
                inherited[i] = CreateFileW(L"NUL",
                                           i ? GENERIC_WRITE : GENERIC_READ,
                                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                                           &sa,
                                           OPEN_EXISTING,
                                           0,
                                           NULL);
                if (inherited[i] == INVALID_HANDLE_VALUE)
                    error = GetLastError();
            } else if (!DuplicateHandle(GetCurrentProcess(),
                                        original,
                                        GetCurrentProcess(),
                                        &inherited[i],
                                        0,
                                        TRUE,
                                        DUPLICATE_SAME_ACCESS))
                error = GetLastError();
        }
    }
    HANDLE result_handle = NULL, parent_handle = NULL;
    if (!error && options->result_channel) {
        if (!CreatePipe(&p->pipes[2], &inherited[count], &sa, 4096)
            || !SetHandleInformation(p->pipes[2], HANDLE_FLAG_INHERIT, 0))
            error = GetLastError();
        result_handle = inherited[count++];
    }
    if (!error && options->parent_watch) {
        if (!DuplicateHandle(GetCurrentProcess(),
                             GetCurrentProcess(),
                             GetCurrentProcess(),
                             &inherited[count],
                             SYNCHRONIZE,
                             TRUE,
                             0))
            error = GetLastError();
        parent_handle = inherited[count++];
    }
    HANDLE service_handle = NULL;
    if (!error && options->service_channel) {
        if (!DuplicateHandle(GetCurrentProcess(),
                             (HANDLE)options->service_channel,
                             GetCurrentProcess(),
                             &inherited[count],
                             0,
                             TRUE,
                             DUPLICATE_SAME_ACCESS))
            error = GetLastError();
        service_handle = inherited[count++];
    }
    if (!error) {
        size_t argc = 0;
        while (options->arguments[argc])
            argc++;
        const char **args = ALLOC((argc + 1) * sizeof(*args));
        char result_arg[32], parent_arg[32], service_arg[32];
        snprintf(service_arg,
                 sizeof(service_arg),
                 "%llu",
                 (unsigned long long)(uintptr_t)service_handle);
        snprintf(result_arg,
                 sizeof(result_arg),
                 "%llu",
                 (unsigned long long)(uintptr_t)result_handle);
        snprintf(parent_arg,
                 sizeof(parent_arg),
                 "%llu",
                 (unsigned long long)(uintptr_t)parent_handle);
        for (size_t i = 0; i <= argc; i++) {
            const char *arg = options->arguments[i];
            if (arg && options->result_channel && !strcmp(arg, CHILD_RESULT_ARGUMENT))
                arg = result_arg;
            if (arg && options->parent_watch && !strcmp(arg, CHILD_PARENT_ARGUMENT))
                arg = parent_arg;
            if (arg && options->service_channel && !strcmp(arg, CHILD_SERVICE_ARGUMENT))
                arg = service_arg;
            args[i] = arg;
        }
        char *quoted = create_windows_command_line(args);
        if (!quoted)
            error = ERROR_BAD_LENGTH;
        else {
            command = wide(quoted);
            if (!command)
                error = GetLastError();
        }
        FREE(quoted);
        FREE(args);
    }
    if (!error) {
        p->job = CreateJobObjectW(NULL, NULL);
        if (!p->job)
            error = GetLastError();
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {0};
    limits.BasicLimitInformation.LimitFlags =
        options->survive_parent ? 0 : JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!error
        && !SetInformationJobObject(p->job,
                                    JobObjectExtendedLimitInformation,
                                    &limits,
                                    sizeof(limits)))
        error = GetLastError();
    SIZE_T bytes = 0;
    if (!error) {
        InitializeProcThreadAttributeList(NULL, 2, 0, &bytes);
        startup.lpAttributeList = ALLOC(bytes);
        initialized = InitializeProcThreadAttributeList(startup.lpAttributeList, 2, 0, &bytes);
        if (!initialized)
            error = GetLastError();
    }
    if (!error
        && (!UpdateProcThreadAttribute(startup.lpAttributeList,
                                       0,
                                       PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                       inherited,
                                       count * sizeof(HANDLE),
                                       NULL,
                                       NULL)
            || !UpdateProcThreadAttribute(startup.lpAttributeList,
                                          0,
                                          PROC_THREAD_ATTRIBUTE_JOB_LIST,
                                          &p->job,
                                          sizeof(p->job),
                                          NULL,
                                          NULL)))
        error = GetLastError();
    startup.StartupInfo.hStdInput = inherited[0];
    startup.StartupInfo.hStdOutput = inherited[1];
    startup.StartupInfo.hStdError = inherited[2];
    PROCESS_INFORMATION info = {0};
    if (!error) {
        if (p->deadline && child_monotonic_ms() >= p->deadline) {
            p->result.status = CHILD_TIMED_OUT;
            p->done = true;
        } else if (!CreateProcessW(executable,
                                   command,
                                   NULL,
                                   NULL,
                                   TRUE,
                                   EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW
                                       | CREATE_UNICODE_ENVIRONMENT,
                                   environment,
                                   directory,
                                   &startup.StartupInfo,
                                   &info))
            error = GetLastError();
        else
            p->process = info.hProcess;
    }
    close_handle(&info.hThread);
    if (initialized)
        DeleteProcThreadAttributeList(startup.lpAttributeList);
    FREE(startup.lpAttributeList);
    for (size_t i = 0; i < count; i++)
        close_handle(&inherited[i]);
    FREE(environment);
    FREE(directory);
    FREE(executable);
    FREE(command);
    if (error) {
        p->result.status = CHILD_START_ERROR;
        p->result.system_error = error;
        p->done = true;
    }
    if (p->done)
        close_input(p);
    return p;
}

static bool drain(child_process_t *p) {
    bool progress = false;
    child_output_t *streams[] = {&p->result.out, &p->result.err, &p->result.result};
    for (int i = 0; i < 3; i++) {
        for (int turn = 0; p->pipes[i] && turn < 16; turn++) {
            DWORD available = 0;
            if (!PeekNamedPipe(p->pipes[i], NULL, 0, NULL, &available, NULL)) {
                DWORD error = GetLastError();
                if (error != ERROR_BROKEN_PIPE) {
                    p->result.system_error = error;
                    p->callback_failed = true;
                }
                close_handle(&p->pipes[i]);
                break;
            }
            if (!available)
                break;
            unsigned char data[4096];
            DWORD received = 0;
            if (!ReadFile(p->pipes[i],
                          data,
                          available < sizeof(data) ? available : sizeof(data),
                          &received,
                          NULL)) {
                p->result.system_error = GetLastError();
                p->callback_failed = true;
                close_handle(&p->pipes[i]);
                break;
            }
            progress |= received != 0;
            if (i == 2 || p->capture_output)
                child_capture(streams[i], data, received);
            if (i < 2 && received && p->output_callbacks[i] && !p->callback_failed
                && !p->output_callbacks[i](p->callback_context, data, received)) {
                p->callback_failed = true;
                p->result.system_error = ERROR_WRITE_FAULT;
            }
        }
    }
    return progress;
}

static const child_result_t *wait_process(child_process_t *p, bool stop, bool polling) {
    if (p->done)
        return &p->result;
    for (;;) {
        if (stop || (p->deadline && child_monotonic_ms() >= p->deadline)) {
            p->result.status = stop ? CHILD_STOPPED : CHILD_TIMED_OUT;
            break;
        }
        drain(p);
        if (!p->callback_failed)
            pump_input(p);
        if (p->callback_failed) {
            p->result.status = CHILD_IO_ERROR;
            break;
        }
        DWORD state = WaitForSingleObject(p->process, 0);
        if (state == WAIT_OBJECT_0) {
            DWORD code;
            if (GetExitCodeProcess(p->process, &code)) {
                p->result.exit_code = code;
                p->result.status = CHILD_EXITED;
            } else {
                p->result.system_error = GetLastError();
                p->result.status = CHILD_IO_ERROR;
            }
            if (p->deadline && child_monotonic_ms() >= p->deadline)
                p->result.status = CHILD_TIMED_OUT;
            break;
        }
        if (state == WAIT_FAILED) {
            p->result.system_error = GetLastError();
            p->result.status = CHILD_IO_ERROR;
            break;
        }
        if (polling)
            return &p->result;
        Sleep(1);
    }
    close_input(p);
    /* Even a successful root may have left descendants holding pipes or DLLs. */
    if (!TerminateJobObject(p->job, 1))
        p->result.cleanup_error = GetLastError();
    uint64_t end = child_monotonic_ms() + 5000;
    bool tree_gone = false;
    for (;;) {
        drain(p);
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting;
        if (!QueryInformationJobObject(p->job,
                                       JobObjectBasicAccountingInformation,
                                       &accounting,
                                       sizeof(accounting),
                                       NULL)) {
            p->result.cleanup_error = GetLastError();
            break;
        }
        if (!accounting.ActiveProcesses) {
            tree_gone = true;
            break;
        }
        if (child_monotonic_ms() >= end) {
            p->result.cleanup_error = WAIT_TIMEOUT;
            break;
        }
        Sleep(1);
    }
    /* Writers are gone; consume the complete finite buffered tail, not merely one turn. */
    if (tree_gone) {
        while (drain(p)) {
        }
    } else {
        drain(p);
    }
    if (p->callback_failed)
        p->result.status = CHILD_IO_ERROR;
    p->done = true;
    return &p->result;
}

const child_result_t *wait_child_process(child_process_t *p) {
    return wait_process(p, false, false);
}

const child_result_t *stop_child_process(child_process_t *p) {
    return wait_process(p, true, false);
}

const child_result_t *poll_child_process(child_process_t *p) {
    return wait_process(p, false, true);
}

void release_child_service(child_process_t *p) {
    if (!p || !p->service)
        return;
    close_input(p);
    close_handle(&p->process);
    close_handle(&p->job);
    for (int i = 0; i < 3; i++)
        close_handle(&p->pipes[i]);
    FREE(p);
}

void destroy_child_process(child_process_t *p) {
    if (!p)
        return;
    stop_child_process(p);
    close_input(p);
    close_handle(&p->process);
    close_handle(&p->job);
    for (int i = 0; i < 3; i++)
        close_handle(&p->pipes[i]);
    FREE(p);
}

bool child_write_result(uintptr_t handle, const void *data, size_t size) {
    const unsigned char *p = data;
    while (size) {
        DWORD written;
        if (!WriteFile((HANDLE)handle, p, size > 4096 ? 4096 : (DWORD)size, &written, NULL)
            || !written)
            return false;
        p += written;
        size -= written;
    }
    return true;
}

bool child_wait_parent(uintptr_t handle, child_deadline_t deadline) {
    for (;;) {
        if (deadline && child_monotonic_ms() >= deadline)
            return false;
        DWORD state = WaitForSingleObject((HANDLE)handle, deadline ? 5 : INFINITE);
        if (state == WAIT_OBJECT_0)
            return true;
        if (state == WAIT_FAILED)
            return false;
    }
}

void child_close_channel(uintptr_t handle) {
    CloseHandle((HANDLE)handle);
}
#endif
