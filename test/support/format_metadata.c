/** @file format_metadata.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent port of the metadata portion of reference.py.
 * No production module validation or canonicalization is linked here.
 */
#include "format_metadata.h"

#include "format_analysis.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GET(v, k) yyjson_mut_obj_get((v), (k))
#define EACH(a, i, n, v) yyjson_mut_arr_foreach((a), (i), (n), (v))
#define REQUIRE(c, message)                                                                        \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            if (error && error_size)                                                               \
                snprintf(error, error_size, "%s", message);                                        \
            return false;                                                                          \
        }                                                                                          \
    } while (0)

static bool is(yyjson_mut_val *v, const char *s) {
    return yyjson_mut_is_str(v) && yyjson_mut_get_len(v) == strlen(s)
           && !memcmp(yyjson_mut_get_str(v), s, strlen(s));
}

static int bytes_compare(const char *a, size_t an, const char *b, size_t bn) {
    int c = memcmp(a, b, an < bn ? an : bn);
    return c ? c : (an > bn) - (an < bn);
}

static int string_compare(yyjson_mut_val *a, yyjson_mut_val *b) {
    return bytes_compare(yyjson_mut_get_str(a),
                         yyjson_mut_get_len(a),
                         yyjson_mut_get_str(b),
                         yyjson_mut_get_len(b));
}

static int canonical_compare(yyjson_mut_val *a, yyjson_mut_val *b) {
    size_t an, bn;
    char *as = format_json_canonical(a, &an), *bs = format_json_canonical(b, &bn);
    if (!as || !bs)
        abort();
    int c = bytes_compare(as, an, bs, bn);
    free(as);
    free(bs);
    return c;
}

static int number_compare(yyjson_mut_val *a, yyjson_mut_val *b) {
    uint64_t x = yyjson_mut_get_uint(a), y = yyjson_mut_get_uint(b);
    return (x > y) - (x < y);
}

typedef int (*comparison_t)(yyjson_mut_val *, yyjson_mut_val *);

static void sort(yyjson_mut_val *array, comparison_t compare) {
    size_t n = yyjson_mut_arr_size(array);
    if (n < 2)
        return;
    yyjson_mut_val **values = malloc(n * sizeof(*values));
    if (!values)
        abort();
    for (size_t i = 0; i < n; ++i)
        values[i] = yyjson_mut_arr_get(array, i);
    for (size_t i = 1; i < n; ++i) {
        yyjson_mut_val *v = values[i];
        size_t j = i;
        while (j && compare(values[j - 1], v) > 0) {
            values[j] = values[j - 1];
            --j;
        }
        values[j] = v;
    }
    yyjson_mut_arr_clear(array);
    for (size_t i = 0; i < n; ++i)
        yyjson_mut_arr_append(array, values[i]);
    free(values);
}

static bool distinct(yyjson_mut_val *array, comparison_t compare) {
    size_t n = yyjson_mut_arr_size(array);
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j)
            if (!compare(yyjson_mut_arr_get(array, i), yyjson_mut_arr_get(array, j)))
                return false;
    return true;
}

static int name_version(yyjson_mut_val *a, yyjson_mut_val *b) {
    int c = string_compare(GET(a, "name"), GET(b, "name"));
    return c ? c : string_compare(GET(a, "version"), GET(b, "version"));
}

static int producer_compare(yyjson_mut_val *a, yyjson_mut_val *b) {
    int c = name_version(a, b);
    return c ? c : string_compare(GET(a, "buildId"), GET(b, "buildId"));
}

static int author_compare(yyjson_mut_val *a, yyjson_mut_val *b) {
    int c = string_compare(GET(a, "name"), GET(b, "name"));
    if (c)
        return c;
    yyjson_mut_val *x = GET(a, "email"), *y = GET(b, "email");
    return bytes_compare(x ? yyjson_mut_get_str(x) : "",
                         x ? yyjson_mut_get_len(x) : 0,
                         y ? yyjson_mut_get_str(y) : "",
                         y ? yyjson_mut_get_len(y) : 0);
}

