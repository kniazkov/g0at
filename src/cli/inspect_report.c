/** @file inspect_report.c
 * @copyright 2026 Ivan Kniazkov
 * @brief JSON report v1; presentation of shared validation, never execution.
 */
#include "inspect_report.h"

#include "lib/memory/allocate.h"
#include "vm/module_graph_diagnostics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    yyjson_mut_doc *doc;
    yyjson_mut_val *diagnostics;
    const module_graph_t *graph;
    bool failed;
} report_t;

static yyjson_mut_val *object(report_t *r) {
    yyjson_mut_val *value = yyjson_mut_obj(r->doc);
    r->failed |= value == NULL;
    return value;
}

static yyjson_mut_val *array(report_t *r) {
    yyjson_mut_val *value = yyjson_mut_arr(r->doc);
    r->failed |= value == NULL;
    return value;
}

static yyjson_mut_val *string(report_t *r, const char *s) {
    yyjson_mut_val *value = s ? yyjson_mut_strcpy(r->doc, s) : yyjson_mut_null(r->doc);
    r->failed |= value == NULL;
    return value;
}

static void add(report_t *r, yyjson_mut_val *o, const char *key, yyjson_mut_val *value) {
    r->failed |= !value || !yyjson_mut_obj_add(o, yyjson_mut_str(r->doc, key), value);
}

static void append(report_t *r, yyjson_mut_val *a, yyjson_mut_val *value) {
    r->failed |= !value || !yyjson_mut_arr_append(a, value);
}

static void text(report_t *r, yyjson_mut_val *o, const char *key, const char *value) {
    add(r, o, key, string(r, value));
}

static void number(report_t *r, yyjson_mut_val *o, const char *key, uint64_t value) {
    add(r, o, key, yyjson_mut_uint(r->doc, value));
}

static yyjson_mut_val *
diagnostic(report_t *r, const char *severity, const char *code, const char *message) {
    yyjson_mut_val *o = object(r);
    text(r, o, "severity", severity);
    text(r, o, "code", code);
    text(r, o, "message", message);
    add(r, o, "path", array(r));
    append(r, r->diagnostics, o);
    return o;
}

static const char *checks[] = {"input", "container", "bytecode", "native"};
static const char *container_checks[] =
    {"layout", "content", "json", "declarations", "native_hash", "main_hash"};

static const char *validation_code(const module_validation_diagnostic_t *v) {
    if (v->state == MODULE_CHECK_SKIPPED)
        return "CHECK_SKIPPED";
    if (v->check == MODULE_CHECK_INPUT) {
        if (v->detail == MODULE_VALIDATION_MEMORY)
            return "OUT_OF_MEMORY";
        if (v->detail == MODULE_VALIDATION_LIMIT)
            return "RESOURCE_LIMIT";
        return "ARCHIVE_IO";
    }
    if (v->check == MODULE_CHECK_BYTECODE)
        return "BYTECODE_INVALID";
    if (v->check == MODULE_CHECK_NATIVE)
        return "NATIVE_INVALID";
    switch (v->container.status) {
        case GOAT_CONTAINER_MEMORY:
            return "OUT_OF_MEMORY";
        case GOAT_CONTAINER_LIMIT:
            return "RESOURCE_LIMIT";
        case GOAT_CONTAINER_ZIP:
            return "ZIP_INVALID";
        case GOAT_CONTAINER_JSON:
            return "JSON_INVALID";
        case GOAT_CONTAINER_MAIN_HASH:
            return "MAIN_HASH_MISMATCH";
        case GOAT_CONTAINER_NATIVE_HASH:
            return "NATIVE_HASH_MISMATCH";
        default:
            return "CONTAINER_INVALID";
    }
}

static const char *search_code(module_search_status_t status) {
    switch (status) {
        case MODULE_SEARCH_NOT_FOUND:
            return "DEPENDENCY_NOT_FOUND";
        case MODULE_SEARCH_INVALID_MODULE:
            return "MODULE_INVALID";
        case MODULE_SEARCH_IDENTITY:
            return "IDENTITY_MISMATCH";
        case MODULE_SEARCH_HASH:
            return "DEPENDENCY_HASH_MISMATCH";
        case MODULE_SEARCH_ARGUMENT:
            return "INVALID_ARGUMENT";
        default:
            return "ARCHIVE_IO";
    }
}

