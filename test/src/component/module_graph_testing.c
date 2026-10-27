/** @file module_graph_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Whole-graph preflight contract and independent fixture driver.
 */
#include "lib/allocate.h"
#include "vm/module_graph.h"
#include "vm/module_graph_diagnostics.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#endif

typedef struct {
    yyjson_mut_doc *doc;
    yyjson_mut_val *paths;
    bool stop;
} paths_t;

static bool path(const size_t *edges, size_t count, void *context) {
    paths_t *p = context;
    yyjson_mut_val *array = yyjson_mut_arr(p->doc);
    for (size_t i = 0; i < count; i++)
        yyjson_mut_arr_add_uint(p->doc, array, edges[i]);
    yyjson_mut_arr_append(p->paths, array);
    return !p->stop;
}

static void paths(yyjson_mut_doc *doc,
                  yyjson_mut_val *object,
                  const module_graph_t *graph,
                  size_t node,
                  size_t edge) {
    paths_t p = {doc, yyjson_mut_arr(doc), false};
    assert(module_graph_paths(graph, node, edge, path, &p));
    yyjson_mut_obj_add_val(doc, object, "paths", p.paths);
    if (yyjson_mut_arr_size(p.paths)) {
        p.paths = yyjson_mut_arr(doc);
        p.stop = true;
        assert(!module_graph_paths(graph, node, edge, path, &p));
        assert(yyjson_mut_arr_size(p.paths) == 1);
    }
}

typedef struct {
    yyjson_mut_doc *doc;
    yyjson_mut_val *rows;
    bool stop;
} diagnostics_t;

static bool diagnostic(const module_graph_diagnostic_t *d, void *context) {
    diagnostics_t *out = context;
    yyjson_mut_doc *doc = out->doc;
    yyjson_mut_val *row = yyjson_mut_obj(doc), *path = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_uint(doc, row, "kind", d->kind);
    yyjson_mut_obj_add_uint(doc, row, "node", d->node);
    yyjson_mut_obj_add_uint(doc, row, "edge", d->edge);
    yyjson_mut_obj_add_strncpy(doc,
                               row,
                               "reference",
                               d->reference ? d->reference : "",
                               d->reference_length);
    yyjson_mut_obj_add_uint(doc, row, "reference_length", d->reference_length);
    for (size_t i = 0; i < d->path_count; i++)
        yyjson_mut_arr_add_uint(doc, path, d->path[i]);
    yyjson_mut_obj_add_val(doc, row, "path", path);
    if (d->validation) {
        const module_validation_diagnostic_t *v = d->validation;
        yyjson_mut_val *item = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, item, "check", v->check);
        yyjson_mut_obj_add_uint(doc, item, "state", v->state);
        yyjson_mut_obj_add_int(doc, item, "detail", v->detail);
        yyjson_mut_obj_add_uint(doc, item, "index", v->index);
        yyjson_mut_val *entry_bytes = yyjson_mut_arr(doc);
        bool ascii = true;
        for (size_t i = 0; i < v->entry.size; i++) {
            yyjson_mut_arr_add_uint(doc, entry_bytes, v->entry.data[i]);
            ascii = ascii && v->entry.data[i] < 128;
        }
        yyjson_mut_obj_add_val(doc, item, "entry_bytes", entry_bytes);
        yyjson_mut_obj_add_val(
            doc,
            item,
            "entry",
            yyjson_mut_strncpy(doc,
                               ascii && v->entry.data ? (const char *)v->entry.data : "",
                               ascii ? v->entry.size : 0));
        yyjson_mut_obj_add_int(doc, item, "container_status", v->container.status);
        yyjson_mut_obj_add_int(doc, item, "zip_error", v->container.zip_error);
        yyjson_mut_obj_add_int(doc, item, "json_error", v->container.json_error);
        yyjson_mut_obj_add_val(doc,
                               item,
                               "field",
                               yyjson_mut_strncpy(doc, v->field ? v->field : "", v->field_length));
        yyjson_mut_obj_add_val(doc, row, "validation", item);
    }
    yyjson_mut_arr_append(out->rows, row);
    return !out->stop;
}

