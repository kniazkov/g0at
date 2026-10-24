/** @file module_graph.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Cached iterative graph discovery, checks and diagnostic paths.
 */
#include "module_graph.h"

#include "goat/module_identity.h"
#include "lib/allocate.h"
#include "module/module_json_internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    module_graph_node_t view;
    size_t first, count;
} node_t;

struct module_graph {
    node_t *nodes;
    size_t node_count, node_capacity;
    module_graph_edge_t *edges;
    size_t edge_count, edge_capacity;
    module_graph_issue_t *issues;
    size_t issue_count, issue_capacity;
    size_t *table;
    size_t table_capacity;
};

typedef struct {
    const char *name, *version, *hash;
} request_t;

static char *copy_text(const char *text) {
    if (!text)
        return NULL;
    size_t size = strlen(text) + 1;
    char *copy = ALLOC(size);
    memcpy(copy, text, size);
    return copy;
}

/* The core allocation policy is fatal OOM, including impossible address-space
 * growth. This does not introduce an artificial graph size or depth limit. */
static void *grow(void *old, size_t *capacity, size_t size) {
    if (*capacity > SIZE_MAX / 2 || (*capacity ? *capacity * 2 : 16) > SIZE_MAX / size)
        abort();
    size_t next = *capacity ? *capacity * 2 : 16;
    void *result = CALLOC(next * size);
    if (*capacity)
        memcpy(result, old, *capacity * size);
    FREE(old);
    *capacity = next;
    return result;
}

static yyjson_val *metadata(const validated_module_t *snapshot) {
    return yyjson_doc_get_root(
        goat_module_container_metadata(validated_module_container(snapshot)));
}

static size_t pair_hash(const char *name, const char *version) {
    size_t hash = 5381;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++)
        hash = hash * 33 + *p;
    hash = hash * 33;
    for (const unsigned char *p = (const unsigned char *)version; *p; p++)
        hash = hash * 33 + *p;
    return hash;
}

static void insert(module_graph_t *graph, size_t index) {
    const module_graph_node_t *node = &graph->nodes[index].view;
    if (!node->name)
        return;
    size_t slot = pair_hash(node->name, node->version) & (graph->table_capacity - 1);
    while (graph->table[slot])
        slot = (slot + 1) & (graph->table_capacity - 1);
    graph->table[slot] = index + 1;
}

static size_t find(const module_graph_t *graph, const char *name, const char *version) {
    if (!graph->table_capacity)
        return SIZE_MAX;
    size_t slot = pair_hash(name, version) & (graph->table_capacity - 1);
    while (graph->table[slot]) {
        size_t index = graph->table[slot] - 1;
        const module_graph_node_t *node = &graph->nodes[index].view;
        if (!strcmp(node->name, name) && !strcmp(node->version, version))
            return index;
        slot = (slot + 1) & (graph->table_capacity - 1);
    }
    return SIZE_MAX;
}

static size_t add_node(module_graph_t *graph, const char *name, const char *version) {
    if (graph->node_count == graph->node_capacity)
        graph->nodes = grow(graph->nodes, &graph->node_capacity, sizeof(*graph->nodes));
    size_t index = graph->node_count++;
    graph->nodes[index].view.name = copy_text(name);
    graph->nodes[index].view.version = copy_text(version);
    if (!graph->table_capacity || graph->node_count > graph->table_capacity / 2) {
        if (graph->table_capacity > SIZE_MAX / 2 / sizeof(*graph->table))
            abort();
        graph->table_capacity = graph->table_capacity ? graph->table_capacity * 2 : 32;
        FREE(graph->table);
        graph->table = CALLOC(graph->table_capacity * sizeof(*graph->table));
        for (size_t i = 0; i <= index; i++)
            insert(graph, i);
    } else {
        insert(graph, index);
    }
    return index;
}

static void issue(module_graph_t *graph,
                  module_graph_issue_kind_t kind,
                  size_t node,
                  size_t edge,
                  size_t other,
                  const char *reference) {
    if (graph->issue_count == graph->issue_capacity)
        graph->issues = grow(graph->issues, &graph->issue_capacity, sizeof(*graph->issues));
    graph->issues[graph->issue_count++] =
        (module_graph_issue_t){kind, node, edge, other, reference};
}

