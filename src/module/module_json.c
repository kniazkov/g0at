/** @file module_json.c
 * @brief Strict descriptor parsing against the frozen module v1 schema.
 */
#include "module_json_internal.h"

#include <goat/module_identity.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* A resource limit, not a format version. Bounds recursive schema/facts walking. */
#define JSON_MAX_DEPTH GOAT_JSON_MAX_DEPTH

enum json_type {
    JSON_NULL = 1,
    JSON_BOOL = 2,
    JSON_INTEGER = 4,
    JSON_STRING = 8,
    JSON_OBJECT = 16,
    JSON_ARRAY = 32
};

typedef enum json_pattern {
    PATTERN_NONE,
    PATTERN_NAME,
    PATTERN_HASH,
    PATTERN_TARGET,
    PATTERN_LIBRARY,
    PATTERN_SYMBOL
} json_pattern;
typedef struct json_schema json_schema;

typedef struct json_field {
    const char *name;
    const json_schema *schema;
    bool required;
} json_field;

typedef struct json_literal {
    unsigned type;
    const char *text;
} json_literal;

struct json_schema {
    const json_schema *reference, *items, *keys, *extra;
    const json_field *fields;
    const json_literal *literals;
    size_t field_count, literal_count, min_count, max_count, min_length, max_length;
    unsigned types;
    json_pattern pattern;
    bool closed, bounded, unique, format_version;
    int64_t minimum, maximum;
};

#include "module_json_schema.h"

static bool text_is(const yyjson_val *v, const char *s) {
    return yyjson_is_str(v) && yyjson_get_len(v) == strlen(s)
           && !memcmp(yyjson_get_str(v), s, strlen(s));
}

static int compare_keys(const void *a, const void *b) {
    const yyjson_val *x = *(const yyjson_val *const *)a;
    const yyjson_val *y = *(const yyjson_val *const *)b;
    size_t nx = yyjson_get_len(x), ny = yyjson_get_len(y);
    int cmp = memcmp(yyjson_get_str(x), yyjson_get_str(y), nx < ny ? nx : ny);
    return cmp ? cmp : (nx > ny) - (nx < ny);
}

static goat_module_json_error preflight(yyjson_val *v, size_t depth) {
    if (depth > JSON_MAX_DEPTH)
        return GOAT_MODULE_JSON_LIMIT;
    if (yyjson_is_obj(v)) {
        size_t n = yyjson_obj_size(v);
        if (n > SIZE_MAX / sizeof(yyjson_val *))
            return GOAT_MODULE_JSON_LIMIT;
        yyjson_val **keys = n ? malloc(n * sizeof(*keys)) : NULL;
        if (n && !keys)
            return GOAT_MODULE_JSON_MEMORY;
        yyjson_obj_iter it = yyjson_obj_iter_with(v);
        yyjson_val *key;
        size_t i = 0;
        while ((key = yyjson_obj_iter_next(&it)))
            keys[i++] = key;
        if (n > 1)
            qsort(keys, n, sizeof(*keys), compare_keys);
        bool duplicate = false;
        for (i = 1; i < n; i++)
            duplicate |= compare_keys(keys + i - 1, keys + i) == 0;
        free(keys);
        if (duplicate)
            return GOAT_MODULE_JSON_DUPLICATE_KEY;
        it = yyjson_obj_iter_with(v);
        while ((key = yyjson_obj_iter_next(&it))) {
            goat_module_json_error err = preflight(yyjson_obj_iter_get_val(key), depth + 1);
            if (err != GOAT_MODULE_JSON_OK)
                return err;
        }
    } else if (yyjson_is_arr(v)) {
        yyjson_arr_iter it = yyjson_arr_iter_with(v);
        yyjson_val *item;
        while ((item = yyjson_arr_iter_next(&it))) {
            goat_module_json_error err = preflight(item, depth + 1);
            if (err != GOAT_MODULE_JSON_OK)
                return err;
        }
    }
    return GOAT_MODULE_JSON_OK;
}

static bool alnum_ascii(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
}

