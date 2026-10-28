/** @file module_imports_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Static dependency collector contract and fixture harness.
 */
#include "common/module_imports.h"
#include "lib/memory/allocate.h"

/* Fixture setup and API checks must also execute in Release builds. */
#ifdef NDEBUG
#    undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#endif

static void contract_tests(void) {
    module_imports_t *out = NULL;
    module_imports_error_t error;
    assert(module_imports_collect(NULL, 0, &out, &error) == MODULE_IMPORTS_ARGUMENT);
    assert(!out && error.status == MODULE_IMPORTS_ARGUMENT);
    module_source_t file = {.name = "memory.goat",
                            .source = "import \"math@1.0.0\";",
                            .length = strlen("import \"math@1.0.0\";")};
    module_sources_t graph = {.files = &file, .count = 1};
    assert(module_imports_collect(&graph, 0, NULL, NULL) == MODULE_IMPORTS_ARGUMENT);
    const char *bad = "{\"dependencies\":{\"math\":2}}";
    graph.descriptor = yyjson_read(bad, strlen(bad), 0);
    assert(module_imports_collect(&graph, 0, &out, &error) == MODULE_IMPORTS_DESCRIPTOR);
    assert(!out && error.json);
    yyjson_doc_free(graph.descriptor);
    graph.descriptor = NULL;
    assert(!module_imports_collect(&graph, 0, &out, NULL));
    assert(out->count == 1 && out->dependencies[0].count == 1);
    module_imports_t *second = NULL;
    assert(!module_imports_collect(&graph, 0, &second, &error));
    assert(second != out && second->dependencies != out->dependencies);
    assert(second->dependencies[0].name != out->dependencies[0].name);
    assert(!strcmp(file.source, "import \"math@1.0.0\";"));
    assert(error.status == MODULE_IMPORTS_OK && !error.message);
    free_module_imports(second);
    free_module_imports(out);
    free_module_imports(NULL);
}

int main(int argc, char **argv) {
#ifdef _WIN32
    fs_error_t args_error = {0};
    assert(fs_arguments(&argc, &argv, &args_error));
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    contract_tests();
    int status = 0;
    if (argc > 1) {
        assert(argc >= 3);
        module_sources_t *graph = NULL;
        module_sources_error_t source_error = {0};
        module_imports_t *imports = NULL;
        module_imports_error_t error = {0};
        yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
        yyjson_mut_val *root = yyjson_mut_obj(doc);
        yyjson_mut_doc_set_root(doc, root);
        status =
            module_sources_read(argv[2], !strcmp(argv[1], "gmod"), NULL, &graph, &source_error);
        if (status) {
            status += 100;
            yyjson_mut_obj_add_str(doc,
                                   root,
                                   "message",
                                   source_error.message ? source_error.message : "");
        } else {
            size_t limit = argc > 3 ? strtoul(argv[3], NULL, 10) : 0;
            status = module_imports_collect(graph, limit, &imports, &error);
            yyjson_mut_val *files = yyjson_mut_arr(doc);
            for (size_t f = 0; f < graph->count; f++)
                yyjson_mut_arr_add_strcpy(doc, files, graph->files[f].name);
            yyjson_mut_obj_add_val(doc, root, "files", files);
            /* Result lifetime is independent of all source and descriptor storage. */
            free_module_sources(graph);
            graph = NULL;
            if (status) {
                assert(!imports);
                yyjson_mut_obj_add_str(doc, root, "message", error.message ? error.message : "");
                yyjson_mut_obj_add_uint(doc, root, "file", error.site.file);
                yyjson_mut_obj_add_uint(doc, root, "row", error.site.row);
                yyjson_mut_obj_add_uint(doc, root, "column", error.site.column);
                yyjson_mut_obj_add_uint(doc, root, "offset", error.site.offset);
            } else {
                for (size_t warning = 0; warning < 2; warning++) {
                    yyjson_mut_val *array = yyjson_mut_arr(doc);
                    module_import_dependency_t *items =
                        warning ? imports->unused_defaults : imports->dependencies;
                    size_t count = warning ? imports->unused_count : imports->count;
                    yyjson_mut_obj_add_val(doc,
                                           root,
                                           warning ? "unused_defaults" : "dependencies",
                                           array);
                    for (size_t i = 0; i < count; i++) {
                        yyjson_mut_val *item = yyjson_mut_obj(doc), *sites = yyjson_mut_arr(doc);
                        yyjson_mut_obj_add_str(doc, item, "name", items[i].name);
                        yyjson_mut_obj_add_str(doc, item, "version", items[i].version);
                        yyjson_mut_obj_add_val(doc, item, "sites", sites);
                        for (size_t j = 0; j < items[i].count; j++) {
                            module_import_site_t s = items[i].sites[j];
                            yyjson_mut_val *site = yyjson_mut_obj(doc);
                            yyjson_mut_obj_add_uint(doc, site, "file", s.file);
                            yyjson_mut_obj_add_uint(doc, site, "row", s.row);
                            yyjson_mut_obj_add_uint(doc, site, "column", s.column);
                            yyjson_mut_obj_add_uint(doc, site, "offset", s.offset);
                            yyjson_mut_arr_add_val(sites, site);
                        }
                        yyjson_mut_arr_add_val(array, item);
                    }
                }
            }
        }
        yyjson_mut_obj_add_uint(doc, root, "status", status);
        char *json = yyjson_mut_write(doc, 0, NULL);
        assert(json);
        puts(json);
        free(json);
        yyjson_mut_doc_free(doc);
        free_module_imports(imports);
        free_module_sources(graph);
        clear_module_sources_error(&source_error);
    }
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    assert(!get_allocated_memory_size());
    return status;
}