static void diagnostics(yyjson_mut_doc *doc, yyjson_mut_val *root, const module_graph_t *graph) {
    diagnostics_t out = {doc, yyjson_mut_arr(doc), false};
    assert(!report_module_graph_diagnostics(graph, NULL, NULL));
    assert(report_module_graph_diagnostics(graph, diagnostic, &out));
    yyjson_mut_obj_add_val(doc, root, "diagnostics", out.rows);
    if (yyjson_mut_arr_size(out.rows)) {
        out.rows = yyjson_mut_arr(doc);
        out.stop = true;
        assert(!report_module_graph_diagnostics(graph, diagnostic, &out));
        assert(yyjson_mut_arr_size(out.rows) == 1);
    }
    char *text = format_module_graph_diagnostics(graph);
    assert(text);
    yyjson_mut_obj_add_strcpy(doc, root, "diagnostic_text", text);
    FREE(text);
}

static void contracts(const module_search_t *search) {
    module_graph_t *graph = (void *)(uintptr_t)1;
    assert(validate_module_graph(NULL, search, NULL, &graph) == MODULE_GRAPH_ARGUMENT);
    assert(!graph);
    assert(validate_module_graph_memory(NULL, 1, "memory", search, NULL, &graph)
           == MODULE_GRAPH_ARGUMENT);
    assert(!graph);
    assert(validate_module_graph_memory(NULL, 0, "", search, NULL, &graph)
           == MODULE_GRAPH_ARGUMENT);
    assert(!graph);
    assert(validate_source_graph(NULL, search, NULL, &graph) == MODULE_GRAPH_ARGUMENT);
    assert(!graph);
    assert(!module_graph_node(NULL, 0) && !module_graph_edge(NULL, 0));
    assert(!module_graph_issue(NULL, 0));
    assert(!module_graph_node_count(NULL) && !module_graph_edge_count(NULL));
    assert(!module_graph_issue_count(NULL));
    assert(!module_graph_validation_count(NULL, 0));
    assert(!module_graph_validation(NULL, 0, 0));
    assert(!module_graph_paths(NULL, 0, SIZE_MAX, NULL, NULL));
    free_module_graph(NULL);
    assert(!report_module_graph_diagnostics(NULL, diagnostic, NULL));
    assert(!format_module_graph_diagnostics(NULL));
    if (!search)
        return;
    module_import_dependency_t deps[] = {{.name = "absent", .version = "1"},
                                         {.name = "absent", .version = "1"}};
    module_imports_t imports = {.dependencies = deps, .count = 2};
    assert(validate_source_graph(&imports, search, NULL, &graph) == MODULE_GRAPH_ARGUMENT);
    assert(!graph);
    deps[1].name = "../invalid";
    assert(validate_source_graph(&imports, search, NULL, &graph) == MODULE_GRAPH_ARGUMENT);
    assert(!graph);
    imports.dependencies = NULL;
    assert(validate_source_graph(&imports, search, NULL, &graph) == MODULE_GRAPH_ARGUMENT);
    assert(!graph);
    imports.count = SIZE_MAX;
    assert(validate_source_graph(&imports, search, NULL, &graph) == MODULE_GRAPH_ARGUMENT);
    imports.count = 0;
    assert(validate_source_graph(&imports, search, NULL, NULL) == MODULE_GRAPH_ARGUMENT);
}

static void report(const module_validation_diagnostic_t *diagnostic, void *context) {
    if (diagnostic->check == MODULE_CHECK_INPUT)
        (*(size_t *)context)++;
}

static void metadata(const char *archive, const yyjson_doc *doc, void *context) {
    assert(archive && *archive);
    assert(yyjson_is_obj(yyjson_doc_get_root(doc)));
    (*(size_t *)context)++;
}