/* Preserve arbitrary ZIP names without requiring valid UTF-8. */
static yyjson_mut_val *base64(report_t *r, const void *bytes, size_t size) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t length = allocation_size_multiply(size / 3 + (size % 3 != 0), 4);
    char *encoded = ALLOC(allocation_size_add(length, 1));
    const unsigned char *data = bytes;
    size_t j = 0;
    for (size_t i = 0; i < size; i += 3) {
        uint32_t n = (uint32_t)data[i] << 16;
        if (size - i > 1)
            n |= (uint32_t)data[i + 1] << 8;
        if (size - i > 2)
            n |= data[i + 2];
        encoded[j++] = alphabet[n >> 18];
        encoded[j++] = alphabet[(n >> 12) & 63];
        encoded[j++] = size - i > 1 ? alphabet[(n >> 6) & 63] : '=';
        encoded[j++] = size - i > 2 ? alphabet[n & 63] : '=';
    }
    encoded[j] = 0;
    yyjson_mut_val *value = string(r, encoded);
    FREE(encoded);
    return value;
}

static bool graph_diagnostic(const module_graph_diagnostic_t *d, void *context) {
    report_t *r = context;
    const module_graph_node_t *node = module_graph_node(r->graph, d->node);
    const module_graph_edge_t *edge = module_graph_edge(r->graph, d->edge);
    const module_validation_diagnostic_t *v = d->validation;
    const char *code, *message;
    if (v) {
        code = validation_code(v);
        message =
            v->state == MODULE_CHECK_SKIPPED
                ? "Check not performed because its prerequisite failed."
            : v->check == MODULE_CHECK_CONTAINER ? goat_container_status_string(v->container.status)
            : v->check == MODULE_CHECK_BYTECODE  ? module_bytecode_status_string(v->bytecode.status)
            : v->check == MODULE_CHECK_NATIVE    ? goat_native_status_string(v->native.status)
                                                 : "Cannot read or retain archive input.";
    } else {
        static const char *codes[] = {NULL,
                                      "DEPENDENCY_HASH_MISMATCH",
                                      "DEPENDENCY_HASH_CONFLICT",
                                      "DEPENDENCY_CYCLE",
                                      "REFERENCE_INVALID"};
        static const char *messages[] = {"Module lookup or validation failed.",
                                         "Pinned main hash differs.",
                                         "Dependency requests pin conflicting hashes.",
                                         "Dependency cycle detected.",
                                         "External function reference is invalid."};
        code = d->kind == MODULE_GRAPH_SEARCH ? search_code(node->search.status) : codes[d->kind];
        message = messages[d->kind];
    }
    yyjson_mut_val *o =
        diagnostic(r, v && v->state == MODULE_CHECK_SKIPPED ? "skipped" : "error", code, message);
    text(r, o, "archive", node->archive);
    yyjson_mut_val *path = yyjson_mut_obj_get(o, "path");
    for (size_t i = 0; i < d->path_count; ++i) {
        const module_graph_edge_t *e = module_graph_edge(r->graph, d->path[i]);
        const module_graph_node_t *n = module_graph_node(r->graph, e->to);
        yyjson_mut_val *step = object(r);
        text(r, step, "name", n->name);
        text(r, step, "version", n->version);
        text(r, step, "expectedHash", e->hash);
        append(r, path, step);
    }
    if (edge)
        text(r, o, "expectedHash", edge->hash);
    if (node->hash)
        text(r, o, "actualHash", node->hash);
    if (d->reference)
        add(r, o, "reference", yyjson_mut_strncpy(r->doc, d->reference, d->reference_length));
    if (v) {
        text(r, o, "check", checks[v->check]);
        if (v->state == MODULE_CHECK_SKIPPED) {
            yyjson_mut_val *p = object(r);
            text(r, p, "check", checks[v->prerequisite]);
            text(r, p, "containerCheck", container_checks[v->container_prerequisite]);
            add(r, o, "prerequisite", p);
        }
        if (v->entry.size)
            add(r, o, "entryBase64", base64(r, v->entry.data, v->entry.size));
        if (v->field)
            add(r, o, "field", yyjson_mut_strncpy(r->doc, v->field, v->field_length));
        if (v->index != SIZE_MAX)
            number(r, o, "index", v->index);
        if (v->system_error)
            add(r, o, "systemError", yyjson_mut_sint(r->doc, v->system_error));
    } else if (node->search.filesystem.native_code)
        number(r, o, "systemError", node->search.filesystem.native_code);
    return !r->failed;
}