static int function_compare(yyjson_mut_val *a, yyjson_mut_val *b) {
    return number_compare(GET(a, "functionId"), GET(b, "functionId"));
}

static int specialization_compare(yyjson_mut_val *a, yyjson_mut_val *b) {
    return number_compare(GET(a, "specializationId"), GET(b, "specializationId"));
}

static int symbol_compare(yyjson_mut_val *a, yyjson_mut_val *b) {
    return string_compare(GET(a, "symbol"), GET(b, "symbol"));
}

static int library_compare(yyjson_mut_val *a, yyjson_mut_val *b) {
    return string_compare(GET(a, "library"), GET(b, "library"));
}

static int native_compare(yyjson_mut_val *a, yyjson_mut_val *b) {
    int c = canonical_compare(GET(a, "target"), GET(b, "target"));
    if (!c)
        c = number_compare(GET(a, "goatAbiVersion"), GET(b, "goatAbiVersion"));
    if (!c)
        c = string_compare(GET(a, "hash"), GET(b, "hash"));
    return c ? c : library_compare(a, b);
}

static bool identifier(yyjson_mut_val *v) {
    const char *s = yyjson_mut_get_str(v);
    size_t n = yyjson_mut_get_len(v), stem = 0;
    while (stem < n && s[stem] != '.')
        ++stem;
    if (stem == 3
        && (!memcmp(s, "con", 3) || !memcmp(s, "prn", 3) || !memcmp(s, "aux", 3)
            || !memcmp(s, "nul", 3)))
        return false;
    return !(stem == 4 && (!memcmp(s, "com", 3) || !memcmp(s, "lpt", 3)) && s[3] >= '0'
             && s[3] <= '9');
}

static bool target(yyjson_mut_val *t, yyjson_mut_val *defs, char *error, size_t error_size) {
    if (!format_json_shape(t, GET(defs, "target"), defs, error, error_size))
        return false;
    REQUIRE(yyjson_mut_get_uint(GET(t, "bits")) == (is(GET(t, "architecture"), "x86") ? 32u : 64u),
            "target bitness");
    yyjson_mut_val *r = GET(t, "runtime"), *family = GET(r, "family");
    REQUIRE(is(GET(t, "os"), "linux") ? (is(family, "glibc") || is(family, "musl"))
                                      : (is(family, "msvcrt") || is(family, "ucrt")),
            "runtime family");
    if (is(GET(t, "os"), "windows"))
        REQUIRE(yyjson_mut_get_uint(yyjson_mut_arr_get(GET(t, "minimumOs"), 0)) >= 10,
                "Windows minimum");
    sort(GET(t, "cpuFeatures"), string_compare);
    yyjson_mut_val *parts[] = {GET(t, "minimumOs"), GET(r, "minimumVersion")};
    for (size_t i = 0; i < 2; ++i)
        while (yyjson_mut_arr_size(parts[i]) > 1
               && yyjson_mut_get_uint(yyjson_mut_arr_get_last(parts[i])) == 0)
            yyjson_mut_arr_remove_last(parts[i]);
    return true;
}

/* Witness search follows reference.py, using guarded neighbours at int64 edges. */
typedef struct {
    int64_t integer;
    double real;
    uint32_t character;
    bool boolean;
    const char *string;
    size_t length;
} scalar_t;

static uint32_t codepoint(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    uint32_t v = *p++;
    if (v < 128)
        return v;
    unsigned n = v < 224 ? 1 : v < 240 ? 2 : 3;
    v &= n == 1 ? 31 : n == 2 ? 15 : 7;
    while (n--)
        v = (v << 6) | (*p++ & 63);
    return v;
}