static bool pattern_valid(yyjson_val *v, const json_schema *s) {
    const char *p = yyjson_get_str(v);
    size_t n = yyjson_get_len(v);
    switch (s->pattern) {
        case PATTERN_NONE:
            return true;
        case PATTERN_NAME:
            return s->max_length == 250 ? goat_module_version_valid(p, n)
                                        : goat_module_name_valid(p, n);
        case PATTERN_HASH:
            if (n != 64)
                return false;
            for (size_t i = 0; i < n; i++)
                if (!((p[i] >= '0' && p[i] <= '9') || (p[i] >= 'a' && p[i] <= 'f')))
                    return false;
            return true;
        case PATTERN_TARGET:
            if (!n || !alnum_ascii(p[0]))
                return false;
            for (size_t i = 1; i < n; i++) {
                if (p[i] == '+') {
                    if (i + 1 == n || p[i + 1] == '+')
                        return false;
                } else if (!alnum_ascii(p[i]) && p[i] != '_' && p[i] != '-')
                    return false;
            }
            return true;
        case PATTERN_LIBRARY: {
            if (n < 11 || memcmp(p, "native/", 7) || !alnum_ascii(p[7]))
                return false;
            size_t end;
            if (n >= 4 && !memcmp(p + n - 4, ".dll", 4))
                end = n - 4;
            else if (!memcmp(p + n - 3, ".so", 3))
                end = n - 3;
            else
                return false;
            for (size_t i = 8; i < end; i++)
                if (!alnum_ascii(p[i]) && p[i] != '.' && p[i] != '_' && p[i] != '-')
                    return false;
            return true;
        }
        case PATTERN_SYMBOL:
            if (n < 12 || memcmp(p, "goat_fn_", 8) || memcmp(p + n - 3, "_v1", 3) || p[8] < '1'
                || p[8] > '9')
                return false;
            for (size_t i = 9; i < n - 3; i++)
                if (p[i] < '0' || p[i] > '9')
                    return false;
            return true;
    }
    return false;
}

static goat_module_json_error shape(yyjson_val *v, const json_schema *s) {
    if (s->reference)
        return shape(v, s->reference);
    int64_t number = 0;
    unsigned type = yyjson_is_obj(v)                ? JSON_OBJECT
                    : yyjson_is_arr(v)              ? JSON_ARRAY
                    : yyjson_is_str(v)              ? JSON_STRING
                    : yyjson_is_bool(v)             ? JSON_BOOL
                    : yyjson_is_null(v)             ? JSON_NULL
                    : goat_json_integer(v, &number) ? JSON_INTEGER
                                                    : 0;
    if (!type || (s->types && !(s->types & type)))
        return GOAT_MODULE_JSON_INVALID_FIELD;
    if (s->literal_count) {
        bool match = false;
        for (size_t i = 0; i < s->literal_count; i++) {
            const json_literal *lit = s->literals + i;
            if (type != lit->type)
                continue;
            if (type == JSON_STRING)
                match |= text_is(v, lit->text);
            else if (type == JSON_BOOL)
                match |= yyjson_get_bool(v) == (lit->text[0] == 't');
            else
                match |= number == strtoll(lit->text, NULL, 10);
        }
        if (!match)
            return s->format_version && type == JSON_INTEGER ? GOAT_MODULE_JSON_UNSUPPORTED_VERSION
                                                             : GOAT_MODULE_JSON_INVALID_FIELD;
    }
    if (type == JSON_INTEGER && s->bounded && (number < s->minimum || number > s->maximum))
        return GOAT_MODULE_JSON_INVALID_FIELD;
    if (type == JSON_STRING) {
        size_t n = yyjson_get_len(v);
        if (n < s->min_length || (s->max_length && n > s->max_length) || !pattern_valid(v, s))
            return GOAT_MODULE_JSON_INVALID_FIELD;
    }
    if (type == JSON_OBJECT) {
        yyjson_obj_iter it = yyjson_obj_iter_with(v);
        yyjson_val *key;
        while ((key = yyjson_obj_iter_next(&it))) {
            const json_schema *child = s->extra;
            for (size_t i = 0; i < s->field_count; i++)
                if (text_is(key, s->fields[i].name)) {
                    child = s->fields[i].schema;
                    break;
                }
            if (!child && s->closed)
                return GOAT_MODULE_JSON_UNKNOWN_FIELD;
            if (s->keys) {
                goat_module_json_error err = shape(key, s->keys);
                if (err != GOAT_MODULE_JSON_OK)
                    return err;
            }
            if (child) {
                goat_module_json_error err = shape(yyjson_obj_iter_get_val(key), child);
                if (err != GOAT_MODULE_JSON_OK)
                    return err;
            }
        }
        for (size_t i = 0; i < s->field_count; i++)
            if (s->fields[i].required && !yyjson_obj_get(v, s->fields[i].name))
                return GOAT_MODULE_JSON_INVALID_FIELD;
    }
    if (type == JSON_ARRAY) {
        size_t n = yyjson_arr_size(v);
        if (n < s->min_count || (s->max_count && n > s->max_count))
            return GOAT_MODULE_JSON_INVALID_FIELD;
        size_t i, max;
        yyjson_val *item;
        yyjson_arr_foreach(v, i, max, item) {
            goat_module_json_error err = shape(item, s->items);
            if (err != GOAT_MODULE_JSON_OK)
                return err;
            if (s->unique) {
                /* v1 uniqueItems arrays contain strings only. */
                for (size_t j = 0; j < i; j++) {
                    yyjson_val *prev = yyjson_arr_get(v, j);
                    if (!compare_keys(&prev, &item))
                        return GOAT_MODULE_JSON_INVALID_FIELD;
                }
            }
        }
    }
    return GOAT_MODULE_JSON_OK;
}