int main(int argc, char **argv) {
#ifdef _WIN32
    fs_error_t args_error = {0};
    assert(fs_arguments(&argc, &argv, &args_error));
    clear_fs_error(&args_error);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    contracts(NULL);
    if (argc > 1) {
        assert(argc == 4 || argc == 5);
        bool summary = argc == 5;
        module_search_t *search = NULL;
        module_search_error_t search_error = {0};
        assert(!prepare_module_search(argv[2], NULL, NULL, &search, &search_error));
        contracts(search);
        size_t checks = 0, metadata_count = 0;
        module_validation_options_t options = {.max_archive = SIZE_MAX,
                                               .max_expanded = SIZE_MAX,
                                               .report = report,
                                               .context = &checks,
                                               .metadata = metadata,
                                               .metadata_context = &metadata_count};
        module_graph_t *graph = NULL;
        module_sources_t *sources = NULL;
        module_imports_t *imports = NULL;
        module_sources_error_t source_error = {0};
        module_imports_error_t import_error = {0};
        int status;
        if (!strcmp(argv[1], "module") || !strcmp(argv[1], "module-null")) {
            status = validate_module_graph(argv[3],
                                           search,
                                           !strcmp(argv[1], "module-null") ? NULL : &options,
                                           &graph);
            void *bytes = NULL;
            size_t length = 0;
            if (read_fs_regular_file(argv[3], UINT32_MAX - 1, &bytes, &length, NULL)) {
                module_graph_t *memory = NULL;
                int memory_status = validate_module_graph_memory(bytes,
                                                                 length,
                                                                 "<unpublished-memory-root>",
                                                                 search,
                                                                 NULL,
                                                                 &memory);
                FREE(bytes);
                assert(memory_status == status);
                assert(module_graph_node_count(memory) == module_graph_node_count(graph));
                assert(module_graph_edge_count(memory) == module_graph_edge_count(graph));
                assert(module_graph_issue_count(memory) == module_graph_issue_count(graph));
                for (size_t i = 0; i < module_graph_issue_count(graph); i++)
                    assert(module_graph_issue(memory, i)->kind
                           == module_graph_issue(graph, i)->kind);
                free_module_graph(memory);
            }
        } else {
            assert(!module_sources_read(argv[3],
                                        !strcmp(argv[1], "gmod"),
                                        NULL,
                                        &sources,
                                        &source_error));
            status = module_imports_collect(sources, 0, &imports, &import_error);
            if (!status)
                status = validate_source_graph(imports, search, &options, &graph);
            else
                status += 100;
        }
        free_module_sources(sources);
        if (strcmp(argv[1], "module-null") && graph) {
            size_t valid_nodes = 0;
            for (size_t i = 0; i < module_graph_node_count(graph); i++)
                valid_nodes += module_graph_node(graph, i)->snapshot != NULL;
            assert(metadata_count >= valid_nodes && metadata_count <= checks);
        }
        free_module_imports(imports);
        free_module_search(search);
        clear_module_search_error(&search_error);
        clear_module_sources_error(&source_error);
        /* Graph views must outlive every input and search context. */
        yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
        yyjson_mut_val *root = yyjson_mut_obj(doc);
        yyjson_mut_doc_set_root(doc, root);
        yyjson_mut_obj_add_uint(doc, root, "status", status);
        yyjson_mut_obj_add_uint(doc, root, "checks", checks);
        yyjson_mut_obj_add_uint(doc, root, "node_count", module_graph_node_count(graph));
        yyjson_mut_obj_add_uint(doc, root, "edge_count", module_graph_edge_count(graph));
        yyjson_mut_val *nodes = yyjson_mut_arr(doc), *edges = yyjson_mut_arr(doc);
        yyjson_mut_val *issues = yyjson_mut_arr(doc);
        for (size_t i = 0; i < module_graph_node_count(graph) && !summary; i++) {
            const module_graph_node_t *node = module_graph_node(graph, i);
            yyjson_mut_val *obj = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_strcpy(doc, obj, "name", node->name ? node->name : "<source>");
            yyjson_mut_obj_add_strcpy(doc, obj, "version", node->version ? node->version : "");
            yyjson_mut_obj_add_strcpy(doc, obj, "hash", node->hash ? node->hash : "");
            yyjson_mut_obj_add_bool(doc, obj, "valid", node->snapshot != NULL);
            yyjson_mut_obj_add_uint(doc, obj, "search_status", node->search.status);
            yyjson_mut_obj_add_uint(doc, obj, "references_skipped", node->references_skipped);
            yyjson_mut_obj_add_uint(doc, obj, "attempts", node->search.count);
            size_t validation_count = module_graph_validation_count(graph, i);
            yyjson_mut_obj_add_uint(doc, obj, "validation_count", validation_count);
            assert(!module_graph_validation(graph, i, validation_count));
            for (size_t j = 0; j < validation_count; j++)
                assert(module_graph_validation(graph, i, j)->state != MODULE_CHECK_PASSED);
            if (node->snapshot) {
                yyjson_val *metadata = yyjson_doc_get_root(
                    goat_module_container_metadata(validated_module_container(node->snapshot)));
                yyjson_mut_obj_add_val(doc, obj, "metadata", yyjson_val_mut_copy(doc, metadata));
            }
            paths(doc, obj, graph, i, SIZE_MAX);
            yyjson_mut_arr_append(nodes, obj);
        }
        for (size_t i = 0; i < module_graph_edge_count(graph) && !summary; i++) {
            const module_graph_edge_t *edge = module_graph_edge(graph, i);
            yyjson_mut_val *obj = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, obj, "from", edge->from);
            yyjson_mut_obj_add_uint(doc, obj, "to", edge->to);
            yyjson_mut_obj_add_strcpy(doc, obj, "hash", edge->hash ? edge->hash : "");
            paths(doc, obj, graph, edge->to, i);
            yyjson_mut_arr_append(edges, obj);
        }
        for (size_t i = 0; i < module_graph_issue_count(graph); i++) {
            const module_graph_issue_t *issue = module_graph_issue(graph, i);
            yyjson_mut_val *obj = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, obj, "kind", issue->kind);
            yyjson_mut_obj_add_uint(doc, obj, "node", issue->node);
            yyjson_mut_obj_add_uint(doc, obj, "edge", issue->edge);
            yyjson_mut_obj_add_uint(doc, obj, "other_edge", issue->other_edge);
            yyjson_mut_obj_add_strncpy(doc,
                                       obj,
                                       "reference",
                                       issue->reference ? issue->reference : "",
                                       issue->reference_length);
            yyjson_mut_obj_add_uint(doc, obj, "reference_length", issue->reference_length);
            yyjson_mut_arr_append(issues, obj);
        }
        if (summary) {
            for (size_t i = 0; i < module_graph_node_count(graph); i++) {
                const module_graph_node_t *node = module_graph_node(graph, i);
                if (node->name && !strcmp(node->name, "zisolated")) {
                    paths_t p = {doc, yyjson_mut_arr(doc), false};
                    assert(module_graph_paths(graph, i, SIZE_MAX, path, &p));
                    assert(yyjson_mut_arr_size(p.paths) == 1);
                }
            }
        }
        assert(!module_graph_node(graph, module_graph_node_count(graph)));
        assert(!module_graph_validation_count(graph, module_graph_node_count(graph)));
        assert(!module_graph_validation(graph, module_graph_node_count(graph), 0));
        assert(!module_graph_edge(graph, module_graph_edge_count(graph)));
        assert(!module_graph_issue(graph, module_graph_issue_count(graph)));
        assert(!module_graph_paths(graph, module_graph_node_count(graph), SIZE_MAX, path, NULL));
        if (summary && module_graph_node_count(graph)) {
            paths_t p = {doc, yyjson_mut_arr(doc), true};
            assert(
                !module_graph_paths(graph, module_graph_node_count(graph) - 1, SIZE_MAX, path, &p));
            assert(yyjson_mut_arr_size(p.paths) == 1);
            yyjson_mut_obj_add_uint(doc,
                                    root,
                                    "last_path_length",
                                    yyjson_mut_arr_size(yyjson_mut_arr_get_first(p.paths)));
        }
        yyjson_mut_obj_add_val(doc, root, "nodes", nodes);
        yyjson_mut_obj_add_val(doc, root, "edges", edges);
        yyjson_mut_obj_add_val(doc, root, "issues", issues);
        if (graph && (!summary || !strcmp(argv[4], "diagnostics")))
            diagnostics(doc, root, graph);
        char *json = yyjson_mut_write(doc, 0, NULL);
        assert(json);
        puts(json);
        free(json);
        yyjson_mut_doc_free(doc);
        free_module_graph(graph);
    }
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    assert(!get_allocated_memory_size());
    return 0;
}