static scalar_t scalar_value(const char *kind, yyjson_mut_val *v) {
    scalar_t r = {0};
    if (!strcmp(kind, "integer"))
        r.integer = yyjson_mut_get_sint(v);
    else if (!strcmp(kind, "real")) {
        uint64_t bits = 0;
        const char *p = yyjson_mut_get_str(v);
        for (size_t i = 0; i < 16; ++i)
            bits = (bits << 4) | (uint64_t)(p[i] <= '9' ? p[i] - '0' : p[i] - 'a' + 10);
        memcpy(&r.real, &bits, sizeof(bits));
    } else if (!strcmp(kind, "character"))
        r.character = codepoint(yyjson_mut_get_str(v));
    else if (!strcmp(kind, "string")) {
        r.string = yyjson_mut_get_str(v);
        r.length = yyjson_mut_get_len(v);
    } else if (!strcmp(kind, "boolean"))
        r.boolean = yyjson_mut_get_bool(v);
    return r;
}

static bool predicate(const char *kind, scalar_t a, scalar_t b, yyjson_mut_val *op) {
    int c = 0;
    if (!strcmp(kind, "real")) {
        if (is(op, "equal"))
            return a.real == b.real;
        if (is(op, "notEqual"))
            return a.real != b.real;
        if (is(op, "less"))
            return a.real < b.real;
        if (is(op, "lessEqual"))
            return a.real <= b.real;
        if (is(op, "greater"))
            return a.real > b.real;
        return a.real >= b.real;
    }
    if (!strcmp(kind, "integer"))
        c = (a.integer > b.integer) - (a.integer < b.integer);
    else if (!strcmp(kind, "character"))
        c = (a.character > b.character) - (a.character < b.character);
    else if (!strcmp(kind, "string"))
        c = bytes_compare(a.string, a.length, b.string, b.length);
    else if (!strcmp(kind, "boolean"))
        c = (int)a.boolean - (int)b.boolean;
    if (is(op, "equal"))
        return c == 0;
    if (is(op, "notEqual"))
        return c != 0;
    if (is(op, "less"))
        return c < 0;
    if (is(op, "lessEqual"))
        return c <= 0;
    if (is(op, "greater"))
        return c > 0;
    return c >= 0;
}

static bool witness(const char *kind, scalar_t v, yyjson_mut_val **predicates, size_t count) {
    for (size_t i = 0; i < count; ++i)
        if (!predicate(kind,
                       v,
                       scalar_value(kind, GET(GET(predicates[i], "constant"), "value")),
                       GET(predicates[i], "operation")))
            return false;
    return true;
}