static yyjson_mut_val *version(report_t *r, const goat_native_version *v) {
    if (!v->known)
        return yyjson_mut_null(r->doc);
    yyjson_mut_val *a = array(r);
    for (size_t i = 0; i < 4; ++i)
        append(r, a, yyjson_mut_uint(r->doc, v->components[i]));
    return a;
}

static yyjson_mut_val *
target_json(report_t *r, const goat_native_environment *t, bool explicit_target) {
    if (!t)
        return yyjson_mut_null(r->doc);
    const char *os[] = {"unknown", "linux", "windows"};
    const char *arch[] = {"unknown", "x86", "x86_64", "aarch64"};
    const char *runtime[] = {"unknown", "glibc", "musl", "msvcrt", "ucrt"};
    yyjson_mut_val *o = object(r), *cpu = array(r);
    text(r, o, "source", explicit_target ? "explicit" : "current");
    text(r, o, "os", os[t->os]);
    text(r, o, "architecture", arch[t->architecture]);
    text(r, o, "runtime", runtime[t->runtime]);
    number(r, o, "bits", t->bits);
    add(r, o, "littleEndian", yyjson_mut_bool(r->doc, t->little_endian));
    add(r, o, "osVersion", version(r, &t->os_version));
    add(r, o, "runtimeVersion", version(r, &t->runtime_version));
    for (uint64_t bit = 1; bit <= GOAT_CPU_SVE2; bit <<= 1)
        if (t->cpu_features & bit)
            append(r, cpu, string(r, goat_native_cpu_feature_name(bit)));
    add(r, o, "cpuFeatures", cpu);
    return o;
}

static int key_compare(const void *left, const void *right) {
    yyjson_val *a = *(yyjson_val *const *)left, *b = *(yyjson_val *const *)right;
    size_t alen = yyjson_get_len(a), blen = yyjson_get_len(b);
    int result = memcmp(yyjson_get_str(a), yyjson_get_str(b), alen < blen ? alen : blen);
    return result ? result : (alen > blen) - (alen < blen);
}

static yyjson_mut_val *exports_json(report_t *r, yyjson_val *metadata) {
    yyjson_val *source = yyjson_obj_get(metadata, "externals"), *key, *entry;
    size_t count = yyjson_obj_size(source), index, maximum;
    yyjson_val **keys = ALLOC(allocation_size_multiply(count, sizeof(*keys)));
    yyjson_obj_foreach(source, index, maximum, key, entry) keys[index] = key;
    qsort(keys, count, sizeof(*keys), key_compare);
    yyjson_mut_val *a = array(r);
    for (index = 0; index < count; ++index) {
        key = keys[index];
        entry = yyjson_obj_iter_get_val(key);
        yyjson_mut_val *o = object(r);
        add(r, o, "name", yyjson_val_mut_copy(r->doc, key));
        add(r, o, "mutable", yyjson_val_mut_copy(r->doc, yyjson_obj_get(entry, "mutable")));
        yyjson_val *type = yyjson_obj_get(entry, "type");
        if (type)
            add(r, o, "type", yyjson_val_mut_copy(r->doc, type));
        append(r, a, o);
    }
    FREE(keys);
    return a;
}

static yyjson_mut_val *reason_json(report_t *r, const char *code, const char *message) {
    yyjson_mut_val *reason = object(r);
    text(r, reason, "code", code);
    text(r, reason, "message", message);
    return reason;
}

