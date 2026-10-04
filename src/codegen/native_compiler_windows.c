/** @file native_compiler_windows.c
 * @copyright 2026 Ivan Kniazkov
 * @brief MinGW DLL compilation without a shell; failed builds preserve the previous DLL.
 */
#include "lib/windows_target.h"

#ifdef _WIN32
#    include <windows.h>
#endif
#include "native_compiler.h"

#ifdef _WIN32
#    include "lib/allocate.h"
#    include "lib/io.h"
#    include "lib/windows_command_line.h"

#    include <bcrypt.h>
#    include <errno.h>
#    include <stdio.h>
#    include <string.h>

static char *join(const char *a, const char *b) {
    size_t n = strlen(a), m = strlen(b);
    char *text = ALLOC(n + m + 1);
    memcpy(text, a, n);
    memcpy(text + n, b, m + 1);
    return text;
}

/** @brief Creates an exclusive sibling directory; its ACL is inherited from the parent. */
static char *create_directory(const char *target) {
    char *directory = ALLOC(strlen(target) + 40);
    for (unsigned attempt = 0; attempt < 32; attempt++) {
        uint32_t random[4];
        if (BCryptGenRandom(NULL,
                            (PUCHAR)random,
                            sizeof(random),
                            BCRYPT_USE_SYSTEM_PREFERRED_RNG)) {
            SetLastError(ERROR_GEN_FAILURE);
            break;
        }
        sprintf(directory,
                "%s.goat-%08x%08x%08x%08x",
                target,
                (unsigned)random[0],
                (unsigned)random[1],
                (unsigned)random[2],
                (unsigned)random[3]);
        if (CreateDirectoryA(directory, NULL))
            return directory;
        if (GetLastError() != ERROR_ALREADY_EXISTS)
            break;
    }
    DWORD error = GetLastError();
    FREE(directory);
    SetLastError(error);
    return NULL;
}

/** @brief Starts one executable with only the redirected standard handles inherited. */
static DWORD run_compiler(const char *compiler,
                          const char *source,
                          const char *library,
                          const char *log,
                          int64_t *exit_code) {
    DWORD size = SearchPathA(NULL, compiler, ".exe", 0, NULL, NULL);
    if (!size)
        return GetLastError();
    char *executable = ALLOC(size + 1);
    DWORD length = SearchPathA(NULL, compiler, ".exe", size + 1, executable, NULL);
    if (!length || length > size) {
        DWORD error = length ? ERROR_INSUFFICIENT_BUFFER : GetLastError();
        FREE(executable);
        return error;
    }
    const char *args[] = {executable,
                          "-std=c11",
                          "-O2",
                          "-shared",
                          "-DGOAT_NATIVE_BUILD",
#    ifdef _WIN64
                          "-m64",
#    else
                          "-m32",
#    endif
                          "-fno-fast-math",
                          "-ffp-contract=off",
                          "-Wl,--no-undefined",
                          "-Wl,--exclude-all-symbols",
                          "-o",
                          library,
                          source,
                          "-lm",
                          NULL};
    char *command = create_windows_command_line(args);
    if (!command) {
        FREE(executable);
        return ERROR_BAD_LENGTH;
    }
    SECURITY_ATTRIBUTES security = {.nLength = sizeof(security), .bInheritHandle = TRUE};
    HANDLE input = CreateFileA("NUL",
                               GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &security,
                               OPEN_EXISTING,
                               0,
                               NULL);
    HANDLE output = INVALID_HANDLE_VALUE;
    DWORD error = input == INVALID_HANDLE_VALUE ? GetLastError() : 0;
    if (!error) {
        output =
            CreateFileA(log, GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_ALWAYS, 0, NULL);
        if (output == INVALID_HANDLE_VALUE)
            error = GetLastError();
    }
    STARTUPINFOEXA startup = {0};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input;
    startup.StartupInfo.hStdOutput = output;
    startup.StartupInfo.hStdError = output;
    SIZE_T bytes = 0;
    bool initialized = false;
    if (!error) {
        InitializeProcThreadAttributeList(NULL, 1, 0, &bytes);
        startup.lpAttributeList = ALLOC(bytes);
        initialized = InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &bytes);
        if (!initialized)
            error = GetLastError();
    }
    HANDLE handles[] = {input, output};
    if (!error
        && !UpdateProcThreadAttribute(startup.lpAttributeList,
                                      0,
                                      PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                      handles,
                                      sizeof(handles),
                                      NULL,
                                      NULL))
        error = GetLastError();
    PROCESS_INFORMATION process = {0};
    if (!error
        && !CreateProcessA(executable,
                           command,
                           NULL,
                           NULL,
                           TRUE,
                           EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW,
                           NULL,
                           NULL,
                           &startup.StartupInfo,
                           &process))
        error = GetLastError();
    if (process.hProcess) {
        DWORD code;
        if (WaitForSingleObject(process.hProcess, INFINITE) != WAIT_OBJECT_0
            || !GetExitCodeProcess(process.hProcess, &code))
            error = GetLastError();
        else
            *exit_code = code;
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    if (initialized)
        DeleteProcThreadAttributeList(startup.lpAttributeList);
    FREE(startup.lpAttributeList);
    if (input != INVALID_HANDLE_VALUE)
        CloseHandle(input);
    if (output != INVALID_HANDLE_VALUE)
        CloseHandle(output);
    FREE(command);
    FREE(executable);
    return error;
}