bool format_metadata_guards_overlap(yyjson_mut_val *a, yyjson_mut_val *b) {
    yyjson_mut_val *types = GET(a, "argumentTypes");
    if (!format_json_equal(types, GET(b, "argumentTypes")))
        return false;
    size_t total =
        yyjson_mut_arr_size(GET(a, "conditions")) + yyjson_mut_arr_size(GET(b, "conditions"));
    yyjson_mut_val **predicates = malloc((total + 1) * sizeof(*predicates));
    if (!predicates)
        abort();
    for (size_t index = 0; index < yyjson_mut_arr_size(types); ++index) {
        const char *kind = yyjson_mut_get_str(yyjson_mut_arr_get(types, index));
        size_t count = 0;
        yyjson_mut_val *groups[] = {GET(a, "conditions"), GET(b, "conditions")};
        for (size_t g = 0; g < 2; ++g) {
            size_t i, n;
            yyjson_mut_val *p;
            EACH(groups[g], i, n, p)
            if (yyjson_mut_get_uint(GET(p, "argument")) == index)
                predicates[count++] = p;
        }
        bool found = false;
        scalar_t v = {0};
        if (!strcmp(kind, "integer")) {
            int64_t initial[] = {INT64_MIN, INT64_MAX, 0};
            for (size_t i = 0; i < 3; ++i) {
                v.integer = initial[i];
                found |= witness(kind, v, predicates, count);
            }
            for (size_t i = 0; i < count && !found; ++i) {
                v = scalar_value(kind, GET(GET(predicates[i], "constant"), "value"));
                found |= witness(kind, v, predicates, count);
                if (v.integer > INT64_MIN) {
                    --v.integer;
                    found |= witness(kind, v, predicates, count);
                    ++v.integer;
                }
                if (v.integer < INT64_MAX) {
                    ++v.integer;
                    found |= witness(kind, v, predicates, count);
                }
            }
        } else if (!strcmp(kind, "real")) {
            double initial[] = {0, INFINITY, -INFINITY, NAN};
            for (size_t i = 0; i < 4; ++i) {
                v.real = initial[i];
                found |= witness(kind, v, predicates, count);
            }
            for (size_t i = 0; i < count && !found; ++i) {
                double d = scalar_value(kind, GET(GET(predicates[i], "constant"), "value")).real;
                double candidates[] = {d, nextafter(d, -INFINITY), nextafter(d, INFINITY)};
                for (size_t j = 0; j < 3; ++j) {
                    v.real = candidates[j];
                    found |= witness(kind, v, predicates, count);
                }
            }
        } else if (!strcmp(kind, "character")) {
            uint32_t initial[] = {0, 0x10ffff, 0xe000, 0xd7ff};
            for (size_t i = 0; i < 4; ++i) {
                v.character = initial[i];
                found |= witness(kind, v, predicates, count);
            }
            for (size_t i = 0; i < count && !found; ++i) {
                int64_t c =
                    scalar_value(kind, GET(GET(predicates[i], "constant"), "value")).character;
                for (int d = -1; d <= 1; ++d)
                    if (c + d >= 0 && c + d <= 0x10ffff && !(c + d >= 0xd800 && c + d <= 0xdfff)) {
                        v.character = (uint32_t)(c + d);
                        found |= witness(kind, v, predicates, count);
                    }
            }
        } else if (!strcmp(kind, "string")) {
            for (size_t i = 0; i < count && !found; ++i) {
                v = scalar_value(kind, GET(GET(predicates[i], "constant"), "value"));
                found |= witness(kind, v, predicates, count);
            }
            char *fresh = malloc(count + 1);
            if (!fresh)
                abort();
            memset(fresh, 'x', count + 1);
            for (size_t len = 0; len <= count && !found; ++len) {
                v.string = fresh;
                v.length = len;
                found |= witness(kind, v, predicates, count);
            }
            free(fresh);
        } else if (!strcmp(kind, "boolean")) {
            v.boolean = false;
            found = witness(kind, v, predicates, count);
            v.boolean = true;
            found |= witness(kind, v, predicates, count);
        } else
            found = witness(kind, v, predicates, count);
        if (!found) {
            free(predicates);
            return false;
        }
    }
    free(predicates);
    return true;
}