static const char *probe_code(const inspect_probe_result_t *probe) {
    if (probe->abi_error)
        return "PROBE_ABI_INVALID";
    if (probe->infrastructure_error)
        return "PROBE_INFRASTRUCTURE_FAILED";
    if (probe->state == INSPECT_PROBE_CRASHED)
        return "PROBE_CRASHED";
    if (probe->state == INSPECT_PROBE_TIMEOUT)
        return "PROBE_TIMEOUT";
    if (probe->stage == INSPECT_PROBE_LOAD)
        return "PROBE_LOAD_FAILED";
    if (probe->stage == INSPECT_PROBE_UNLOAD)
        return "PROBE_UNLOAD_FAILED";
    return "PROBE_FAILED";
}

static const char *cleanup_message(const inspect_probe_reports_t *probes) {
    return probes->cleanup.status == CLEANUP_TIMEOUT
               ? "Cleanup confirmation timed out; the janitor may still be working. "
                 "Manual deletion is unsafe until it has finished."
               : "Temporary resource cleanup was not confirmed.";
}

static const char *cleanup_code(const inspect_probe_reports_t *probes) {
    return probes->cleanup.status == CLEANUP_TIMEOUT ? "CLEANUP_TIMEOUT" : "CLEANUP_FAILED";
}

static yyjson_mut_val *stream_json(report_t *r, const child_output_t *output) {
    yyjson_mut_val *stream = object(r);
    add(r, stream, "dataBase64", base64(r, output->data, output->size));
    add(r, stream, "truncated", yyjson_mut_bool(r->doc, output->truncated));
    return stream;
}

typedef struct {
    report_t *report;
    const module_graph_node_t *node;
    const inspect_probe_result_t *probe;
    const char *code, *message;
} probe_path_context_t;

static bool probe_path_diagnostic(const size_t *edges, size_t count, void *context) {
    probe_path_context_t *c = context;
    report_t *r = c->report;
    yyjson_mut_val *d =
        diagnostic(r,
                   c->probe->abi_error || c->probe->infrastructure_error ? "error" : "warning",
                   c->code,
                   c->message);
    text(r, d, "archive", c->node->archive);
    text(r, d, "check", "native");
    if (c->probe->child.system_error)
        number(r, d, "systemError", c->probe->child.system_error);
    yyjson_mut_val *path = yyjson_mut_obj_get(d, "path");
    for (size_t i = 0; i < count; ++i) {
        const module_graph_edge_t *edge = module_graph_edge(r->graph, edges[i]);
        const module_graph_node_t *node = module_graph_node(r->graph, edge->to);
        yyjson_mut_val *step = object(r);
        text(r, step, "name", node->name);
        text(r, step, "version", node->version);
        text(r, step, "expectedHash", edge->hash);
        append(r, path, step);
    }
    return !r->failed;
}

static yyjson_mut_val *checked_loading(report_t *r,
                                       const module_graph_node_t *node,
                                       const inspect_probe_result_t *probe,
                                       const inspect_probe_reports_t *probes) {
    yyjson_mut_val *loading = object(r), *trial = object(r), *cleanup = object(r);
    yyjson_mut_val *capture = object(r), *diagnostics = array(r), *completed = array(r);
    text(r, loading, "state", "checked");
    text(r, trial, "state", inspect_probe_state_name(probe->state));
    text(r, trial, "stage", inspect_probe_stage_name(probe->stage));
    add(r,
        trial,
        "exitCode",
        probe->child.status == CHILD_EXITED ? yyjson_mut_sint(r->doc, probe->child.exit_code)
                                            : yyjson_mut_null(r->doc));
    add(r,
        trial,
        "signal",
        probe->child.status == CHILD_SIGNALED ? yyjson_mut_sint(r->doc, probe->child.signal_number)
                                              : yyjson_mut_null(r->doc));
    add(r, capture, "stdout", stream_json(r, &probe->child.out));
    add(r, capture, "stderr", stream_json(r, &probe->child.err));
    add(r, trial, "capture", capture);
    if (probe->load_completed)
        append(r, completed, string(r, "load"));
    if (probe->descriptor_completed)
        append(r, completed, string(r, "descriptor"));
    if (probe->symbols_completed)
        append(r, completed, string(r, "symbols"));
    if (probe->unload_completed)
        append(r, completed, string(r, "unload"));
    add(r, trial, "completedStages", completed);
    if (probe->state != INSPECT_PROBE_PASSED || probe->abi_error || probe->infrastructure_error) {
        const char *code = probe_code(probe);
        const char *message =
            probe->diagnostic[0] ? probe->diagnostic : "Native loading trial failed.";
        append(r, diagnostics, reason_json(r, code, message));
        probe_path_context_t context = {r, node, probe, code, message};
        size_t graph_index = 0;
        while (graph_index < module_graph_node_count(r->graph)
               && module_graph_node(r->graph, graph_index) != node)
            ++graph_index;
        r->failed |=
            !module_graph_paths(r->graph, graph_index, SIZE_MAX, probe_path_diagnostic, &context);
    }
    add(r, trial, "diagnostics", diagnostics);
    text(r, cleanup, "state", probes->cleanup.status == CLEANUP_OK ? "passed" : "failed");
    yyjson_mut_val *cleanup_diagnostics = array(r);
    if (probes->cleanup.status != CLEANUP_OK) {
        append(r,
               cleanup_diagnostics,
               reason_json(r, cleanup_code(probes), cleanup_message(probes)));
        if (probes->temporary_directory)
            text(r, cleanup, "temporaryDirectory", probes->temporary_directory);
    }
    add(r, cleanup, "diagnostics", cleanup_diagnostics);
    /* A failed confirmation does not establish that any path still exists. */
    add(r, cleanup, "remainingPaths", array(r));
    add(r, loading, "probe", trial);
    add(r, loading, "cleanup", cleanup);
    return loading;
}

