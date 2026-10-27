/* Copyright 2026 Ivan Kniazkov
 * Distributed under the MIT license; see LICENSE.txt. */
#include "yyjson.h"

#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef GOAT_SCHEMA_ROOT
#    define GOAT_SCHEMA_ROOT "."
#endif

typedef struct buffer {
    char *data;
    size_t size, capacity;
} buffer;

static void fail(const char *message) {
    fprintf(stderr, "module-schema: %s\n", message);
    exit(EXIT_FAILURE);
}

static void append(buffer *b, const char *format, ...) {
    va_list args, copy;
    va_start(args, format);
    va_copy(copy, args);
    int count = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (count < 0 || (size_t)count > SIZE_MAX - b->size - 1)
        fail("output too large");
    size_t need = b->size + (size_t)count + 1;
    if (need > b->capacity) {
        size_t capacity = need > SIZE_MAX / 2 ? need : need * 2;
        char *data = realloc(b->data, capacity);
        if (!data)
            fail("out of memory");
        b->data = data;
        b->capacity = capacity;
    }
    vsnprintf(b->data + b->size, b->capacity - b->size, format, args);
    va_end(args);
    b->size += (size_t)count;
}

static void quote(buffer *b, const char *s, bool c_string) {
    append(b, "\"");
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        switch (*p) {
            case '"':
                append(b, "\\\"");
                break;
            case '\\':
                append(b, "\\\\");
                break;
            case '\b':
                append(b, "\\b");
                break;
            case '\f':
                append(b, "\\f");
                break;
            case '\n':
                append(b, "\\n");
                break;
            case '\r':
                append(b, "\\r");
                break;
            case '\t':
                append(b, "\\t");
                break;
            default:
                if (c_string && (*p < 32 || *p >= 127))
                    append(b, "\\%03o", *p);
                else if (*p < 32)
                    append(b, "\\u%04x", *p);
                else
                    append(b, "%c", *p);
        }
    }
    append(b, "\"");
}

static void indent(buffer *b, unsigned level) {
    for (unsigned i = 0; i < level; i++)
        append(b, "  ");
}

static void json(buffer *b, yyjson_val *v, unsigned level) {
    if (yyjson_is_obj(v)) {
        append(b, "{");
        size_t i, count;
        yyjson_val *key, *value;
        yyjson_obj_foreach(v, i, count, key, value) {
            append(b, "%s\n", i ? "," : "");
            indent(b, level + 1);
            quote(b, yyjson_get_str(key), false);
            append(b, ": ");
            json(b, value, level + 1);
        }
        if (yyjson_obj_size(v)) {
            append(b, "\n");
            indent(b, level);
        }
        append(b, "}");
    } else if (yyjson_is_arr(v)) {
        append(b, "[");
        size_t i, count;
        yyjson_val *value;
        yyjson_arr_foreach(v, i, count, value) {
            append(b, "%s\n", i ? "," : "");
            indent(b, level + 1);
            json(b, value, level + 1);
        }
        if (yyjson_arr_size(v)) {
            append(b, "\n");
            indent(b, level);
        }
        append(b, "]");
    } else if (yyjson_is_str(v))
        quote(b, yyjson_get_str(v), false);
    else if (yyjson_is_bool(v))
        append(b, "%s", yyjson_get_bool(v) ? "true" : "false");
    else if (yyjson_is_sint(v))
        append(b, "%" PRId64, yyjson_get_sint(v));
    else if (yyjson_is_uint(v))
        append(b, "%" PRIu64, yyjson_get_uint(v));
    else
        fail("unsupported JSON value");
}

/* Validate the whole document, including objects not reached as schema nodes.
 * Reject duplicate keys and embedded NUL rather than silently truncating them. */
