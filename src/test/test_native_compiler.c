/** @file test_native_compiler.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Invalid requests and unsupported platforms leave no artifacts.
 */
#include "test_native_compiler.h"

#include "codegen/native_compiler.h"
#include "test_macro.h"

#include <errno.h>
#include <stdio.h>

bool test_native_compiler_request(void) {
#ifdef __linux__
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