static bool
guards(yyjson_mut_val *entries, yyjson_mut_val *functions, char *error, size_t error_size) {
    REQUIRE(distinct(entries, specialization_compare), "duplicate specializationId");
    REQUIRE(distinct(entries, symbol_compare), "duplicate native symbol");
    size_t i, n;
    yyjson_mut_val *e;
    EACH(entries, i, n, e) {
        yyjson_mut_val *g = GET(e, "when"), *sig = GET(e, "signature"),
                       *arguments = GET(sig, "arguments");
        REQUIRE(format_json_equal(GET(g, "argumentTypes"), arguments), "guard/signature types");
        if (functions) {
            bool found = false;
            size_t fi, fn;
            yyjson_mut_val *f;
            EACH(functions, fi, fn, f)
            if (format_json_equal(GET(f, "functionId"), GET(e, "functionId")))
                found = yyjson_mut_arr_size(GET(f, "parameters")) == yyjson_mut_arr_size(arguments);
            REQUIRE(found, "specialization arity");
        }
        char symbol[64];
        snprintf(symbol,
                 sizeof(symbol),
                 "goat_fn_%" PRIu64 "_v1",
                 yyjson_mut_get_uint(GET(e, "specializationId")));
        REQUIRE(is(GET(e, "symbol"), symbol), "symbol identity");
        yyjson_mut_val *conditions = GET(g, "conditions"), *p;
        size_t pi, pn;
        EACH(conditions, pi, pn, p) {
            uint64_t arg = yyjson_mut_get_uint(GET(p, "argument"));
            REQUIRE(arg < yyjson_mut_arr_size(arguments), "guard argument");
            yyjson_mut_val *c = GET(p, "constant"), *type = GET(c, "type"),
                           *op = GET(p, "operation");
            REQUIRE(format_json_equal(type, yyjson_mut_arr_get(arguments, (size_t)arg)),
                    "guard constant type");
            if (!format_scalar(yyjson_mut_get_str(type), GET(c, "value"), error, error_size))
                return false;
            REQUIRE(is(op, "equal") || is(op, "notEqual") || is(type, "integer") || is(type, "real")
                        || is(type, "character"),
                    "ordered guard type");
        }
        REQUIRE(distinct(conditions, canonical_compare), "duplicate guard condition");
        if (yyjson_mut_arr_size(conditions))
            sort(conditions, canonical_compare);
        else
            yyjson_mut_obj_remove_key(g, "conditions");
    }
    sort(entries, specialization_compare);
    for (i = 0; i < yyjson_mut_arr_size(entries); ++i)
        for (size_t j = i + 1; j < yyjson_mut_arr_size(entries); ++j) {
            yyjson_mut_val *a = yyjson_mut_arr_get(entries, i), *b = yyjson_mut_arr_get(entries, j);
            REQUIRE(!format_json_equal(GET(a, "functionId"), GET(b, "functionId"))
                        || !format_metadata_guards_overlap(GET(a, "when"), GET(b, "when")),
                    "overlapping native guards");
        }
    return true;
}