static void validate_json(yyjson_val *v, unsigned depth) {
    if (depth > 128)
        fail("schema nesting exceeds 128");
    if (yyjson_is_obj(v)) {
        size_t i, count;
        yyjson_val *key, *value;
        yyjson_obj_foreach(v, i, count, key, value) {
            validate_json(key, depth + 1);
            if (yyjson_obj_get(v, yyjson_get_str(key)) != value)
                fail("duplicate object key");
            validate_json(value, depth + 1);
        }
    } else if (yyjson_is_arr(v)) {
        size_t i, count;
        yyjson_val *value;
        yyjson_arr_foreach(v, i, count, value) validate_json(value, depth + 1);
    } else if (yyjson_is_str(v)) {
        if (strlen(yyjson_get_str(v)) != yyjson_get_len(v))
            fail("embedded NUL in string");
    } else if (!yyjson_is_int(v) && !yyjson_is_bool(v))
        fail("only integer, string and boolean scalar tokens are supported");
    if (yyjson_is_uint(v) && yyjson_get_uint(v) > INT64_MAX)
        fail("integer outside signed 64-bit range");
}

static const char *type_name(yyjson_val *v) {
    if (!yyjson_is_str(v))
        fail("schema type must be a string");
    const char *s = yyjson_get_str(v);
    const char *types[] = {"null", "boolean", "integer", "string", "array", "object"};
    const char *names[] =
        {"JSON_NULL", "JSON_BOOL", "JSON_INTEGER", "JSON_STRING", "JSON_ARRAY", "JSON_OBJECT"};
    for (size_t i = 0; i < 6; i++)
        if (!strcmp(s, types[i]))
            return names[i];
    fail("unsupported schema type");
    return NULL;
}

static const char *pattern_name(yyjson_val *v) {
    if (!yyjson_is_str(v))
        fail("pattern must be a string");
    const char *patterns[] = {"^[a-z0-9](?:[a-z0-9._-]*[a-z0-9_-])?$",
                              "^[0-9a-f]{64}$",
                              "^(host|[a-z0-9][a-z0-9_-]*(\\+[a-z0-9_-]+)*)$",
                              "^native/[a-z0-9][a-z0-9._-]*\\.(dll|so)$",
                              "^goat_fn_[1-9][0-9]*_v1$"};
    const char *names[] = {"PATTERN_NAME",
                           "PATTERN_HASH",
                           "PATTERN_TARGET",
                           "PATTERN_LIBRARY",
                           "PATTERN_SYMBOL"};
    for (size_t i = 0; i < 5; i++)
        if (!strcmp(yyjson_get_str(v), patterns[i]))
            return names[i];
    fail("unsupported schema pattern");
    return NULL;
}

static bool scalar(yyjson_val *v) {
    return yyjson_is_str(v) || yyjson_is_int(v) || yyjson_is_bool(v);
}

static yyjson_val *defs;
static yyjson_val **nodes;
static size_t node_count, node_capacity;

static bool has_type(yyjson_val *n, const char *name) {
    yyjson_val *type = yyjson_obj_get(n, "type");
    if (yyjson_is_str(type))
        return !strcmp(yyjson_get_str(type), name);
    size_t i, count;
    yyjson_val *v;
    yyjson_arr_foreach(type, i, count, v) if (yyjson_is_str(v)
                                              && !strcmp(yyjson_get_str(v), name)) return true;
    return false;
}

static yyjson_val *resolve(yyjson_val *n) {
    size_t remaining = yyjson_obj_size(defs) + 1;
    yyjson_val *ref;
    while ((ref = yyjson_obj_get(n, "$ref"))) {
        if (!remaining--)
            fail("cyclic schema reference");
        if (!yyjson_is_str(ref) || strncmp(yyjson_get_str(ref), "#/$defs/", 8))
            fail("unsupported schema reference");
        n = yyjson_obj_get(defs, yyjson_get_str(ref) + 8);
        if (!yyjson_is_obj(n))
            fail("unknown schema reference");
    }
    return n;
}

static bool only_strings(yyjson_val *n) {
    n = resolve(n);
    yyjson_val *type = yyjson_obj_get(n, "type");
    if (yyjson_is_str(type) && !strcmp(yyjson_get_str(type), "string"))
        return true;
    if (yyjson_is_str(yyjson_obj_get(n, "const")))
        return true;
    yyjson_val *values = yyjson_obj_get(n, "enum");
    if (!yyjson_is_arr(values) || !yyjson_arr_size(values))
        return false;
    size_t i, count;
    yyjson_val *v;
    yyjson_arr_foreach(values, i, count, v) if (!yyjson_is_str(v)) return false;
    return true;
}