static yyjson_mut_val *native_json(report_t *r,
                                   const module_graph_node_t *node,
                                   yyjson_val *metadata,
                                   const goat_native_environment *target,
                                   const inspect_probe_reports_t *probes,
                                   size_t node_index) {
    yyjson_val *source = yyjson_obj_get(metadata, "native"), *variant;
    size_t i, max, matches = 0;
    const uint32_t abi = 1;
    yyjson_mut_val *a = array(r);
    if (node->snapshot) {
        yyjson_arr_foreach(source, i, max, variant) {
            goat_native_compatibility_result result;
            if (goat_module_native_compatibility(variant, target, &abi, 1, &result)
                == GOAT_NATIVE_OK)
                ++matches;
        }
    }
    yyjson_arr_foreach(source, i, max, variant) {
        goat_native_compatibility_result result;
        goat_native_status status =
            node->snapshot ? goat_module_native_compatibility(variant, target, &abi, 1, &result)
                           : GOAT_NATIVE_INVALID_METADATA;
        yyjson_mut_val *o = object(r), *compatibility = object(r), *loading;
        add(r, o, "library", yyjson_val_mut_copy(r->doc, yyjson_obj_get(variant, "library")));
        text(r,
             compatibility,
             "state",
             !node->snapshot            ? "not_checked"
             : status == GOAT_NATIVE_OK ? "compatible"
                                        : "incompatible");
        add(r, o, "compatibility", compatibility);
        if (probes && node_index < probes->count && probes->results[node_index]
            && probes->selected[node_index] == i) {
            loading = checked_loading(r, node, probes->results[node_index], probes);
        } else {
            const char *code, *message;
            if (!node->snapshot || !probes || !probes->graph_valid) {
                code = "VALIDATION_FAILED";
                message =
                    "Archive or dependency graph validation failed; no native code was loaded.";
            } else if (!probes->local_target) {
                code = "FOREIGN_TARGET";
                message = "The selected target differs from the current environment; loading was "
                          "not attempted.";
            } else if (status != GOAT_NATIVE_OK) {
                code = "NO_COMPATIBLE_VARIANT";
                message = "Variant is not compatible with the selected target.";
            } else if (matches > 1) {
                code = "AMBIGUOUS_NATIVE_VARIANT";
                message = "Multiple native variants match; no variant was loaded.";
            } else {
                code = "NOT_SELECTED";
                message = "This variant was not selected for loading.";
            }
            loading = object(r);
            text(r, loading, "state", "not_checked");
            add(r, loading, "reason", reason_json(r, code, message));
        }
        add(r, o, "loading", loading);
        append(r, a, o);
    }
    if (node->snapshot && (matches == 0 || matches > 1)) {
        yyjson_mut_val *d =
            diagnostic(r,
                       "warning",
                       matches ? "AMBIGUOUS_NATIVE_VARIANT" : "NO_NATIVE_VARIANT",
                       matches ? "Multiple native variants match; VM remains available."
                               : "No native variant matches; VM remains available.");
        text(r, d, "archive", node->archive);
    }
    return a;
}