static int compare_requests(const void *left, const void *right) {
    const request_t *a = left, *b = right;
    int order = strcmp(a->name, b->name);
    return order ? order : strcmp(a->version, b->version);
}

static void add_edges(module_graph_t *graph, size_t from, request_t *requests, size_t count) {
    if (count > 1)
        qsort(requests, count, sizeof(*requests), compare_requests);
    graph->nodes[from].first = graph->edge_count;
    graph->nodes[from].count = count;
    for (size_t i = 0; i < count; i++) {
        size_t to = find(graph, requests[i].name, requests[i].version);
        if (to == SIZE_MAX)
            to = add_node(graph, requests[i].name, requests[i].version);
        if (graph->edge_count == graph->edge_capacity)
            graph->edges = grow(graph->edges, &graph->edge_capacity, sizeof(*graph->edges));
        graph->edges[graph->edge_count++] =
            (module_graph_edge_t){from, to, copy_text(requests[i].hash)};
    }
}

static void expand(module_graph_t *graph, size_t index) {
    yyjson_val *root = metadata(graph->nodes[index].view.snapshot);
    yyjson_val *array = yyjson_obj_get(root, "dependencies");
    size_t count = yyjson_arr_size(array);
    if (count > SIZE_MAX / sizeof(request_t))
        abort();
    request_t *requests = ALLOC(count * sizeof(*requests));
    for (size_t i = 0; i < count; i++) {
        yyjson_val *record = yyjson_arr_get(array, i);
        requests[i] = (request_t){yyjson_get_str(yyjson_obj_get(record, "name")),
                                  yyjson_get_str(yyjson_obj_get(record, "version")),
                                  yyjson_get_str(yyjson_obj_get(record, "hash"))};
    }
    add_edges(graph, index, requests, count);
    FREE(requests);
}

static void check_hashes(module_graph_t *graph) {
    size_t *first = ALLOC(graph->node_count * sizeof(*first));
    for (size_t i = 0; i < graph->node_count; i++)
        first[i] = SIZE_MAX;
    for (size_t i = 0; i < graph->edge_count; i++) {
        module_graph_edge_t *edge = &graph->edges[i];
        const char *actual = graph->nodes[edge->to].view.hash;
        if (!edge->hash)
            edge->hash = copy_text(actual);
        if (!edge->hash)
            continue;
        if (actual && strcmp(edge->hash, actual))
            issue(graph, MODULE_GRAPH_HASH, edge->to, i, SIZE_MAX, NULL);
        size_t prior = first[edge->to];
        if (prior == SIZE_MAX)
            first[edge->to] = i;
        else if (strcmp(edge->hash, graph->edges[prior].hash))
            issue(graph, MODULE_GRAPH_CONFLICT, edge->to, i, prior, NULL);
    }
    FREE(first);
}

typedef struct {
    size_t node, next;
} frame_t;

static void check_cycles(module_graph_t *graph) {
    unsigned char *color = CALLOC(graph->node_count);
    frame_t *stack = ALLOC(graph->node_count * sizeof(*stack));
    size_t depth = 1;
    stack[0] = (frame_t){0, 0};
    color[0] = 1;
    while (depth) {
        frame_t *frame = &stack[depth - 1];
        const node_t *node = &graph->nodes[frame->node];
        if (frame->next == node->count) {
            color[frame->node] = 2;
            depth--;
            continue;
        }
        size_t index = node->first + frame->next++;
        size_t to = graph->edges[index].to;
        if (color[to] == 1)
            issue(graph, MODULE_GRAPH_CYCLE, to, index, SIZE_MAX, NULL);
        else if (!color[to]) {
            color[to] = 1;
            stack[depth++] = (frame_t){to, 0};
        }
    }
    FREE(stack);
    FREE(color);
}

static bool has_references(const node_t *node) {
    if (!node->view.snapshot)
        return false;
    yyjson_obj_iter iter =
        yyjson_obj_iter_with(yyjson_obj_get(metadata(node->view.snapshot), "externals"));
    yyjson_val *key;
    while ((key = yyjson_obj_iter_next(&iter)))
        if (yyjson_obj_get(yyjson_obj_iter_get_val(key), "functionRef"))
            return true;
    return false;
}