bool format_metadata_normalize(yyjson_mut_doc *doc,
                               yyjson_mut_val *v,
                               const char *kind,
                               yyjson_mut_val *source,
                               char *error,
                               size_t error_size) {
    yyjson_mut_val *defs = GET(source, "DEFS"), *schema = GET(GET(source, "ROOTS"), kind);
    REQUIRE(schema, "unknown metadata kind");
    if (!format_json_shape(v, schema, defs, error, error_size))
        return false;
    const char *identifiers[] = {"name", "version", "module"};
    for (size_t i = 0; i < 3; ++i)
        if (GET(v, identifiers[i]))
            REQUIRE(identifier(GET(v, identifiers[i])), "reserved identifier");
    const char *optional[] = {"description", "license", "homepage"};
    for (size_t i = 0; i < 3; ++i)
        if (is(GET(v, optional[i]), ""))
            yyjson_mut_obj_remove_key(v, optional[i]);
    yyjson_mut_val *authors = GET(v, "authors"), *item;
    size_t i, n;
    EACH(authors, i, n, item)
    if (is(GET(item, "email"), ""))
        yyjson_mut_obj_remove_key(item, "email");
    REQUIRE(distinct(authors, author_compare), "duplicate author");
    REQUIRE(distinct(GET(v, "producers"), canonical_compare), "duplicate producer");
    sort(GET(v, "producers"), producer_compare);
    if (authors && !yyjson_mut_arr_size(authors))
        yyjson_mut_obj_remove_key(v, "authors");
    if (!strcmp(kind, "gmod")) {
        yyjson_mut_val *main = GET(v, "main");
        REQUIRE(!memchr(yyjson_mut_get_str(main), 0, yyjson_mut_get_len(main)), "NUL path");
        yyjson_mut_val *deps = GET(v, "dependencies"), *key, *value;
        size_t di, dn;
        yyjson_mut_obj_foreach(deps, di, dn, key, value) {
            REQUIRE(identifier(key) && identifier(value), "reserved identifier");
        }
        if (!deps)
            yyjson_mut_obj_add_val(doc, v, "dependencies", yyjson_mut_obj(doc));
        yyjson_mut_val *native = GET(v, "native");
        if (!native) {
            native = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_val(doc, v, "native", native);
        }
        if (!GET(native, "mode"))
            yyjson_mut_obj_add_str(doc, native, "mode", "auto");
        if (!GET(native, "target"))
            yyjson_mut_obj_add_str(doc, native, "target", "host");
        return true;
    }
    if (!strcmp(kind, "module")) {
        yyjson_mut_val *files = GET(v, "files"), *deps = GET(v, "dependencies"),
                       *functions = GET(v, "functions");
        REQUIRE(yyjson_mut_get_uint(GET(v, "main")) < yyjson_mut_arr_size(files),
                "main file index");
        EACH(files, i, n, item) {
            yyjson_mut_val *name = GET(item, "name");
            REQUIRE(!memchr(yyjson_mut_get_str(name), 0, yyjson_mut_get_len(name)),
                    "NUL logical filename");
            if (!yyjson_mut_get_bool(GET(item, "external")))
                for (size_t j = 0; j < i; ++j) {
                    yyjson_mut_val *earlier = yyjson_mut_arr_get(files, j);
                    REQUIRE(yyjson_mut_get_bool(GET(earlier, "external"))
                                || !format_json_equal(name, GET(earlier, "name")),
                            "duplicate internal filename");
                }
        }
        EACH(deps, i, n, item)
        REQUIRE(identifier(GET(item, "name")) && identifier(GET(item, "version")),
                "reserved identifier");
        REQUIRE(distinct(deps, name_version), "duplicate dependency");
        sort(deps, name_version);
        REQUIRE(distinct(functions, function_compare), "duplicate functionId");
        sort(functions, function_compare);
        EACH(functions, i, n, item) {
            REQUIRE(yyjson_mut_get_uint(GET(item, "file")) < yyjson_mut_arr_size(files),
                    "function file");
            yyjson_mut_val *analysis = GET(item, "analysis");
            if (analysis) {
                if (!format_analysis(doc,
                                     analysis,
                                     yyjson_mut_arr_size(GET(item, "parameters")),
                                     defs,
                                     error,
                                     error_size))
                    return false;
                if (yyjson_mut_is_null(analysis))
                    yyjson_mut_obj_remove_key(item, "analysis");
            }
        }
        yyjson_mut_val *externals = GET(v, "externals"), *key, *e;
        size_t ei, en;
        yyjson_mut_obj_foreach(externals, ei, en, key, e) {
            (void)key;
            yyjson_mut_val *type = GET(e, "type"), *value = GET(e, "value"),
                           *fid = GET(e, "functionId"), *ref = GET(e, "functionRef");
            REQUIRE(!(yyjson_mut_get_bool(GET(e, "mutable")) && (type || value || fid || ref)),
                    "mutable guarantee");
            REQUIRE(!(fid && ref), "two function bindings");
            if (value) {
                if (!format_scalar(yyjson_mut_get_str(type), value, error, error_size))
                    return false;
                if (is(type, "null"))
                    yyjson_mut_obj_remove_key(e, "value");
            }
            if (fid || ref)
                REQUIRE(!type || is(type, "function"), "function binding type");
            if (fid) {
                bool found = false;
                size_t fi, fn;
                yyjson_mut_val *f;
                EACH(functions, fi, fn, f)
                if (format_json_equal(fid, GET(f, "functionId")))
                    found = true;
                REQUIRE(found, "missing function");
            }
            if (ref)
                REQUIRE(identifier(GET(ref, "module")) && identifier(GET(ref, "version")),
                        "reserved identifier");
        }
        yyjson_mut_val *native = GET(v, "native");
        /* A missing functions field still imposes an empty function-id set. */
        yyjson_mut_val *arities = functions ? functions : yyjson_mut_arr(doc);
        EACH(native, i, n, item) {
            if (!target(GET(item, "target"), defs, error, error_size)
                || !guards(GET(item, "specializations"), arities, error, error_size))
                return false;
        }
        REQUIRE(distinct(native, library_compare), "duplicate native library");
        sort(native, native_compare);
    } else if (!strcmp(kind, "native")) {
        if (!target(GET(v, "target"), defs, error, error_size)
            || !guards(GET(v, "specializations"), NULL, error, error_size))
            return false;
    } else if (!strcmp(kind, "target")) {
        if (!target(v, defs, error, error_size))
            return false;
    }
    const char *empty_fields[] =
        {"authors", "producers", "dependencies", "functions", "externals", "native"};
    for (size_t j = 0; j < 6; ++j) {
        yyjson_mut_val *field = GET(v, empty_fields[j]);
        if (field
            && ((yyjson_mut_is_arr(field) && !yyjson_mut_arr_size(field))
                || (yyjson_mut_is_obj(field) && !yyjson_mut_obj_size(field))))
            yyjson_mut_obj_remove_key(v, empty_fields[j]);
    }
    return true;
}