static void remove_file(native_compile_result_t *result, const char *path) {
    if (!DeleteFileA(path)) {
        DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && !result->windows_cleanup_error)
            result->windows_cleanup_error = error;
    }
}

native_compile_result_t
compile_native_library(const wchar_t *source, const char *compiler, const char *destination) {
    native_compile_result_t result = {.status = NATIVE_COMPILE_IO_ERROR, .exit_code = -1};
    if (!source || !compiler || !compiler[0] || !destination || !destination[0]) {
        result.system_error = EINVAL;
        return result;
    }
    DWORD size = GetFullPathNameA(destination, 0, NULL, NULL);
    if (!size) {
        result.windows_error = GetLastError();
        return result;
    }
    char *target = ALLOC(size);
    DWORD length = GetFullPathNameA(destination, size, target, NULL);
    if (!length || length >= size) {
        result.windows_error = length ? ERROR_INSUFFICIENT_BUFFER : GetLastError();
        FREE(target);
        return result;
    }
    char *directory = create_directory(target);
    if (!directory) {
        result.windows_error = GetLastError();
        FREE(target);
        return result;
    }
    char *input = join(directory, "\\module.c");
    /* PE's export directory stores this basename; moving module.dll would break imports. */
    const char *name = target;
    for (const char *p = target; *p; p++)
        if (*p == '\\' || *p == '/')
            name = p + 1;
    char *output = ALLOC(strlen(directory) + strlen(name) + 2);
    sprintf(output, "%s\\%s", directory, name);
    char *log = join(directory, "\\compiler.log");
    if (!write_utf8_file(input, source)) {
        result.system_error = errno ? errno : EIO;
        goto cleanup;
    }
    result.windows_error = run_compiler(compiler, input, output, log, &result.exit_code);
    if (result.windows_error) {
        result.status = NATIVE_COMPILE_START_ERROR;
        goto cleanup;
    }
    result.status = NATIVE_COMPILE_FAILED;
    native_compiler_read_diagnostics(&result, log);
    if (result.exit_code || result.system_error)
        goto cleanup;
    result.status = NATIVE_COMPILE_IO_ERROR;
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExA(output, GetFileExInfoStandard, &info)) {
        result.windows_error = GetLastError();
        goto cleanup;
    }
    if ((info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
        || (!info.nFileSizeHigh && !info.nFileSizeLow)) {
        result.windows_error = ERROR_INVALID_DATA;
        goto cleanup;
    }
    if (!MoveFileExA(output, target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        result.windows_error = GetLastError();
        goto cleanup;
    }
    result.status = NATIVE_COMPILE_OK;
cleanup:
    remove_file(&result, input);
    remove_file(&result, output);
    remove_file(&result, log);
    if (!RemoveDirectoryA(directory) && !result.windows_cleanup_error)
        result.windows_cleanup_error = GetLastError();
    FREE(input);
    FREE(output);
    FREE(log);
    FREE(directory);
    FREE(target);
    return result;
}

native_workspace_t *create_native_workspace(void) {
    DWORD size = GetTempPathA(0, NULL);
    if (!size)
        return NULL;
    char *base = ALLOC((size_t)size + 1);
    DWORD length = GetTempPathA(size + 1, base);
    if (!length || length > size) {
        FREE(base);
        return NULL;
    }
    char *prefix = join(base, "goat-native");
    FREE(base);
    char *directory = create_directory(prefix);
    FREE(prefix);
    if (!directory)
        return NULL;
    native_workspace_t *workspace = ALLOC(sizeof(*workspace));
    workspace->directory = directory;
    workspace->library = join(directory, "/module.dll");
    return workspace;
}

void destroy_native_workspace(native_workspace_t *workspace) {
    if (!workspace)
        return;
    remove(workspace->library);
    RemoveDirectoryA(workspace->directory);
    FREE(workspace->library);
    FREE(workspace->directory);
    FREE(workspace);
}
#endif