/* One reachability walk per owner with references, not per graph node/function.
 * Invalid edges cannot grant authority; independent valid branches still can.
 * A missing target behind a failed edge is unknown, not a spurious new error. */
static void check_references(module_graph_t *graph) {
    bool *seen = CALLOC(graph->node_count * sizeof(*seen));
    size_t *queue = ALLOC(graph->node_count * sizeof(*queue));
    for (size_t owner = 0; owner < graph->node_count; owner++) {
        node_t *node = &graph->nodes[owner];
        if (!has_references(node))
            continue;
        memset(seen, 0, graph->node_count * sizeof(*seen));
        seen[owner] = true;
        size_t head = 0, tail = 1;
        queue[0] = owner;
        bool incomplete = false;
        while (head < tail) {
            const node_t *current = &graph->nodes[queue[head++]];
            for (size_t i = 0; i < current->count; i++) {
                const module_graph_edge_t *edge = &graph->edges[current->first + i];
                const module_graph_node_t *target = &graph->nodes[edge->to].view;
                if (!target->snapshot || !edge->hash || strcmp(target->hash, edge->hash)) {
                    incomplete = true;
                    continue;
                }
                if (!seen[edge->to]) {
                    seen[edge->to] = true;
                    queue[tail++] = edge->to;
                }
            }
        }
        yyjson_obj_iter iter =
            yyjson_obj_iter_with(yyjson_obj_get(metadata(node->view.snapshot), "externals"));
        yyjson_val *key;
        while ((key = yyjson_obj_iter_next(&iter))) {
            yyjson_val *ref = yyjson_obj_get(yyjson_obj_iter_get_val(key), "functionRef");
            if (!ref)
                continue;
            size_t target = find(graph,
                                 yyjson_get_str(yyjson_obj_get(ref, "module")),
                                 yyjson_get_str(yyjson_obj_get(ref, "version")));
            if (target == SIZE_MAX || !seen[target]) {
                if (incomplete)
                    node->view.references_skipped++;
                else
                    issue(graph,
                          MODULE_GRAPH_REFERENCE,
                          owner,
                          SIZE_MAX,
                          SIZE_MAX,
                          yyjson_get_str(key));
                continue;
            }
            bool found = false;
            if (target != owner) {
                yyjson_val *functions =
                    yyjson_obj_get(metadata(graph->nodes[target].view.snapshot), "functions");
                int64_t wanted = 0;
                goat_json_integer(yyjson_obj_get(ref, "functionId"), &wanted);
                for (size_t i = 0; i < yyjson_arr_size(functions); i++) {
                    yyjson_val *function = yyjson_arr_get(functions, i);
                    int64_t id = 0;
                    goat_json_integer(yyjson_obj_get(function, "functionId"), &id);
                    if (id == wanted) {
                        found = true;
                        break;
                    }
                }
            }
            if (!found)
                issue(graph,
                      MODULE_GRAPH_REFERENCE,
                      owner,
                      SIZE_MAX,
                      SIZE_MAX,
                      yyjson_get_str(key));
        }
    }
    FREE(queue);
    FREE(seen);
}

static module_graph_status_t finish(module_graph_t *graph,
                                    const module_search_t *search,
                                    const module_validation_options_t *options,
                                    module_graph_t **out) {
    for (size_t i = 1; i < graph->node_count; i++) {
        module_graph_node_t *node = &graph->nodes[i].view;
        validated_module_t *snapshot = NULL;
        module_search_status_t status =
            find_module(search, node->name, node->version, NULL, options, &snapshot, &node->search);
        if (status != MODULE_SEARCH_OK) {
            if (node->search.count)
                node->archive = copy_text(node->search.attempted[node->search.count - 1]);
            issue(graph, MODULE_GRAPH_SEARCH, i, SIZE_MAX, SIZE_MAX, NULL);
            continue;
        }
        node->snapshot = snapshot;
        node->archive = copy_text(validated_module_archive(snapshot));
        node->hash = yyjson_get_str(yyjson_obj_get(metadata(snapshot), "mainHash"));
        expand(graph, i);
    }
    check_hashes(graph);
    check_cycles(graph);
    check_references(graph);
    *out = graph;
    return graph->issue_count ? MODULE_GRAPH_INVALID : MODULE_GRAPH_OK;
}

