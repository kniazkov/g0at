/** @file host.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Loads string operations from a separate library and supplies host services.
 */
#include "goat/native_abi.h"
#include "lib/allocate.h"
#include "model/native_services.h"

#include <stdio.h>

#ifdef TEST_NATIVE_COMPILER
#    include "codegen/native_compiler.h"
#    include "lib/io.h"

#endif
#include <assert.h>
#include <locale.h>
#include <string.h>
#ifdef _WIN32
#    include <windows.h>
#else
#    include <dlfcn.h>
#endif

int main(int argc, char **argv) {
#ifdef TEST_NATIVE_COMPILER
    int cleanup_code;
    if (cleanup_service_main(argc, argv, &cleanup_code))
        return cleanup_code;
    assert(argc == 4);
    string_value_t source = read_utf8_file(argv[3]);
    assert(source.data);
    native_compile_result_t compiled = compile_native_library(source.data, argv[2], argv[1]);
    FREE_STRING(source);
    if (compiled.diagnostics)
        fputs(compiled.diagnostics, stderr);
    assert(compiled.status == NATIVE_COMPILE_OK);
    assert(!compiled.cleanup_error && !compiled.windows_cleanup_error);
    destroy_native_compile_result(&compiled);
#else
    assert(argc == 2);
#endif
    setlocale(LC_CTYPE, "");
    typedef void(GOAT_NATIVE_CALL * suite_t)(const goat_native_context_v2_t *);
    suite_t suite = NULL;
#ifdef _WIN32
    HMODULE library = LoadLibraryA(argv[1]);
    assert(library);
    FARPROC symbol = GetProcAddress(library, "run_strings");
#else
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    assert(library);
    void *symbol = dlsym(library, "run_strings");
#endif
    assert(symbol && sizeof(symbol) == sizeof(suite));
    memcpy(&suite, &symbol, sizeof(suite));
    size_t baseline = get_allocated_memory_size();
    native_memory_t memory = {0};
    goat_native_context_v2_t context = init_native_context(&memory);
    suite(&context);
    assert(get_allocated_memory_size() > baseline);
    goat_native_deallocate_all(&context);
    assert(get_allocated_memory_size() == baseline);
#ifdef _WIN32
    FreeLibrary(library);
#else
    dlclose(library);
#endif
    return 0;
}
