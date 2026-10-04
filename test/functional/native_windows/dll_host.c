/** @file dll_host.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Tests the exact public DLL export; not a production module loader.
 */
#include <assert.h>
#include <goat/native_abi.h>
#include <string.h>
#include <windows.h>

int test_native_abi(goat_native_query_v1_t query);

int main(int argc, char **argv) {
    assert(argc == 2);
    HMODULE module = LoadLibraryA(argv[1]);
    assert(module);
    FARPROC address = GetProcAddress(module, "goat_native_query_v1");
    assert(address);
    assert(!GetProcAddress(module, "goat_f1_i_"));
    goat_native_query_v1_t query;
    _Static_assert(sizeof(query) == sizeof(address), "Windows function pointers");
    memcpy(&query, &address, sizeof(query));
    int result = test_native_abi(query);
    assert(FreeLibrary(module));
    return result;
}