module_graph_status_t validate_module_graph(const char *archive,
                                            const module_search_t *search,
                                            const module_validation_options_t *options,
                                            module_graph_t **out) {
    if (out)
        *out = NULL;
    if (!archive || !*archive || !search || !out)
        return MODULE_GRAPH_ARGUMENT;
    module_graph_t *graph = CALLOC(sizeof(*graph));
    add_node(graph, NULL, NULL);
    module_graph_node_t *node = &graph->nodes[0].view;
    node->archive = copy_text(archive);
    node->search.attempted = ALLOC(sizeof(*node->search.attempted));
    node->search.attempted[0] = copy_text(archive);
    node->search.count = 1;
    void *data = NULL;
    size_t size = 0, limit = UINT32_MAX - 1;
    if (options && options->max_archive < limit)
        limit = options->max_archive;
    validated_module_t *snapshot = NULL;
    if (!read_fs_regular_file(archive, limit, &data, &size, &node->search.filesystem)) {
        node->search.status = MODULE_SEARCH_IO;
    } else {
        node->search.validation = validate_module(data, size, archive, options, &snapshot);
        if (node->search.validation.status != MODULE_VALIDATION_OK)
            node->search.status = MODULE_SEARCH_INVALID_MODULE;
    }
    FREE(data);
    if (node->search.status) {
        issue(graph, MODULE_GRAPH_SEARCH, 0, SIZE_MAX, SIZE_MAX, NULL);
    } else {
        yyjson_val *root = metadata(snapshot);
        node->name = copy_text(yyjson_get_str(yyjson_obj_get(root, "name")));
        node->version = copy_text(yyjson_get_str(yyjson_obj_get(root, "version")));
        node->snapshot = snapshot;
        node->hash = yyjson_get_str(yyjson_obj_get(root, "mainHash"));
        insert(graph, 0);
        expand(graph, 0);
    }
    return finish(graph, search, options, out);
}

module_graph_status_t validate_source_graph(const module_imports_t *imports,
                                            const module_search_t *search,
                                            const module_validation_options_t *options,
                                            module_graph_t **out) {
    if (out)
        *out = NULL;
    if (!imports || !search || !out || (imports->count && !imports->dependencies)
        || imports->count > SIZE_MAX / sizeof(request_t))
        return MODULE_GRAPH_ARGUMENT;
    request_t *requests = ALLOC(imports->count * sizeof(*requests));
    bool valid = true;
    for (size_t i = 0; i < imports->count; i++) {
        const module_import_dependency_t *dep = &imports->dependencies[i];
        if (!dep->name || !dep->version || !goat_module_name_valid(dep->name, strlen(dep->name))
            || !goat_module_version_valid(dep->version, strlen(dep->version))) {
            valid = false;
            break;
        }
        requests[i] = (request_t){dep->name, dep->version, NULL};
    }
    if (valid && imports->count > 1) {
        qsort(requests, imports->count, sizeof(*requests), compare_requests);
        for (size_t i = 1; i < imports->count; i++)
            if (!compare_requests(&requests[i - 1], &requests[i]))
                valid = false;
    }
    if (!valid) {
        FREE(requests);
        return MODULE_GRAPH_ARGUMENT;
    }
    module_graph_t *graph = CALLOC(sizeof(*graph));
    add_node(graph, NULL, NULL);
    graph->nodes[0].view.archive = copy_text(module_search_root(search));
    add_edges(graph, 0, requests, imports->count);
    FREE(requests);
    return finish(graph, search, options, out);
}

void free_module_graph(module_graph_t *graph) {
    if (!graph)
        return;
    for (size_t i = 0; i < graph->node_count; i++) {
        module_graph_node_t *node = &graph->nodes[i].view;
        FREE((void *)node->name);
        FREE((void *)node->version);
        FREE((void *)node->archive);
        free_validated_module((validated_module_t *)node->snapshot);
        clear_module_search_error(&node->search);
    }
    for (size_t i = 0; i < graph->edge_count; i++)
        FREE((void *)graph->edges[i].hash);
    FREE(graph->nodes);
    FREE(graph->edges);
    FREE(graph->issues);
    FREE(graph->table);
    FREE(graph);
}

