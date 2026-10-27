/** @file module_library_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Exercise the production mapped-range loader with controlled real images.
 */
#include "lib/files/binary_file.h"
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "model/module_library.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    fs_error_t error = {0};
    assert(fs_arguments(&argc, &argv, &error));
    assert(argc == 4);
    size_t size = 0;
    void *bytes = read_binary_file(argv[2], 16 * 1024 * 1024, &size);
    assert(bytes);
    yyjson_doc *metadata = NULL;
    assert(goat_module_json_read(bytes, size, GOAT_MODULE_METADATA, &metadata)
           == GOAT_MODULE_JSON_OK);
    FREE(bytes);
    module_library_result_t result = load_module_library(argv[1], metadata, 0);
    bool success = !strcmp(argv[3], "success");
    if ((result.library != NULL) != success) {
        FPRINTF(stderr,
                "Unexpected loader result: %s\n",
                result.diagnostic ? result.diagnostic : "success");
        return 1;
    }
    if (success) {
        assert(!result.diagnostic);
        const yyjson_val *root = yyjson_doc_get_root(metadata);
        const yyjson_val *variant = yyjson_arr_get_first(yyjson_obj_get(root, "native"));
        const yyjson_val *specs = yyjson_obj_get(variant, "specializations");
        for (size_t i = 0; i < yyjson_arr_size(specs); i++) {
            const char *name = yyjson_get_str(yyjson_obj_get(yyjson_arr_get(specs, i), "symbol"));
            module_native_entry_t entry = {0};
            assert(resolve_module_library_entry(result.library, name, &entry));
            assert(entry.adapter && entry.readable && entry.readable_state);
            assert(!entry.readable(entry.readable_state, &entry, sizeof(entry)));
            assert(!entry.readable(entry.readable_state, NULL, 1));
            assert(!entry.readable(entry.readable_state, (void *)(uintptr_t)(UINTPTR_MAX - 2), 8));
            assert(!entry.readable(entry.readable_state, (void *)(uintptr_t)1, 8));
        }
        module_native_entry_t entry = {0};
        assert(!resolve_module_library_entry(result.library, "not_a_declared_adapter", &entry));
        assert(!entry.adapter && !entry.readable && !entry.readable_state);
    } else {
        assert(result.diagnostic && *result.diagnostic);
    }
    release_module_library(result.library);
    FREE(result.diagnostic);
    yyjson_doc_free(metadata);
    free_fs_arguments(argc, argv);
    clear_fs_error(&error);
    assert(!get_allocated_memory_size());
    return 0;
}
