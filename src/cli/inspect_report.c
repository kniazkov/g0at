/** @file inspect_report.c
 * @copyright 2026 Ivan Kniazkov
 * @brief JSON report v1; presentation of shared validation, never execution.
 */
#include "inspect_report.h"

#include "lib/allocate.h"
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

static yyjson_mut_val *native_json(report_t *r,
                                   const module_graph_node_t *node,
                                   yyjson_val *metadata,
                                   const goat_native_environment *target) {
    yyjson_val *source = yyjson_obj_get(metadata, "native"), *variant;
    size_t i, max, matches = 0;
    yyjson_mut_val *a = array(r);
    yyjson_arr_foreach(source, i, max, variant) {
        goat_native_compatibility_result result;
        const uint32_t abi = 1;
        goat_native_status status =
            node->snapshot ? goat_module_native_compatibility(variant, target, &abi, 1, &result)
                           : GOAT_NATIVE_INVALID_METADATA;
        if (status == GOAT_NATIVE_OK)
            ++matches;
        yyjson_mut_val *o = object(r), *compatibility = object(r), *loading = object(r),
                       *reason = object(r);
        add(r, o, "library", yyjson_val_mut_copy(r->doc, yyjson_obj_get(variant, "library")));
        text(r,
             compatibility,
             "state",
             !node->snapshot            ? "not_checked"
             : status == GOAT_NATIVE_OK ? "compatible"
                                        : "incompatible");
        add(r, o, "compatibility", compatibility);
        text(r, loading, "state", "not_checked");
        text(r,
             reason,
             "code",
             !node->snapshot            ? "VALIDATION_FAILED"
             : status != GOAT_NATIVE_OK ? "NO_COMPATIBLE_VARIANT"
                                        : "PROBE_NOT_IMPLEMENTED");
        text(r,
             reason,
             "message",
             !node->snapshot            ? "Archive validation failed."
             : status != GOAT_NATIVE_OK ? "Variant is not compatible with the selected target."
                                        : "Isolated loading has not been performed.");
        add(r, loading, "reason", reason);
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
                        const char *information_text) {
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
        add(&r, o, "native", meta ? native_json(&r, node, meta, target) : yyjson_mut_null(r.doc));
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
