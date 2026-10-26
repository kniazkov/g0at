/** @file module_snapshot.c
 * @copyright 2026 Ivan Kniazkov
 * @brief All-or-nothing snapshot preparation and memory-only dependency bindings.
 */
#include "module_snapshot.h"

#include "goat/module_identity.h"
#include "lib/allocate.h"
#include "model/process.h"

#include <stdlib.h>
#include <string.h>

struct module_snapshot {
    module_graph_t *graph;
    process_t *process;
    module_files_t **files;
    size_t count;
    size_t *offsets, *targets;
    void *native_state;
    void (*destroy_native)(void *);
};

void free_module_snapshot(module_snapshot_t *snapshot) {
    if (!snapshot)
        return;
    if (snapshot->process)
        destroy_process(snapshot->process);
    if (snapshot->destroy_native)
        snapshot->destroy_native(snapshot->native_state);
    free(snapshot->files);
    free(snapshot->offsets);
    free(snapshot->targets);
    free_module_graph(snapshot->graph);
    free(snapshot);
}

/* Consume a fully validated graph through the same all-or-nothing constructor
 * for file and memory roots. Diagnostic ownership on failure is identical. */
static module_graph_status_t create_snapshot(module_graph_t *graph,
                                             module_graph_status_t status,
                                             const module_search_t *search,
                                             module_snapshot_t **out,
                                             module_graph_t **diagnostics) {
    if (status) {
        if (diagnostics)
            *diagnostics = graph;
        else
            free_module_graph(graph);
        return status;
    }
    size_t count = module_graph_node_count(graph), edges = module_graph_edge_count(graph);
    if (count >= SIZE_MAX / sizeof(size_t) || count > SIZE_MAX / sizeof(module_files_t *)
        || edges > SIZE_MAX / sizeof(size_t)) {
        free_module_graph(graph);
        return MODULE_GRAPH_MEMORY;
    }
    module_snapshot_t *snapshot = calloc(1, sizeof(*snapshot));
    if (!snapshot) {
        free_module_graph(graph);
        return MODULE_GRAPH_MEMORY;
    }
    snapshot->graph = graph;
    snapshot->count = count;
    snapshot->files = calloc(count, sizeof(*snapshot->files));
    snapshot->offsets = calloc(count + 1, sizeof(*snapshot->offsets));
    snapshot->targets = edges ? malloc(edges * sizeof(*snapshot->targets)) : NULL;
    size_t *positions = calloc(count, sizeof(*positions));
    if (!snapshot->files || !snapshot->offsets || (edges && !snapshot->targets) || !positions) {
        free(positions);
        free_module_snapshot(snapshot);
        return MODULE_GRAPH_MEMORY;
    }
    for (size_t i = 0; i < edges; ++i)
        snapshot->offsets[module_graph_edge(graph, i)->from + 1]++;
    for (size_t i = 0; i < count; ++i)
        snapshot->offsets[i + 1] += snapshot->offsets[i];
    memcpy(positions, snapshot->offsets, count * sizeof(*positions));
    for (size_t i = 0; i < edges; ++i) {
        const module_graph_edge_t *edge = module_graph_edge(graph, i);
        snapshot->targets[positions[edge->from]++] = edge->to;
    }
    free(positions);
    /* No process/scopes exist until every reachable archive has passed. The
       graph now owns all bytes; runtime views never reopen diagnostic paths. */
    snapshot->process = create_process();
    for (size_t i = 0; i < count; ++i) {
        const validated_module_t *module = module_graph_node(graph, i)->snapshot;
        bool created = create_module_files_from_snapshot(
            snapshot->process,
            goat_module_container_metadata(validated_module_container(module)),
            validated_module_bytecode(module),
            snapshot->files + i);
        if (!created) {
            free_module_snapshot(snapshot);
            return MODULE_GRAPH_MEMORY;
        }
    }
    if (!prepare_process_launch(snapshot->process,
                                module_files_identity(snapshot->files[0]),
                                module_file_id(module_files_main(snapshot->files[0])),
                                module_search_root(search))) {
        free_module_snapshot(snapshot);
        return MODULE_GRAPH_ARGUMENT;
    }
    /* Resolve only edges of this consumer, never the global snapshot inventory.
       Validation already guarantees canonical literals and complete exact hashes. */
    for (size_t i = 0; i < count; ++i) {
        const bytecode_t *code = module_files_bytecode(snapshot->files[i]);
        for (size_t j = 0; j < code->instructions_count; ++j) {
            instruction_t instruction = code->instructions[j];
            if (instruction.opcode != MODULE_IMPORT)
                continue;
            const data_descriptor_t *descriptor = code->data_descriptors + instruction.arg1;
            goat_module_import_wide_t spec;
            if (!goat_module_parse_import_wide((const gchar_t *)(code->data + descriptor->offset),
                                               descriptor->count,
                                               &spec)
                || !spec.has_version) {
                free_module_snapshot(snapshot);
                return MODULE_GRAPH_INVALID;
            }
            char *name = encode_utf8_view((string_view_t){spec.name.data, spec.name.length}, NULL);
            char *version =
                encode_utf8_view((string_view_t){spec.version.data, spec.version.length}, NULL);
            size_t target = module_snapshot_import(snapshot, i, name, version);
            FREE(name);
            FREE(version);
            if (target == SIZE_MAX
                || !bind_module_files_import(snapshot->files[i],
                                             instruction.arg1,
                                             snapshot->files[target])) {
                free_module_snapshot(snapshot);
                return MODULE_GRAPH_INVALID;
            }
        }
    }
    *out = snapshot;
    return MODULE_GRAPH_OK;
}

