/** @file native_compiler_linux.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Linux shared-library compilation, publication and temporary-file cleanup.
 */
#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#    define _POSIX_C_SOURCE 200809L
#endif
#include "native_compiler.h"

#ifdef __linux__
#    include "lib/allocate.h"
#    include "lib/io.h"

#    include <errno.h>
#    include <fcntl.h>
#    include <spawn.h>
#    include <stdio.h>
#    include <stdlib.h>
#    include <string.h>
#    include <sys/stat.h>
#    include <sys/wait.h>
#    include <unistd.h>

extern char **environ;

static char *join(const char *first, const char *second) {
    size_t a = strlen(first), b = strlen(second);
    char *text = ALLOC(a + b + 1);
    memcpy(text, first, a);
    memcpy(text + a, second, b + 1);
    return text;
}

/** @brief Redirects compiler input from /dev/null and both output streams to the log. */
static int start_compiler(const char *compiler,
                          const char *source,
                          const char *library,
                          const char *log,
                          pid_t *pid) {
    posix_spawn_file_actions_t actions;
    int error = posix_spawn_file_actions_init(&actions);
    if (error)
        return error;
    error = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    if (!error)
        error = posix_spawn_file_actions_addopen(&actions,
                                                 STDERR_FILENO,
                                                 log,
                                                 O_WRONLY | O_CREAT | O_TRUNC,
                                                 0600);
    if (!error)
        error = posix_spawn_file_actions_adddup2(&actions, STDERR_FILENO, STDOUT_FILENO);
    char *const args[] = {(char *)compiler,
                          "-std=c11",
                          "-O2",
                          "-fPIC",
                          "-shared",
                          "-fno-fast-math",
                          "-ffp-contract=off",
                          "-Wl,-z,defs",
                          "-o",
                          (char *)library,
                          (char *)source,
                          "-lm",
                          NULL};
    if (!error)
        error = posix_spawnp(pid, compiler, &actions, NULL, args, environ);
    posix_spawn_file_actions_destroy(&actions);
    return error;
}

static void remove_file(native_compile_result_t *result, const char *path) {
    if (unlink(path) && errno != ENOENT && !result->cleanup_error)
        result->cleanup_error = errno;
}

native_compile_result_t
compile_native_library(const wchar_t *source, const char *compiler, const char *destination) {
    native_compile_result_t result = {.status = NATIVE_COMPILE_IO_ERROR, .exit_code = -1};
    if (!source || !destination || !destination[0] || !compiler || !compiler[0]) {
        result.system_error = EINVAL;
        return result;
    }
    /* Relative compiler operands must not be interpreted as options. */
    char *target = join(destination[0] == '/' ? "" : "./", destination);
    char *directory = join(target, ".goat-XXXXXX");
    if (!mkdtemp(directory)) {
        result.system_error = errno;
        FREE(directory);
        FREE(target);
        return result;
    }
    char *input = join(directory, "/module.c");
    char *output = join(directory, "/module.so");
    char *log = join(directory, "/compiler.log");
    if (!write_utf8_file(input, source)) {
        result.system_error = errno ? errno : EIO;
        goto cleanup;
    }
    pid_t pid;
    result.system_error = start_compiler(compiler, input, output, log, &pid);
    if (result.system_error) {
        result.status = NATIVE_COMPILE_START_ERROR;
        goto cleanup;
    }
    int status;
    pid_t waited;
    do {
        waited = waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0) {
        result.system_error = errno;
        goto cleanup;
    }
    result.status = NATIVE_COMPILE_FAILED;
    if (WIFEXITED(status))
        result.exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
        result.signal_number = WTERMSIG(status);
    native_compiler_read_diagnostics(&result, log);
    if (result.exit_code != 0 || result.system_error)
        goto cleanup;
    result.status = NATIVE_COMPILE_IO_ERROR;
    struct stat info;
    if (lstat(output, &info)) {
        result.system_error = errno;
        goto cleanup;
    }
    if (!S_ISREG(info.st_mode) || !info.st_size) {
        result.system_error = EIO;
        goto cleanup;
    }
    if (rename(output, target)) {
        result.system_error = errno;
        goto cleanup;
    }
    result.status = NATIVE_COMPILE_OK;
cleanup:
    remove_file(&result, input);
    remove_file(&result, output);
    remove_file(&result, log);
    if (rmdir(directory) && !result.cleanup_error)
        result.cleanup_error = errno;
    FREE(input);
    FREE(output);
    FREE(log);
    FREE(directory);
    FREE(target);
    return result;
}

native_workspace_t *create_native_workspace(void) {
    const char *base = getenv("TMPDIR");
    if (!base || !*base)
        base = "/tmp";
    char *directory = join(base, "/goat-native-XXXXXX");
    if (!mkdtemp(directory)) {
        FREE(directory);
        return NULL;
    }
    native_workspace_t *workspace = ALLOC(sizeof(*workspace));
    workspace->directory = directory;
    workspace->library = join(directory, "/module.so");
    return workspace;
}

void destroy_native_workspace(native_workspace_t *workspace) {
    if (!workspace)
        return;
    remove(workspace->library);
    rmdir(workspace->directory);
    FREE(workspace->library);
    FREE(workspace->directory);
    FREE(workspace);
}
#endif