size_t module_graph_node_count(const module_graph_t *graph) {
    return graph ? graph->node_count : 0;
}

size_t module_graph_edge_count(const module_graph_t *graph) {
    return graph ? graph->edge_count : 0;
}

size_t module_graph_issue_count(const module_graph_t *graph) {
    return graph ? graph->issue_count : 0;
}

const module_graph_node_t *module_graph_node(const module_graph_t *graph, size_t index) {
    return graph && index < graph->node_count ? &graph->nodes[index].view : NULL;
}

const module_graph_edge_t *module_graph_edge(const module_graph_t *graph, size_t index) {
    return graph && index < graph->edge_count ? &graph->edges[index] : NULL;
}

const module_graph_issue_t *module_graph_issue(const module_graph_t *graph, size_t index) {
    return graph && index < graph->issue_count ? &graph->issues[index] : NULL;
}

/* Prune siblings that cannot reach the requested endpoint before enumerating
 * paths. A layered diamond elsewhere must not cause exponential work merely
 * to report one unrelated path. Reverse adjacency is temporary O(V + E). */
static bool *ancestors(const module_graph_t *graph, size_t goal) {
    size_t *first = ALLOC(graph->node_count * sizeof(*first));
    size_t *next = ALLOC(graph->edge_count * sizeof(*next));
    size_t *queue = ALLOC(graph->node_count * sizeof(*queue));
    bool *reachable = CALLOC(graph->node_count * sizeof(*reachable));
    for (size_t i = 0; i < graph->node_count; i++)
        first[i] = SIZE_MAX;
    for (size_t i = 0; i < graph->edge_count; i++) {
        size_t to = graph->edges[i].to;
        next[i] = first[to];
        first[to] = i;
    }
    size_t head = 0, tail = 1;
    queue[0] = goal;
    reachable[goal] = true;
    while (head < tail) {
        for (size_t edge = first[queue[head++]]; edge != SIZE_MAX; edge = next[edge]) {
            size_t from = graph->edges[edge].from;
            if (!reachable[from]) {
                reachable[from] = true;
                queue[tail++] = from;
            }
        }
    }
    FREE(queue);
    FREE(next);
    FREE(first);
    return reachable;
}

bool module_graph_paths(const module_graph_t *graph,
                        size_t node,
                        size_t final_edge,
                        module_graph_path_report_t report,
                        void *context) {
    if (!graph || node >= graph->node_count || !report
        || (final_edge != SIZE_MAX
            && (final_edge >= graph->edge_count || graph->edges[final_edge].to != node)))
        return false;
    size_t goal = final_edge == SIZE_MAX ? node : graph->edges[final_edge].from;
    bool *reachable = ancestors(graph, goal);
    frame_t *stack = ALLOC(graph->node_count * sizeof(*stack));
    size_t *path = ALLOC(graph->node_count * sizeof(*path));
    bool *seen = CALLOC(graph->node_count * sizeof(*seen));
    size_t depth = 1;
    stack[0] = (frame_t){0, 0};
    seen[0] = true;
    bool complete = true;
    while (depth) {
        frame_t *frame = &stack[depth - 1];
        const node_t *current = &graph->nodes[frame->node];
        if (frame->node == goal) {
            if (final_edge != SIZE_MAX)
                path[depth - 1] = final_edge;
            if (!report(path, depth - 1 + (final_edge != SIZE_MAX), context)) {
                complete = false;
                break;
            }
            seen[frame->node] = false;
            depth--;
        } else if (frame->next == current->count) {
            seen[frame->node] = false;
            depth--;
        } else {
            size_t edge = current->first + frame->next++;
            size_t to = graph->edges[edge].to;
            if (seen[to] || !reachable[to])
                continue;
            path[depth - 1] = edge;
            seen[to] = true;
            stack[depth++] = (frame_t){to, 0};
        }
    }
    FREE(reachable);
    FREE(seen);
    FREE(path);
    FREE(stack);
    return complete;
}