module_graph_status_t load_module_snapshot(const char *archive,
                                           const module_search_t *search,
                                           const module_validation_options_t *options,
                                           module_snapshot_t **out,
                                           module_graph_t **diagnostics) {
    if (out)
        *out = NULL;
    if (diagnostics)
        *diagnostics = NULL;
    if (!out || !archive || !search)
        return MODULE_GRAPH_ARGUMENT;
    module_graph_t *graph = NULL;
    module_graph_status_t status = validate_module_graph(archive, search, options, &graph);
    return create_snapshot(graph, status, search, out, diagnostics);
}

module_graph_status_t load_module_snapshot_memory(const void *data,
                                                  size_t size,
                                                  const char *label,
                                                  const module_search_t *search,
                                                  const module_validation_options_t *options,
                                                  module_snapshot_t **out,
                                                  module_graph_t **diagnostics) {
    if (out)
        *out = NULL;
    if (diagnostics)
        *diagnostics = NULL;
    if (!out || !label || !search || (!data && size))
        return MODULE_GRAPH_ARGUMENT;
    module_graph_t *graph = NULL;
    module_graph_status_t status =
        validate_module_graph_memory(data, size, label, search, options, &graph);
    return create_snapshot(graph, status, search, out, diagnostics);
}

process_t *module_snapshot_process(module_snapshot_t *snapshot) {
    return snapshot ? snapshot->process : NULL;
}

size_t module_snapshot_count(const module_snapshot_t *snapshot) {
    return snapshot ? snapshot->count : 0;
}

const module_graph_t *module_snapshot_graph(const module_snapshot_t *snapshot) {
    return snapshot ? snapshot->graph : NULL;
}

const validated_module_t *module_snapshot_module(const module_snapshot_t *snapshot, size_t node) {
    return snapshot && node < snapshot->count ? module_graph_node(snapshot->graph, node)->snapshot
                                              : NULL;
}

module_files_t *module_snapshot_files(module_snapshot_t *snapshot, size_t node) {
    return snapshot && node < snapshot->count ? snapshot->files[node] : NULL;
}

size_t module_snapshot_find(const module_snapshot_t *snapshot,
                            const char *name,
                            const char *version,
                            const char *hash) {
    if (!snapshot || !name || !version || !hash)
        return SIZE_MAX;
    for (size_t i = 0; i < snapshot->count; ++i) {
        const module_graph_node_t *node = module_graph_node(snapshot->graph, i);
        if (!strcmp(node->name, name) && !strcmp(node->version, version)
            && !strcmp(node->hash, hash))
            return i;
    }
    return SIZE_MAX;
}

size_t module_snapshot_dependency_count(const module_snapshot_t *snapshot, size_t consumer) {
    return snapshot && consumer < snapshot->count
               ? snapshot->offsets[consumer + 1] - snapshot->offsets[consumer]
               : 0;
}

size_t
module_snapshot_dependency(const module_snapshot_t *snapshot, size_t consumer, size_t dependency) {
    return dependency < module_snapshot_dependency_count(snapshot, consumer)
               ? snapshot->targets[snapshot->offsets[consumer] + dependency]
               : SIZE_MAX;
}

size_t module_snapshot_import(const module_snapshot_t *snapshot,
                              size_t consumer,
                              const char *name,
                              const char *version) {
    if (!name || !version)
        return SIZE_MAX;
    size_t count = module_snapshot_dependency_count(snapshot, consumer);
    for (size_t i = 0; i < count; ++i) {
        size_t target = module_snapshot_dependency(snapshot, consumer, i);
        const module_graph_node_t *node = module_graph_node(snapshot->graph, target);
        if (!strcmp(name, node->name) && !strcmp(version, node->version))
            return target;
    }
    return SIZE_MAX;
}

bool set_module_snapshot_native_state(module_snapshot_t *snapshot,
                                      void *state,
                                      void (*destroy)(void *)) {
    if (!snapshot || !state || snapshot->native_state)
        return false;
    snapshot->native_state = state;
    snapshot->destroy_native = destroy;
    return true;
}

void *module_snapshot_native_state(module_snapshot_t *snapshot) {
    return snapshot ? snapshot->native_state : NULL;
}