static void validate_schema(yyjson_val *n) {
    if (!yyjson_is_obj(n) || !yyjson_obj_size(n))
        fail("schema must be a nonempty object");
    size_t i, count;
    yyjson_val *key, *v;
    yyjson_obj_foreach(n, i, count, key, v) {
        const char *k = yyjson_get_str(key);
        if (!strcmp(k, "$ref")) {
            if (!yyjson_is_str(v) || strncmp(yyjson_get_str(v), "#/$defs/", 8)
                || !yyjson_obj_get(defs, yyjson_get_str(v) + 8))
                fail("unknown schema reference");
        } else if (!strcmp(k, "type")) {
            if (yyjson_is_arr(v)) {
                if (!yyjson_arr_size(v))
                    fail("empty type union");
                size_t j, len;
                yyjson_val *t;
                yyjson_arr_foreach(v, j, len, t) type_name(t);
            } else
                type_name(v);
        } else if (!strcmp(k, "properties")) {
            if (!yyjson_is_obj(v) || !yyjson_obj_size(v))
                fail("properties must be a nonempty object");
        } else if (!strcmp(k, "required")) {
            if (!yyjson_is_arr(v))
                fail("required must be an array");
            size_t j, len;
            yyjson_val *t;
            yyjson_arr_foreach(v, j, len, t) {
                if (!yyjson_is_str(t)
                    || !yyjson_obj_get(yyjson_obj_get(n, "properties"), yyjson_get_str(t)))
                    fail("required names an unknown property");
            }
        } else if (!strcmp(k, "items") || !strcmp(k, "propertyNames")) {
            if (!yyjson_is_obj(v))
                fail("child schema must be an object");
        } else if (!strcmp(k, "additionalProperties")) {
            if (!yyjson_is_obj(v) && !yyjson_is_bool(v))
                fail("additionalProperties must be an object or boolean");
        } else if (!strcmp(k, "minimum") || !strcmp(k, "maximum")) {
            if (!yyjson_is_int(v))
                fail("bound must be an integer");
        } else if (!strcmp(k, "minItems") || !strcmp(k, "maxItems") || !strcmp(k, "minLength")
                   || !strcmp(k, "maxLength")) {
            if (!yyjson_is_int(v) || yyjson_get_sint(v) < 0 || yyjson_get_uint(v) > UINT32_MAX)
                fail("length/count must be a nonnegative 32-bit integer");
        } else if (!strcmp(k, "uniqueItems")) {
            if (!yyjson_is_bool(v))
                fail("uniqueItems must be boolean");
        } else if (!strcmp(k, "pattern"))
            pattern_name(v);
        else if (!strcmp(k, "const")) {
            if (!scalar(v))
                fail("unsupported const value");
        } else if (!strcmp(k, "enum")) {
            if (!yyjson_is_arr(v) || !yyjson_arr_size(v))
                fail("enum must be nonempty");
            size_t j, len;
            yyjson_val *t;
            yyjson_arr_foreach(v, j, len, t) if (!scalar(t)) fail("unsupported enum value");
        } else
            fail("unknown schema keyword");
    }
    if (yyjson_obj_get(n, "enum") && yyjson_obj_get(n, "const"))
        fail("enum and const together are unsupported");
    if (yyjson_obj_get(n, "$ref")) {
        if (yyjson_obj_size(n) != 1)
            fail("reference siblings are unsupported");
        resolve(n);
    } else if (!yyjson_obj_get(n, "type") && !yyjson_obj_get(n, "enum")
               && !yyjson_obj_get(n, "const"))
        fail("schema requires type, enum, const or reference");
    if (has_type(n, "array") && !yyjson_obj_get(n, "items"))
        fail("array schema requires items");
    if ((yyjson_obj_get(n, "maxItems") && yyjson_get_sint(yyjson_obj_get(n, "maxItems")) == 0)
        || (yyjson_obj_get(n, "maxLength") && yyjson_get_sint(yyjson_obj_get(n, "maxLength")) == 0))
        fail("zero maximum length/count is unsupported");
    if (yyjson_is_true(yyjson_obj_get(n, "uniqueItems"))
        && !only_strings(yyjson_obj_get(n, "items")))
        fail("uniqueItems requires string items");
    const char *lower[] = {"minimum", "minItems", "minLength"};
    const char *upper[] = {"maximum", "maxItems", "maxLength"};
    for (size_t j = 0; j < 3; j++) {
        yyjson_val *lo = yyjson_obj_get(n, lower[j]), *hi = yyjson_obj_get(n, upper[j]);
        if (lo && hi && yyjson_get_sint(lo) > yyjson_get_sint(hi))
            fail("minimum exceeds maximum");
    }
}