/* Allocation-free failure report, emitted only before any output was written. */
static bool report_failed(void) {
    fputs("{\"reportVersion\":1,\"success\":false,\"exitCode\":1,"
          "\"input\":null,\"target\":null,\"modules\":[],\"diagnostics\":[{"
          "\"severity\":\"error\",\"code\":\"REPORT_FAILED\","
          "\"message\":\"Could not serialize the inspection report.\",\"path\":[]}]}\n",
          stdout);
    fflush(stdout);
    return false;
}

bool write_inspect_json(const char *input,
                        const goat_native_environment *target,
                        bool explicit_target,
                        const module_graph_t *graph,
                        const module_graph_node_t *const *nodes,
                        size_t count,
                        const metadata_record_t *metadata,
                        int exit_code,
                        const char *code,
                        const char *message,
                        const char *information_kind,
                        const char *information_text,
                        const inspect_probe_reports_t *probes) {
    report_t r = {.doc = yyjson_mut_doc_new(NULL), .graph = graph};
    if (!r.doc)
        return report_failed();
    yyjson_mut_val *root = object(&r), *modules = array(&r);
    r.diagnostics = array(&r);
    yyjson_mut_doc_set_root(r.doc, root);
    number(&r, root, "reportVersion", 1);
    add(&r, root, "success", yyjson_mut_bool(r.doc, exit_code == 0));
    number(&r, root, "exitCode", exit_code);
    text(&r, root, "input", input);
    add(&r, root, "target", target_json(&r, target, explicit_target));
    add(&r, root, "modules", modules);
    add(&r, root, "diagnostics", r.diagnostics);
    if (information_kind) {
        yyjson_mut_val *info = object(&r);
        text(&r, info, "kind", information_kind);
        text(&r, info, "text", information_text);
        add(&r, root, "information", info);
    }
    if (code)
        diagnostic(&r, "error", code, message);
    if (graph)
        r.failed |= !report_module_graph_diagnostics(graph, graph_diagnostic, &r);
    for (size_t i = 0; i < count; ++i) {
        const module_graph_node_t *node = nodes[i];
        const metadata_record_t *record = metadata;
        while (record && strcmp(record->archive, node->archive))
            record = record->next;
        yyjson_val *meta = record ? yyjson_doc_get_root(record->doc) : NULL;
        yyjson_mut_val *o = object(&r);
        text(&r, o, "archive", node->archive);
        text(&r, o, "validation", node->snapshot ? "passed" : "failed");
        add(&r, o, "metadata", meta ? yyjson_val_mut_copy(r.doc, meta) : yyjson_mut_null(r.doc));
        add(&r, o, "exports", meta ? exports_json(&r, meta) : yyjson_mut_null(r.doc));
        add(&r,
            o,
            "native",
            meta ? native_json(&r, node, meta, target, probes, i) : yyjson_mut_null(r.doc));
        append(&r, modules, o);
        if (node->references_skipped) {
            yyjson_mut_val *d = diagnostic(
                &r,
                "skipped",
                "REFERENCE_SKIPPED",
                "External references were not checked because graph prerequisites failed.");
            text(&r, d, "archive", node->archive);
            text(&r, d, "check", "references");
        }
    }
    if (probes && probes->cleanup.status != CLEANUP_OK) {
        yyjson_mut_val *d = diagnostic(&r, "error", cleanup_code(probes), cleanup_message(probes));
        if (probes->cleanup.native_code)
            number(&r, d, "systemError", probes->cleanup.native_code);
    }
    size_t length = 0;
    char *json = r.failed ? NULL : yyjson_mut_write(r.doc, 0, &length);
    if (!json) {
        yyjson_mut_doc_free(r.doc);
        return report_failed();
    }
    bool ok = fwrite(json, 1, length, stdout) == length && putchar('\n') != EOF && !fflush(stdout);
    free(json);
    yyjson_mut_doc_free(r.doc);
    return ok;
}
