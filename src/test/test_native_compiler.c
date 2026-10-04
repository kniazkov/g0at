/** @file test_native_compiler.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Invalid requests and unsupported platforms leave no artifacts.
 */
#include "test_native_compiler.h"

#include "codegen/native_compiler.h"
#include "lib/allocate.h"
#include "lib/windows_command_line.h"
#include "test_macro.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

bool test_native_compiler_request(void) {
#if defined(__linux__) || defined(_WIN32)
    const wchar_t *sources[] = {NULL, L"", L"", L"", L""};
    const char *compilers[] = {"cc", NULL, "", "cc", "cc"};
    const char *destinations[] = {"unused.so", "unused.so", "unused.so", NULL, ""};
    for (size_t i = 0; i < 5; i++) {
        native_compile_result_t result =
            compile_native_library(sources[i], compilers[i], destinations[i]);
        ASSERT(result.status == NATIVE_COMPILE_IO_ERROR && result.system_error == EINVAL);
        ASSERT(result.exit_code == -1 && !result.signal_number && !result.cleanup_error);
        ASSERT(!result.diagnostics && !result.diagnostics_length && !result.diagnostics_truncated);
        destroy_native_compile_result(&result);
        destroy_native_compile_result(&result);
    }
#else
    native_compile_result_t result =
        compile_native_library(L"int f(void){return 1;}", "nonexistent-compiler", "unused.so");
    ASSERT(result.status == NATIVE_COMPILE_UNSUPPORTED);
    ASSERT(result.exit_code == -1 && !result.system_error && !result.cleanup_error);
    ASSERT(!result.diagnostics && !result.diagnostics_length);
    destroy_native_compile_result(&result);
#endif
    return true;
}

bool test_windows_command_line(void) {
    const char *args[] = {"compiler path.exe", "", "a b", "a\"b", "C:\\tail\\", "&%x%!", NULL};
    char *line = create_windows_command_line(args);
    ASSERT(line
           && !strcmp(line,
                      "\"compiler path.exe\" \"\" \"a b\" \"a\\\"b\" \"C:\\tail\\\\\" \"&%x%!\""));
    FREE(line);
    char large[17000];
    memset(large, 'x', sizeof(large) - 1);
    large[sizeof(large) - 1] = 0;
    const char *long_args[] = {large, NULL};
    ASSERT(!create_windows_command_line(long_args));
    const char *empty[] = {NULL};
    line = create_windows_command_line(empty);
    ASSERT(line && !line[0]);
    FREE(line);
    return true;
}