static bool equal_text(yyjson_val *a, yyjson_val *b) {
    return yyjson_get_len(a) == yyjson_get_len(b)
           && !memcmp(yyjson_get_str(a), yyjson_get_str(b), yyjson_get_len(a));
}

static bool no_nul(yyjson_val *v) {
    return !memchr(yyjson_get_str(v), 0, yyjson_get_len(v));
}

/* Descriptor-local invariants. Binary/graph/native ABI checks belong to their loaders. */
static bool records_valid(yyjson_val *root, goat_module_json_kind kind) {
    yyjson_val *authors = yyjson_obj_get(root, "authors");
    for (size_t i = 0; i < yyjson_arr_size(authors); i++) {
        yyjson_val *a = yyjson_arr_get(authors, i);
        for (size_t j = 0; j < i; j++) {
            yyjson_val *b = yyjson_arr_get(authors, j);
            yyjson_val *ae = yyjson_obj_get(a, "email"), *be = yyjson_obj_get(b, "email");
            if (equal_text(yyjson_obj_get(a, "name"), yyjson_obj_get(b, "name"))
                && ((!yyjson_get_len(ae) && !yyjson_get_len(be))
                    || (ae && be && equal_text(ae, be))))
                return false;
        }
    }
    if (kind == GOAT_MODULE_GMOD)
        return no_nul(yyjson_obj_get(root, "main"));
    yyjson_val *deps = yyjson_obj_get(root, "dependencies");
    for (size_t i = 0; i < yyjson_arr_size(deps); i++) {
        yyjson_val *a = yyjson_arr_get(deps, i);
        for (size_t j = 0; j < i; j++) {
            yyjson_val *b = yyjson_arr_get(deps, j);
            if (equal_text(yyjson_obj_get(a, "name"), yyjson_obj_get(b, "name"))
                && equal_text(yyjson_obj_get(a, "version"), yyjson_obj_get(b, "version")))
                return false;
        }
    }
    int64_t index;
    yyjson_val *files = yyjson_obj_get(root, "files");
    goat_json_integer(yyjson_obj_get(root, "main"), &index);
    if ((uint64_t)index >= yyjson_arr_size(files))
        return false;
    for (size_t i = 0; i < yyjson_arr_size(files); i++) {
        yyjson_val *a = yyjson_arr_get(files, i);
        if (!no_nul(yyjson_obj_get(a, "name")))
            return false;
        if (yyjson_get_bool(yyjson_obj_get(a, "external")))
            continue;
        for (size_t j = 0; j < i; j++) {
            yyjson_val *b = yyjson_arr_get(files, j);
            if (!yyjson_get_bool(yyjson_obj_get(b, "external"))
                && equal_text(yyjson_obj_get(a, "name"), yyjson_obj_get(b, "name")))
                return false;
        }
    }
    yyjson_val *functions = yyjson_obj_get(root, "functions");
    for (size_t i = 0; i < yyjson_arr_size(functions); i++) {
        yyjson_val *a = yyjson_arr_get(functions, i);
        goat_json_integer(yyjson_obj_get(a, "file"), &index);
        if ((uint64_t)index >= yyjson_arr_size(files))
            return false;
        int64_t id;
        goat_json_integer(yyjson_obj_get(a, "functionId"), &id);
        for (size_t j = 0; j < i; j++) {
            goat_json_integer(yyjson_obj_get(yyjson_arr_get(functions, j), "functionId"), &index);
            if (id == index)
                return false;
        }
    }
    return true;
}

static void remove_empty_text(yyjson_mut_val *obj, const char *key) {
    yyjson_mut_val *v = yyjson_mut_obj_get(obj, key);
    if (v && !yyjson_mut_get_len(v))
        yyjson_mut_obj_remove_key(obj, key);
}

