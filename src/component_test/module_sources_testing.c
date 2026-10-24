/** @file module_sources_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief JSON output harness for source graph fixtures.
 */
#include "common/module_sources.h"
#include "lib/allocate.h"
#include "lib/binary_file.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#endif

int main(int argc, char **argv) {
#ifdef _WIN32
    fs_error_t args_error = {0};
    assert(fs_arguments(&argc, &argv, &args_error));
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    module_sources_error_t error = {0};
    module_sources_t *graph = NULL;
    if (argc == 1) {
        assert(module_sources_read(NULL, false, NULL, &graph, &error) == MODULE_SOURCES_ARGUMENT);
        assert(!graph);
        clear_module_sources_error(&error);
        free_module_sources(NULL);
#ifdef _WIN32
        free_fs_arguments(argc, argv);
#endif
        assert(!get_allocated_memory_size());
        return 0;
    }
    assert(argc >= 3);
    module_sources_options_t options = {65536, 64 * 1024 * 1024, 256 * 1024 * 1024};
    if (argc > 3)
        options.max_files = strtoul(argv[3], NULL, 10);
    if (argc > 4)
        options.max_file_bytes = strtoul(argv[4], NULL, 10);
    if (argc > 5)
        options.max_total_bytes = strtoul(argv[5], NULL, 10);
    module_sources_status_t status =
        module_sources_read(argv[2], !strcmp(argv[1], "gmod"), &options, &graph, &error);
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_uint(doc, root, "status", status);
    if (status) {
        assert(!graph);
        yyjson_mut_obj_add_str(doc, root, "message", error.message ? error.message : "");
        yyjson_mut_obj_add_str(doc, root, "path", error.path ? error.path : "");
        yyjson_mut_obj_add_uint(doc, root, "row", error.row);
        yyjson_mut_obj_add_uint(doc, root, "column", error.column);
        yyjson_mut_obj_add_uint(doc, root, "fs", error.filesystem.status);
    } else {
        yyjson_mut_val *files = yyjson_mut_arr(doc);
        yyjson_mut_obj_add_val(doc, root, "files", files);
        for (size_t i = 0; i < graph->count; i++) {
            module_source_t *file = &graph->files[i];
            yyjson_mut_val *item = yyjson_mut_obj(doc), *targets = yyjson_mut_arr(doc);
            yyjson_mut_obj_add_str(doc, item, "name", file->name);
            yyjson_mut_obj_add_bool(doc, item, "external", file->external);
            yyjson_mut_obj_add_uint(doc, item, "length", file->length);
            yyjson_mut_obj_add_uint(doc,
                                    item,
                                    "checksum",
                                    binary_checksum(file->source, file->length));
            yyjson_mut_obj_add_val(doc, item, "targets", targets);
            for (size_t j = 0; j < file->includes.count; j++)
                yyjson_mut_arr_add_uint(doc, targets, file->targets[j]);
            yyjson_mut_arr_add_val(files, item);
        }
        if (graph->descriptor) {
            yyjson_mut_obj_add_val(
                doc,
                root,
                "descriptor",
                yyjson_val_mut_copy(doc, yyjson_doc_get_root(graph->descriptor)));
        }
        /* Independent snapshots: rebuilding has distinct storage and does not mutate graph. */
        module_sources_t *second = NULL;
        assert(module_sources_read(argv[2], !strcmp(argv[1], "gmod"), &options, &second, NULL)
               == 0);
        assert(second != graph && second->count == graph->count);
        for (size_t i = 0; i < graph->count; i++) {
            assert(second->files[i].source != graph->files[i].source);
            assert(!strcmp(second->files[i].path, graph->files[i].path));
        }
        free_module_sources(second);
    }
    char *text = yyjson_mut_write(doc, 0, NULL);
    assert(text);
    puts(text);
    free(text);
    yyjson_mut_doc_free(doc);
    clear_module_sources_error(&error);
    free_module_sources(graph);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    assert(!get_allocated_memory_size());
    return status;
}