static size_t add_node(yyjson_val *n) {
    validate_schema(n);
    if (node_count == node_capacity) {
        node_capacity = node_capacity ? node_capacity * 2 : 256;
        yyjson_val **new_nodes = realloc(nodes, node_capacity * sizeof(*nodes));
        if (!new_nodes)
            fail("out of memory");
        nodes = new_nodes;
    }
    size_t result = node_count++;
    nodes[result] = n;
    size_t i, count;
    yyjson_val *key, *v;
    yyjson_obj_foreach(yyjson_obj_get(n, "properties"), i, count, key, v) add_node(v);
    const char *children[] = {"items", "propertyNames", "additionalProperties"};
    for (i = 0; i < 3; i++) {
        v = yyjson_obj_get(n, children[i]);
        if (yyjson_is_obj(v))
            add_node(v);
    }
    return result;
}

static size_t node_index(yyjson_val *n) {
    for (size_t i = 0; i < node_count; i++)
        if (nodes[i] == n)
            return i;
    fail("internal missing node");
    return 0;
}

static bool required(yyjson_val *n, const char *name) {
    size_t i, count;
    yyjson_val *v;
    yyjson_arr_foreach(yyjson_obj_get(n, "required"),
                       i,
                       count,
                       v) if (!strcmp(name, yyjson_get_str(v))) return true;
    return false;
}

static void literal(buffer *out, yyjson_val *v) {
    append(out,
           "    {JSON_%s, ",
           yyjson_is_str(v)    ? "STRING"
           : yyjson_is_bool(v) ? "BOOL"
                               : "INTEGER");
    if (yyjson_is_str(v))
        quote(out, yyjson_get_str(v), true);
    else {
        buffer value = {0};
        json(&value, v, 0);
        quote(out, value.data, true);
        free(value.data);
    }
    append(out, "},\n");
}

static void field(buffer *b, bool *first, const char *format, ...) {
    append(b, "%s", *first ? "" : ", ");
    *first = false;
    va_list args;
    va_start(args, format);
    char small[256];
    int n = vsnprintf(small, sizeof(small), format, args);
    va_end(args);
    if (n < 0 || (size_t)n >= sizeof(small))
        fail("internal field too long");
    append(b, "%s", small);
}