char *format_metadata_main_bytes(yyjson_mut_val *metadata,
                                 yyjson_mut_val *source,
                                 size_t *size,
                                 char *error,
                                 size_t error_size) {
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    if (!doc)
        return NULL;
    yyjson_mut_val *copy = yyjson_mut_val_mut_copy(doc, metadata);
    if (!copy || !format_metadata_normalize(doc, copy, "module", source, error, error_size)) {
        yyjson_mut_doc_free(doc);
        return NULL;
    }
    yyjson_mut_val *projection = yyjson_mut_obj(doc);
    const char *fields[] = {"formatVersion",
                            "bytecodeVersion",
                            "name",
                            "version",
                            "main",
                            "files",
                            "dependencies",
                            "functions",
                            "externals"};
    for (size_t i = 0; i < 9; ++i) {
        yyjson_mut_val *value = GET(copy, fields[i]);
        if (value)
            yyjson_mut_obj_add_val(doc, projection, fields[i], yyjson_mut_val_mut_copy(doc, value));
    }
    char *result = format_json_canonical(projection, size);
    yyjson_mut_doc_free(doc);
    return result;
}

static void little_endian64(unsigned char *out, uint64_t value) {
    for (size_t i = 0; i < 8; ++i) {
        out[i] = (unsigned char)value;
        value >>= 8;
    }
}

unsigned char *format_metadata_hash_input(yyjson_mut_val *metadata,
                                          yyjson_mut_val *source,
                                          const unsigned char *bytecode,
                                          size_t bytecode_size,
                                          size_t *size,
                                          char *error,
                                          size_t error_size) {
    size_t metadata_size;
    char *bytes = format_metadata_main_bytes(metadata, source, &metadata_size, error, error_size);
    if (!bytes)
        return NULL;
    static const unsigned char marker[] = "GOAT-MODULE-MAIN\0\1";
    size_t prefix = sizeof(marker) - 1 + 16;
    if (metadata_size > SIZE_MAX - prefix || bytecode_size > SIZE_MAX - prefix - metadata_size) {
        free(bytes);
        return NULL;
    }
    *size = prefix + metadata_size + bytecode_size;
    unsigned char *result = malloc(*size);
    if (result) {
        size_t at = sizeof(marker) - 1;
        memcpy(result, marker, at);
        little_endian64(result + at, (uint64_t)metadata_size);
        at += 8;
        memcpy(result + at, bytes, metadata_size);
        at += metadata_size;
        little_endian64(result + at, (uint64_t)bytecode_size);
        at += 8;
        if (bytecode_size)
            memcpy(result + at, bytecode, bytecode_size);
    }
    free(bytes);
    return result;
}
