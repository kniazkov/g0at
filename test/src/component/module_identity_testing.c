/** @file module_identity_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Runtime instance identity, access boundaries and whole-copy selection.
 */
#include "lib/memory/allocate.h"
#include "model/process.h"
#include "vm/module_snapshot.h"

#ifdef NDEBUG
#    undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void checked(const module_validation_diagnostic_t *event, void *context) {
    if (event->check == MODULE_CHECK_INPUT && event->state == MODULE_CHECK_PASSED)
        ++*(size_t *)context;
}

int main(int argc, char **argv) {
    assert(argc == 3);
    assert(module_snapshot_find(NULL, "a", "1", "hash") == SIZE_MAX);
    module_search_t *search = NULL;
    module_search_error_t error = {0};
    assert(!prepare_module_search(argv[1], argv[2], NULL, &search, &error));
    size_t checks = 0;
    module_validation_options_t options = {SIZE_MAX, SIZE_MAX, checked, &checks};
    module_snapshot_t *snapshot = NULL;
    module_graph_t *diagnostics = NULL;
    module_graph_status_t status =
        load_module_snapshot(argv[1], search, &options, &snapshot, &diagnostics);
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *result = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, result);
    yyjson_mut_obj_add_uint(doc, result, "status", status);
    yyjson_mut_obj_add_uint(doc, result, "checks", checks);
    if (status) {
        assert(!snapshot && diagnostics);
        free_module_graph(diagnostics);
    } else {
        assert(snapshot && !diagnostics);
        size_t count = module_snapshot_count(snapshot);
        assert(checks == count);
        module_snapshot_t *fresh = NULL;
        assert(!load_module_snapshot(argv[1], search, NULL, &fresh, NULL));
        assert(module_snapshot_count(fresh) == count);
        const module_graph_t *graph = module_snapshot_graph(snapshot);
        object_static_string_t counter_storage = {0}, internal_storage = {0}, extra_storage = {0};
        object_t *counter = get_static_string_object(&counter_storage, U"counter");
        object_t *internal = get_static_string_object(&internal_storage, U"internal");
        object_t *extra = get_static_string_object(&extra_storage, U"extra");
        for (size_t i = 0; i < count; ++i) {
            module_files_t *files = module_snapshot_files(snapshot, i);
            module_t *identity = module_files_identity(files);
            object_t *scope = module_file_scope(module_files_main(files));
            assert(identity != module_files_identity(module_snapshot_files(fresh, i)));
            assert(!get_own_field_cell(
                module_file_scope(module_files_main(module_snapshot_files(fresh, i))),
                counter));
            assert(create_object_field(scope,
                                       counter,
                                       get_static_integer_object(0),
                                       false,
                                       ACCESS_EXTERNAL,
                                       identity,
                                       get_object_access_domain(scope),
                                       true));
            assert(create_object_field(scope,
                                       internal,
                                       get_static_integer_object(42),
                                       false,
                                       ACCESS_PUBLIC,
                                       identity,
                                       get_object_access_domain(scope),
                                       true));
            function_access_t own;
            init_function_access_in_file(&own, identity, 0);
            assert(lookup_accessible_property(&own, scope, internal, false).status == MSTAT_OK);
            for (size_t j = 0; j < i; ++j)
                assert(identity != module_files_identity(module_snapshot_files(snapshot, j)));
            clear_function_access(&own, true);
        }
        yyjson_mut_val *nodes = yyjson_mut_arr(doc);
        yyjson_mut_obj_add_val(doc, result, "nodes", nodes);
        for (size_t i = 0; i < count; ++i) {
            const module_graph_node_t *node = module_graph_node(graph, i);
            assert(module_snapshot_find(snapshot, node->name, node->version, node->hash) == i);
            assert(module_snapshot_find(snapshot, node->name, node->version, "wrong") == SIZE_MAX);
            assert(module_snapshot_find(snapshot, NULL, node->version, node->hash) == SIZE_MAX);
            assert(module_snapshot_find(snapshot, node->name, NULL, node->hash) == SIZE_MAX);
            assert(module_snapshot_find(snapshot, node->name, node->version, NULL) == SIZE_MAX);
            assert(module_snapshot_find(snapshot, "missing", node->version, node->hash)
                   == SIZE_MAX);
            assert(module_snapshot_find(snapshot, node->name, "missing", node->hash) == SIZE_MAX);
            assert(module_snapshot_find(snapshot, "", node->version, node->hash) == SIZE_MAX);
            assert(module_snapshot_find(snapshot, node->name, "", node->hash) == SIZE_MAX);
            assert(module_snapshot_find(snapshot, node->name, node->version, "") == SIZE_MAX);
            char *upper = malloc(strlen(node->name) + 1);
            assert(upper);
            strcpy(upper, node->name);
            upper[0] = (char)(upper[0] - 'a' + 'A');
            assert(module_snapshot_find(snapshot, upper, node->version, node->hash) == SIZE_MAX);
            free(upper);
            module_files_t *files = module_snapshot_files(snapshot, i);
            function_access_t caller;
            init_function_access_in_file(&caller, module_files_identity(files), 0);
            yyjson_mut_val *row = yyjson_mut_obj(doc), *targets = yyjson_mut_arr(doc);
            yyjson_mut_arr_append(nodes, row);
            yyjson_mut_obj_add_strcpy(doc, row, "name", node->name);
            yyjson_mut_obj_add_strcpy(doc, row, "version", node->version);
            yyjson_mut_obj_add_strcpy(doc, row, "hash", node->hash);
            yyjson_mut_obj_add_val(doc, row, "targets", targets);
            for (size_t j = 0; j < count; ++j) {
                object_t *scope =
                    module_file_scope(module_files_main(module_snapshot_files(snapshot, j)));
                assert(lookup_accessible_property(&caller, scope, counter, false).status
                       == MSTAT_OK);
                assert(lookup_accessible_property(&caller, scope, internal, false).status
                       == (i == j ? MSTAT_OK : MSTAT_ACCESS_DENIED));
                if (i != j)
                    assert(create_accessible_property(&caller,
                                                      scope,
                                                      extra,
                                                      get_static_integer_object(1),
                                                      false)
                           == MSTAT_FOREIGN_OBJECT);
            }
            size_t deps = module_snapshot_dependency_count(snapshot, i);
            for (size_t j = 0; j < deps; ++j) {
                size_t target = module_snapshot_dependency(snapshot, i, j);
                const module_graph_node_t *dependency = module_graph_node(graph, target);
                assert(module_snapshot_import(snapshot, i, dependency->name, dependency->version)
                       == target);
                module_files_t *target_files = module_snapshot_files(snapshot, target);
                object_t *scope = module_file_scope(module_files_main(target_files));
                property_lookup_t lookup =
                    lookup_accessible_property(&caller, scope, counter, false);
                int_value_t value = get_object_integer_value(lookup.value);
                assert(value.has_value);
                assert(write_accessible_property(&caller,
                                                 scope,
                                                 counter,
                                                 get_static_integer_object(value.value + 1))
                       == MSTAT_OK);
                yyjson_mut_arr_add_uint(doc, targets, target);
            }
            const goat_module_container *container = validated_module_container(node->snapshot);
            yyjson_mut_val *native = yyjson_mut_arr(doc);
            yyjson_mut_obj_add_val(doc, row, "native", native);
            for (size_t j = 0; j < goat_module_container_native_count(container); ++j) {
                goat_container_native library;
                assert(goat_module_container_native_at(container, j, &library));
                yyjson_mut_arr_add_strncpy(doc,
                                           native,
                                           (const char *)library.bytes.data,
                                           library.bytes.size);
            }
            clear_function_access(&caller, true);
        }
        for (size_t i = 0; i < count; ++i) {
            object_t *scope =
                module_file_scope(module_files_main(module_snapshot_files(snapshot, i)));
            int_value_t value =
                get_object_integer_value(get_own_field_cell(scope, counter)->object);
            assert(value.has_value);
            yyjson_mut_obj_add_sint(doc, yyjson_mut_arr_get(nodes, i), "counter", value.value);
        }
        free_module_snapshot(fresh);
        free_module_snapshot(snapshot);
    }
    free_module_search(search);
    clear_module_search_error(&error);
    char *json = yyjson_mut_write(doc, 0, NULL);
    assert(json);
    puts(json);
    free(json);
    yyjson_mut_doc_free(doc);
    assert(!get_allocated_memory_size());
    return 0;
}