static void header(buffer *out, const size_t roots[4]) {
    /* Keep the historical provenance line to make migration byte-verifiable. */
    append(out,
           "/* Generated by scripts/generate_module_json_schema.py. Do not edit. */\n/* "
           "clang-format off */\n#pragma once\n\nstatic const json_schema schemas[%zu];\n\n",
           node_count);
    for (size_t i = 0; i < node_count; i++) {
        yyjson_val *n = nodes[i], *p = yyjson_obj_get(n, "properties");
        if (p) {
            append(out, "static const json_field fields_%zu[] = {\n", i);
            size_t j, count;
            yyjson_val *key, *value;
            yyjson_obj_foreach(p, j, count, key, value) {
                append(out, "    {");
                quote(out, yyjson_get_str(key), true);
                append(out,
                       ", &schemas[%zu], %s},\n",
                       node_index(value),
                       required(n, yyjson_get_str(key)) ? "true" : "false");
            }
            append(out, "};\n");
        }
        yyjson_val *e = yyjson_obj_get(n, "enum"), *c = yyjson_obj_get(n, "const");
        if (e || c) {
            append(out, "static const json_literal literals_%zu[] = {\n", i);
            if (e) {
                size_t j, count;
                yyjson_val *v;
                yyjson_arr_foreach(e, j, count, v) literal(out, v);
            } else
                literal(out, c);
            append(out, "};\n");
        }
    }
    append(out, "static const json_schema schemas[%zu] = {\n", node_count);
    for (size_t i = 0; i < node_count; i++) {
        yyjson_val *n = nodes[i], *v;
        bool first = true;
        append(out, "    {");
        v = yyjson_obj_get(n, "$ref");
        if (v)
            field(out,
                  &first,
                  ".reference = &schemas[%zu]",
                  node_index(yyjson_obj_get(defs, yyjson_get_str(v) + 8)));
        v = yyjson_obj_get(n, "type");
        if (v) {
            field(out, &first, ".types = ");
            if (yyjson_is_str(v))
                append(out, "%s", type_name(v));
            else {
                size_t j, count;
                yyjson_val *t;
                yyjson_arr_foreach(v, j, count, t)
                    append(out, "%s%s", j ? " | " : "", type_name(t));
            }
        }
        v = yyjson_obj_get(n, "properties");
        if (v) {
            field(out, &first, ".fields = fields_%zu", i);
            field(out, &first, ".field_count = %zu", yyjson_obj_size(v));
        }
        const char *keys[] = {"items", "propertyNames", "additionalProperties"};
        const char *names[] = {"items", "keys", "extra"};
        for (size_t j = 0; j < 3; j++) {
            v = yyjson_obj_get(n, keys[j]);
            if (yyjson_is_obj(v))
                field(out, &first, ".%s = &schemas[%zu]", names[j], node_index(v));
        }
        v = yyjson_obj_get(n, "additionalProperties");
        if (yyjson_is_false(v))
            field(out, &first, ".closed = true");
        yyjson_val *lo = yyjson_obj_get(n, "minimum"), *hi = yyjson_obj_get(n, "maximum");
        if (lo || hi) {
            int64_t minimum = lo ? yyjson_get_sint(lo) : INT64_MIN;
            int64_t maximum = hi ? yyjson_get_sint(hi) : INT64_MAX;
            field(out, &first, ".bounded = true");
            if (minimum == INT64_MIN)
                field(out, &first, ".minimum = INT64_MIN");
            else
                field(out, &first, ".minimum = INT64_C(%" PRId64 ")", minimum);
            if (maximum == INT64_MIN)
                field(out, &first, ".maximum = INT64_MIN");
            else
                field(out, &first, ".maximum = INT64_C(%" PRId64 ")", maximum);
        }
        const char *counts[] = {"minItems", "maxItems", "minLength", "maxLength"};
        const char *members[] = {"min_count", "max_count", "min_length", "max_length"};
        for (size_t j = 0; j < 4; j++) {
            v = yyjson_obj_get(n, counts[j]);
            if (v)
                field(out, &first, ".%s = %" PRId64, members[j], yyjson_get_sint(v));
        }
        if (yyjson_is_true(yyjson_obj_get(n, "uniqueItems")))
            field(out, &first, ".unique = true");
        v = yyjson_obj_get(n, "pattern");
        if (v)
            field(out, &first, ".pattern = %s", pattern_name(v));
        yyjson_val *e = yyjson_obj_get(n, "enum"), *c = yyjson_obj_get(n, "const");
        if (e || c) {
            field(out, &first, ".literals = literals_%zu", i);
            field(out, &first, ".literal_count = %zu", e ? yyjson_arr_size(e) : 1);
        }
        if (yyjson_is_int(c) && yyjson_get_sint(c) == 1)
            field(out, &first, ".format_version = true");
        append(out, "},\n");
    }
    append(out,
           "};\n\n#define GMOD_SCHEMA (&schemas[%zu])\n#define METADATA_SCHEMA "
           "(&schemas[%zu])\n#define TARGET_SCHEMA (&schemas[%zu])\n#define NATIVE_SCHEMA "
           "(&schemas[%zu])\n",
           roots[0],
           roots[1],
           roots[2],
           roots[3]);
}

static char *path_join(const char *root, const char *suffix) {
    buffer b = {0};
    append(&b, "%s/%s", root, suffix);
    return b.data;
}

/* Read text in binary mode, accepting checkout CRLF without rewriting it. */
static buffer read_file(const char *path) {
    buffer b = {0};
    FILE *f = fopen(path, "rb");
    if (!f)
        return b;
    int c;
    while ((c = fgetc(f)) != EOF)
        append(&b, "%c", c);
    if (ferror(f)) {
        fclose(f);
        fail("failed to read file");
    }
    fclose(f);
    return b;
}