static yyjson_doc *
normalize(yyjson_doc *doc, goat_module_json_kind kind, goat_module_json_error *error) {
    yyjson_mut_doc *copy = yyjson_doc_mut_copy(doc, NULL);
    if (!copy)
        return NULL;
    yyjson_mut_val *root = yyjson_mut_doc_get_root(copy);
    remove_empty_text(root, "description");
    remove_empty_text(root, "license");
    remove_empty_text(root, "homepage");
    yyjson_mut_val *authors = yyjson_mut_obj_get(root, "authors");
    size_t i, max;
    yyjson_mut_val *author;
    yyjson_mut_arr_foreach(authors, i, max, author) remove_empty_text(author, "email");
    if (authors && !yyjson_mut_arr_size(authors))
        yyjson_mut_obj_remove_key(root, "authors");
    bool ok = true;
    if (kind == GOAT_MODULE_GMOD) {
        if (!yyjson_mut_obj_get(root, "dependencies"))
            ok = yyjson_mut_obj_add_obj(copy, root, "dependencies") != NULL;
        yyjson_mut_val *native = yyjson_mut_obj_get(root, "native");
        if (!native)
            native = yyjson_mut_obj_add_obj(copy, root, "native");
        ok = ok && native;
        if (ok && !yyjson_mut_obj_get(native, "mode"))
            ok = yyjson_mut_obj_add_str(copy, native, "mode", "auto");
        if (ok && !yyjson_mut_obj_get(native, "target"))
            ok = yyjson_mut_obj_add_str(copy, native, "target", "host");
    } else {
        const char *empty[] = {"dependencies", "functions", "externals", "native", "producers"};
        for (size_t j = 0; j < sizeof(empty) / sizeof(*empty); j++) {
            yyjson_mut_val *v = yyjson_mut_obj_get(root, empty[j]);
            if (v && !yyjson_mut_get_len(v))
                yyjson_mut_obj_remove_key(root, empty[j]);
        }
    }
    *error = ok ? goat_module_json_normalize(copy, kind) : GOAT_MODULE_JSON_MEMORY;
    yyjson_doc *result =
        *error == GOAT_MODULE_JSON_OK ? yyjson_mut_doc_imut_copy(copy, NULL) : NULL;
    yyjson_mut_doc_free(copy);
    return result;
}

goat_module_json_error goat_module_json_read(const char *data,
                                             size_t length,
                                             goat_module_json_kind kind,
                                             yyjson_doc **out) {
    if (!out)
        return GOAT_MODULE_JSON_INVALID_FIELD;
    *out = NULL;
    if (kind != GOAT_MODULE_GMOD && kind != GOAT_MODULE_METADATA)
        return GOAT_MODULE_JSON_INVALID_FIELD;
    if (!data || (length >= 3 && !memcmp(data, "\xef\xbb\xbf", 3)))
        return GOAT_MODULE_JSON_SYNTAX;
    yyjson_doc *doc = yyjson_read(data, length, YYJSON_READ_NUMBER_AS_RAW);
    if (!doc)
        return GOAT_MODULE_JSON_SYNTAX;
    yyjson_val *root = yyjson_doc_get_root(doc);
    goat_module_json_error err = preflight(root, 0);
    if (err == GOAT_MODULE_JSON_OK)
        err = shape(root, kind == GOAT_MODULE_GMOD ? GMOD_SCHEMA : METADATA_SCHEMA);
    if (err == GOAT_MODULE_JSON_OK && !records_valid(root, kind))
        err = GOAT_MODULE_JSON_INVALID_FIELD;
    if (err == GOAT_MODULE_JSON_OK) {
        *out = normalize(doc, kind, &err);
        if (!*out && err == GOAT_MODULE_JSON_OK)
            err = GOAT_MODULE_JSON_MEMORY;
    }
    yyjson_doc_free(doc);
    return err;
}

const char *goat_module_json_error_string(goat_module_json_error error) {
    switch (error) {
        case GOAT_MODULE_JSON_OK:
            return "ok";
        case GOAT_MODULE_JSON_SYNTAX:
            return "invalid JSON or UTF-8";
        case GOAT_MODULE_JSON_DUPLICATE_KEY:
            return "duplicate JSON key";
        case GOAT_MODULE_JSON_UNKNOWN_FIELD:
            return "unknown JSON field";
        case GOAT_MODULE_JSON_INVALID_FIELD:
            return "invalid JSON field";
        case GOAT_MODULE_JSON_UNSUPPORTED_VERSION:
            return "unsupported format version";
        case GOAT_MODULE_JSON_LIMIT:
            return "JSON nesting limit exceeded";
        case GOAT_MODULE_JSON_MEMORY:
            return "JSON allocation failed";
    }
    return "unknown error";
}
