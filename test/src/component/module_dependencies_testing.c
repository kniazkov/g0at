/** @file module_dependencies_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Per-consumer exact dependency binding contract and fixture driver.
 */
#include "common/module_imports.h"
#include "lib/allocate.h"
#include "vm/module_dependencies.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#endif

static void contracts(void) {
    module_dependencies_t *result = (void *)(uintptr_t)1;
    module_dependencies_error_t error = {0};
    assert(bind_source_dependencies(NULL, NULL, NULL, &result, &error)
           == MODULE_DEPENDENCIES_ARGUMENT);
    assert(!result && error.status == MODULE_DEPENDENCIES_ARGUMENT);
    assert(bind_module_dependencies(NULL, NULL, NULL, &result, &error)
           == MODULE_DEPENDENCIES_ARGUMENT);
    assert(!result);
    assert(bind_module_dependencies(NULL, NULL, NULL, NULL, NULL) == MODULE_DEPENDENCIES_ARGUMENT);
    assert(!module_dependencies_count(NULL));
    assert(!module_dependency_snapshot(NULL, 0));
    assert(!module_dependencies_metadata(NULL));
    clear_module_dependencies_error(&error);
    clear_module_dependencies_error(NULL);
    free_module_dependencies(NULL);
}

static void input_contracts(const module_search_t *search) {
    module_dependencies_error_t error = {0};
    module_dependencies_t *out = NULL;
    module_imports_t imports = {.count = 1};
    assert(bind_source_dependencies(&imports, search, NULL, &out, &error)
           == MODULE_DEPENDENCIES_ARGUMENT);
    assert(!out && error.index == SIZE_MAX && !error.search.count);
    module_import_dependency_t pairs[] = {{.name = "missing", .version = "1"},
                                          {.name = "missing", .version = "1"}};
    imports.dependencies = pairs;
    imports.count = 2;
    assert(bind_source_dependencies(&imports, search, NULL, &out, &error)
           == MODULE_DEPENDENCIES_ARGUMENT);
    assert(!out && error.index == SIZE_MAX && !error.search.count);
    pairs[1].name = "../invalid";
    assert(bind_source_dependencies(&imports, search, NULL, &out, &error)
           == MODULE_DEPENDENCIES_ARGUMENT);
    assert(!out && !error.search.count);
    pairs[1].name = NULL;
    assert(bind_source_dependencies(&imports, search, NULL, &out, NULL)
           == MODULE_DEPENDENCIES_ARGUMENT);
    imports.count = SIZE_MAX;
    assert(bind_source_dependencies(&imports, search, NULL, &out, &error)
           == MODULE_DEPENDENCIES_ARGUMENT);
    imports.count = 0;
    assert(bind_source_dependencies(&imports, search, NULL, NULL, &error)
           == MODULE_DEPENDENCIES_ARGUMENT);
    assert(!bind_source_dependencies(&imports, search, NULL, &out, NULL));
    assert(out && !module_dependencies_count(out));
    assert(yyjson_is_arr(yyjson_doc_get_root(module_dependencies_metadata(out))));
    free_module_dependencies(out);
    clear_module_dependencies_error(&error);
}

static void report(const module_validation_diagnostic_t *diagnostic, void *context) {
    assert(diagnostic);
    (*(size_t *)context)++;
}

int main(int argc, char **argv) {
#ifdef _WIN32
    fs_error_t args_error = {0};
    assert(fs_arguments(&argc, &argv, &args_error));
    clear_fs_error(&args_error);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    contracts();
    if (argc > 1) {
        assert(argc == 4);
        module_search_t *search = NULL;
        module_search_error_t search_error = {0};
        assert(!prepare_module_search(argv[2], NULL, NULL, &search, &search_error));
        input_contracts(search);
        size_t reports = 0;
        module_validation_options_t options = {SIZE_MAX, SIZE_MAX, report, &reports};
        module_dependencies_t *result = NULL;
        module_dependencies_error_t error = {0};
        module_imports_t *imports = NULL;
        module_sources_t *sources = NULL;
        validated_module_t *module = NULL;
        module_sources_error_t source_error = {0};
        module_imports_error_t import_error = {0};
        yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
        yyjson_mut_val *root = yyjson_mut_obj(doc);
        yyjson_mut_doc_set_root(doc, root);
        int status;
        if (!strcmp(argv[1], "module")) {
            module_validation_result_t validated = validate_module_file(argv[3], NULL, &module);
            assert(validated.status == MODULE_VALIDATION_OK);
            status = bind_module_dependencies(module, search, &options, &result, &error);
        } else {
            status = module_sources_read(argv[3],
                                         !strcmp(argv[1], "gmod"),
                                         NULL,
                                         &sources,
                                         &source_error);
            assert(!status);
            status = module_imports_collect(sources, 0, &imports, &import_error);
            if (status) {
                status += 100;
            } else {
                yyjson_mut_obj_add_uint(doc, root, "unused", imports->unused_count);
                status = bind_source_dependencies(imports, search, &options, &result, &error);
            }
        }
        free_module_imports(imports);
        free_module_sources(sources);
        free_validated_module(module);
        free_module_search(search);
        clear_module_sources_error(&source_error);
        clear_module_search_error(&search_error);
        yyjson_mut_obj_add_uint(doc, root, "status", status);
        if (!status) {
            assert(!module_dependencies_count(result) || reports);
            /* Every view remains usable after freeing all input snapshots/search state. */
            yyjson_mut_val *dependencies =
                yyjson_val_mut_copy(doc, yyjson_doc_get_root(module_dependencies_metadata(result)));
            yyjson_mut_obj_add_val(doc, root, "dependencies", dependencies);
            yyjson_mut_val *nested = yyjson_mut_arr(doc);
            for (size_t i = 0; i < module_dependencies_count(result); i++) {
                const validated_module_t *snapshot = module_dependency_snapshot(result, i);
                assert(snapshot);
                const yyjson_doc *metadata =
                    goat_module_container_metadata(validated_module_container(snapshot));
                yyjson_mut_arr_add_val(nested,
                                       yyjson_val_mut_copy(doc, yyjson_doc_get_root(metadata)));
            }
            assert(!module_dependency_snapshot(result, module_dependencies_count(result)));
            yyjson_mut_obj_add_val(doc, root, "snapshots", nested);
        } else {
            assert(!result);
            yyjson_mut_obj_add_uint(doc, root, "search_status", error.search.status);
            yyjson_mut_obj_add_uint(doc, root, "index", error.index);
            yyjson_mut_val *attempted = yyjson_mut_arr(doc);
            for (size_t i = 0; i < error.search.count; i++)
                yyjson_mut_arr_add_strcpy(doc, attempted, error.search.attempted[i]);
            yyjson_mut_obj_add_val(doc, root, "attempted", attempted);
        }
        char *json = yyjson_mut_write(doc, 0, NULL);
        assert(json);
        puts(json);
        free(json);
        yyjson_mut_doc_free(doc);
        free_module_dependencies(result);
        clear_module_dependencies_error(&error);
    }
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    assert(!get_allocated_memory_size());
    return 0;
}