static bool matches(const buffer *actual, const buffer *expected) {
    size_t a = 0, e = 0;
    while (a < actual->size && e < expected->size) {
        if (actual->data[a] == '\r' && a + 1 < actual->size && actual->data[a + 1] == '\n')
            a++;
        if (actual->data[a++] != expected->data[e++])
            return false;
    }
    return a == actual->size && e == expected->size;
}

int main(int argc, char **argv) {
    const char *root = GOAT_SCHEMA_ROOT, *source = NULL;
    bool check = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--check"))
            check = true;
        else if (!strcmp(argv[i], "--root") && i + 1 < argc)
            root = argv[++i];
        else if (!strcmp(argv[i], "--source") && i + 1 < argc)
            source = argv[++i];
        else if (!strcmp(argv[i], "--help")) {
            puts("module_schema_generator [--root REPOSITORY] [--source JSON] [--check]");
            return 0;
        } else
            fail("invalid arguments (use --help)");
    }
    char *default_source = path_join(root, "docs/module-format-v1/schema-source.json");
    if (!source)
        source = default_source;
    yyjson_read_err error;
    yyjson_doc *doc = yyjson_read_file(source, 0, NULL, &error);
    if (!doc) {
        fprintf(stderr, "module-schema: %s: %s\n", source, error.msg);
        free(default_source);
        return 1;
    }
    yyjson_val *top = yyjson_doc_get_root(doc);
    validate_json(top, 0);
    defs = yyjson_obj_get(top, "DEFS");
    yyjson_val *root_schemas = yyjson_obj_get(top, "ROOTS");
    if (!yyjson_is_obj(top) || yyjson_obj_size(top) != 2 || !yyjson_is_obj(defs)
        || !yyjson_is_obj(root_schemas) || yyjson_obj_size(root_schemas) != 4)
        fail("source must contain DEFS and four ROOTS");
    size_t i, count;
    yyjson_val *key, *v;
    yyjson_obj_foreach(defs, i, count, key, v) {
        if (strchr(yyjson_get_str(key), '/') || strchr(yyjson_get_str(key), '~'))
            fail("escaped definition names are unsupported");
        add_node(v);
    }
    const char *kinds[] = {"gmod", "module", "target", "native"};
    size_t roots[4];
    for (i = 0; i < 4; i++)
        roots[i] = add_node(yyjson_obj_get(root_schemas, kinds[i]));
    buffer outputs[5] = {{0}};
    char *paths[5];
    paths[0] = path_join(root, "src/module/module_json_schema.h");
    header(&outputs[0], roots);
    for (i = 0; i < 4; i++) {
        buffer relative = {0};
        append(&relative, "docs/module-format-v1/%s.schema.json", kinds[i]);
        paths[i + 1] = path_join(root, relative.data);
        free(relative.data);
        buffer *out = &outputs[i + 1];
        append(out,
               "{\n  \"$schema\": \"https://json-schema.org/draft/2020-12/schema\",\n  \"$id\": "
               "\"https://github.com/kniazkov/g0at/docs/module-format-v1/%s.schema.json\"",
               kinds[i]);
        size_t j, len;
        yyjson_val *k, *value;
        yyjson_obj_foreach(yyjson_obj_get(root_schemas, kinds[i]), j, len, k, value) {
            append(out, ",\n  ");
            quote(out, yyjson_get_str(k), false);
            append(out, ": ");
            json(out, value, 1);
        }
        append(out, ",\n  \"$defs\": ");
        json(out, defs, 1);
        append(out, "\n}\n");
    }
    int result = 0;
    for (i = 0; i < 5; i++) {
        if (check) {
            buffer actual = read_file(paths[i]);
            if (!actual.data || !matches(&actual, &outputs[i])) {
                fprintf(stderr, "module-schema: stale or missing %s\n", paths[i]);
                result = 1;
            }
            free(actual.data);
        } else {
            FILE *f = fopen(paths[i], "wb");
            if (!f) {
                fprintf(stderr, "module-schema: cannot write %s\n", paths[i]);
                result = 1;
            } else {
                if (fwrite(outputs[i].data, 1, outputs[i].size, f) != outputs[i].size)
                    result = 1;
                if (fclose(f))
                    result = 1;
            }
        }
        free(paths[i]);
        free(outputs[i].data);
    }
    free(nodes);
    yyjson_doc_free(doc);
    free(default_source);
    return result;
}
