/** @file inspect_report.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent schema checks shared by inspection test runners.
 */
#include "inspect_report.h"

#include "format_json.h"
#include "lib/files/file_system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fail(char *e, size_t n, const char *s) {
    if (e && n)
        snprintf(e, n, "%s", s);
    return false;
}

static bool duplicates(yyjson_mut_val *v, unsigned depth) {
    if (depth > 128)
        return false;
    size_t i, n;
    yyjson_mut_val *k, *x;
    if (yyjson_mut_is_obj(v)) {
        yyjson_mut_obj_foreach(v, i, n, k, x) {
            size_t j, m;
            yyjson_mut_val *q, *y;
            yyjson_mut_obj_foreach(v, j, m, q, y) {
                (void)y;
                if (j >= i)
                    break;
                if (yyjson_mut_get_len(k) == yyjson_mut_get_len(q)
                    && !memcmp(yyjson_mut_get_str(k), yyjson_mut_get_str(q), yyjson_mut_get_len(k)))
                    return false;
            }
            if (!duplicates(x, depth + 1))
                return false;
        }
    } else if (yyjson_mut_is_arr(v)) {
        yyjson_mut_arr_foreach(v, i, n, x) if (!duplicates(x, depth + 1)) return false;
    }
    return true;
}

yyjson_mut_doc *inspect_report_parse(const char *s, size_t n, char *e, size_t z) {
    if (e && z)
        e[0] = 0;
    yyjson_read_err error;
    yyjson_doc *input =
        s ? yyjson_read_opts((char *)s, n, YYJSON_READ_BIGNUM_AS_RAW, NULL, &error) : NULL;
    if (!input) {
        fail(e, z, "invalid JSON");
        return NULL;
    }
    yyjson_mut_doc *doc = yyjson_doc_mut_copy(input, NULL);
    yyjson_doc_free(input);
    if (!doc || !duplicates(yyjson_mut_doc_get_root(doc), 0)) {
        yyjson_mut_doc_free(doc);
        fail(e, z, "duplicate JSON key, nesting limit or allocation failure");
        return NULL;
    }
    return doc;
}

static bool integer(yyjson_mut_val *v) {
    if (yyjson_mut_is_int(v))
        return true;
    if (!yyjson_mut_is_raw(v))
        return false;
    const char *s = yyjson_mut_get_raw(v);
    size_t n = yyjson_mut_get_len(v), i = n && *s == '-' ? 1 : 0;
    if (i == n)
        return false;
    for (; i < n; i++)
        if (s[i] < '0' || s[i] > '9')
            return false;
    return true;
}

static bool equal(yyjson_mut_val *a, yyjson_mut_val *b) {
    if (integer(a) && integer(b))
        return format_json_equal(a, b);
    if (yyjson_mut_get_type(a) != yyjson_mut_get_type(b))
        return false;
    if (yyjson_mut_is_num(a) && yyjson_mut_is_real(a) != yyjson_mut_is_real(b))
        return false;
    if (yyjson_mut_is_obj(a)) {
        if (yyjson_mut_obj_size(a) != yyjson_mut_obj_size(b))
            return false;
        size_t i, n;
        yyjson_mut_val *k, *v;
        yyjson_mut_obj_foreach(a, i, n, k, v) {
            yyjson_mut_val *other =
                yyjson_mut_obj_getn(b, yyjson_mut_get_str(k), yyjson_mut_get_len(k));
            if (!other || !equal(v, other))
                return false;
        }
        return true;
    }
    if (yyjson_mut_is_arr(a)) {
        if (yyjson_mut_arr_size(a) != yyjson_mut_arr_size(b))
            return false;
        size_t i, n;
        yyjson_mut_val *v;
        yyjson_mut_arr_foreach(a, i, n, v) if (!equal(v, yyjson_mut_arr_get(b, i))) return false;
        return true;
    }
    return yyjson_mut_equals(a, b);
}

static bool kind(yyjson_mut_val *v, const char *s) {
    if (!s)
        return false;
    if (!strcmp(s, "null"))
        return yyjson_mut_is_null(v);
    if (!strcmp(s, "boolean"))
        return yyjson_mut_is_bool(v);
    if (!strcmp(s, "integer"))
        return integer(v);
    if (!strcmp(s, "number"))
        return yyjson_mut_is_num(v) || yyjson_mut_is_raw(v);
    if (!strcmp(s, "string"))
        return yyjson_mut_is_str(v);
    if (!strcmp(s, "array"))
        return yyjson_mut_is_arr(v);
    if (!strcmp(s, "object"))
        return yyjson_mut_is_obj(v);
    return false;
}

static long double numeric(yyjson_mut_val *v) {
    if (yyjson_mut_is_raw(v)) {
        size_t n = yyjson_mut_get_len(v);
        char *text = malloc(n + 1);
        if (!text)
            return 0;
        memcpy(text, yyjson_mut_get_raw(v), n);
        text[n] = 0;
        long double value = strtold(text, NULL);
        free(text);
        return value;
    }
    if (yyjson_mut_is_sint(v))
        return (long double)yyjson_mut_get_sint(v);
    if (yyjson_mut_is_uint(v))
        return (long double)yyjson_mut_get_uint(v);
    return (long double)yyjson_mut_get_real(v);
}

static bool base64(const char *s, size_t n) {
    if (n && s[n - 1] == '\n')
        n--; /* Python re.search's final $ anchor. */
    if (n % 4)
        return false;
    size_t padding = n && s[n - 1] == '=' ? 1 : 0;
    if (padding && n > 1 && s[n - 2] == '=')
        padding++;
    for (size_t i = 0; i < n - padding; i++)
        if (!((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z')
              || (s[i] >= '0' && s[i] <= '9') || s[i] == '+' || s[i] == '/'))
            return false;
    return true;
}

static yyjson_mut_doc *read_schema(const char *directory, const char *name, size_t length) {
    /* Schema files are repository inputs, not network resources. */
    if (!directory || !length || memchr(name, ':', length) || name[0] == '/'
        || memchr(name, '\\', length))
        return NULL;
    size_t n = strlen(directory);
    char *path = malloc(n + length + 2);
    if (!path)
        return NULL;
    memcpy(path, directory, n);
    path[n] = '/';
    memcpy(path + n + 1, name, length);
    path[n + length + 1] = 0;
    FILE *f = FOPEN(path, "rb");
    free(path);
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END)) {
        fclose(f);
        return NULL;
    }
    long size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        return NULL;
    }
    char *text = malloc((size_t)size + 1);
    if (!text) {
        fclose(f);
        return NULL;
    }
    bool ok = fread(text, 1, (size_t)size, f) == (size_t)size;
    if (fclose(f))
        ok = false;
    yyjson_mut_doc *doc = ok ? inspect_report_parse(text, (size_t)size, NULL, 0) : NULL;
    free(text);
    return doc;
}

static bool check(yyjson_mut_val *v,
                  yyjson_mut_val *s,
                  yyjson_mut_val *doc,
                  const char *dir,
                  unsigned depth,
                  char *e,
                  size_t z) {
    if (!yyjson_mut_is_obj(s) || depth > 128)
        return fail(e, z, "invalid schema or recursion limit");
    yyjson_mut_val *r = yyjson_mut_obj_get(s, "$ref");
    if (r) {
        const char *ref = yyjson_mut_get_str(r);
        if (!ref || strlen(ref) != yyjson_mut_get_len(r))
            return fail(e, z, "invalid reference");
        const char *hash = strchr(ref, '#');
        size_t n = hash ? (size_t)(hash - ref) : strlen(ref);
        yyjson_mut_doc *external = n ? read_schema(dir, ref, n) : NULL;
        yyjson_mut_val *target = n ? (external ? yyjson_mut_doc_get_root(external) : NULL) : doc;
        if (target && hash && hash[1])
            target = yyjson_mut_ptr_get(target, hash + 1);
        bool ok =
            target
            && check(v, target, n ? yyjson_mut_doc_get_root(external) : doc, dir, depth + 1, e, z);
        yyjson_mut_doc_free(external);
        if (!ok)
            return fail(e, z, "$ref");
    }
    size_t i, n;
    yyjson_mut_val *x;
    yyjson_mut_arr_foreach(yyjson_mut_obj_get(s, "allOf"),
                           i,
                           n,
                           x) if (!check(v, x, doc, dir, depth + 1, e, z)) return false;
    x = yyjson_mut_obj_get(s, "if");
    if (x) {
        bool matched = check(v, x, doc, dir, depth + 1, NULL, 0);
        x = yyjson_mut_obj_get(s, matched ? "then" : "else");
        if (x && !check(v, x, doc, dir, depth + 1, e, z))
            return false;
    }
    const char *combinations[] = {"anyOf", "oneOf"};
    for (unsigned c = 0; c < 2; c++) {
        yyjson_mut_val *choices = yyjson_mut_obj_get(s, combinations[c]);
        if (!choices)
            continue;
        size_t matches = 0;
        yyjson_mut_arr_foreach(choices, i, n, x) if (check(v, x, doc, dir, depth + 1, NULL, 0))
            matches++;
        if (!matches || (c && matches != 1))
            return fail(e, z, combinations[c]);
    }
    x = yyjson_mut_obj_get(s, "type");
    if (x) {
        bool ok = kind(v, yyjson_mut_get_str(x));
        yyjson_mut_val *t;
        yyjson_mut_arr_foreach(x, i, n, t) if (kind(v, yyjson_mut_get_str(t))) ok = true;
        if (!ok)
            return fail(e, z, "type");
    }
    x = yyjson_mut_obj_get(s, "const");
    if (x && !equal(v, x))
        return fail(e, z, "const");
    x = yyjson_mut_obj_get(s, "enum");
    if (x) {
        bool ok = false;
        yyjson_mut_val *t;
        yyjson_mut_arr_foreach(x, i, n, t) if (equal(v, t)) ok = true;
        if (!ok)
            return fail(e, z, "enum");
    }
    if (yyjson_mut_is_obj(v)) {
        yyjson_mut_val *props = yyjson_mut_obj_get(s, "properties"),
                       *extra = yyjson_mut_obj_get(s, "additionalProperties");
        yyjson_mut_arr_foreach(
            yyjson_mut_obj_get(s, "required"),
            i,
            n,
            x) if (!yyjson_mut_obj_getn(v,
                                        yyjson_mut_get_str(x),
                                        yyjson_mut_get_len(x))) return fail(e, z, "required");
        yyjson_mut_val *k;
        yyjson_mut_obj_foreach(v, i, n, k, x) {
            yyjson_mut_val *property =
                yyjson_mut_obj_getn(props, yyjson_mut_get_str(k), yyjson_mut_get_len(k));
            if (!property && yyjson_mut_is_false(extra))
                return fail(e, z, "unknown property");
            if (!property && yyjson_mut_is_obj(extra))
                property = extra;
            if (property && !check(x, property, doc, dir, depth + 1, e, z))
                return false;
            property = yyjson_mut_obj_get(s, "propertyNames");
            if (property && !check(k, property, doc, dir, depth + 1, e, z))
                return false;
        }
    } else if (yyjson_mut_is_arr(v)) {
        size_t count = yyjson_mut_arr_size(v);
        x = yyjson_mut_obj_get(s, "minItems");
        if (x && count < yyjson_mut_get_uint(x))
            return fail(e, z, "minItems");
        x = yyjson_mut_obj_get(s, "maxItems");
        if (x && count > yyjson_mut_get_uint(x))
            return fail(e, z, "maxItems");
        yyjson_mut_val *item = yyjson_mut_obj_get(s, "items");
        bool unique = yyjson_mut_is_true(yyjson_mut_obj_get(s, "uniqueItems"));
        yyjson_mut_arr_foreach(v, i, n, x) {
            if (item && !check(x, item, doc, dir, depth + 1, e, z))
                return false;
            if (unique)
                for (size_t j = 0; j < i; j++)
                    if (equal(x, yyjson_mut_arr_get(v, j)))
                        return fail(e, z, "uniqueItems");
        }
    } else {
        /* Reuse the independent scalar oracle (Unicode lengths, exact integer
         * bounds and five format patterns), not the production validator. */
        yyjson_mut_doc *atomic = yyjson_mut_doc_new(NULL);
        if (!atomic)
            return fail(e, z, "allocation failure");
        yyjson_mut_val *subset = yyjson_mut_obj(atomic);
        const char *keys[] = {"minimum", "maximum", "minLength", "maxLength", "pattern"};
        bool ok = true;
        for (unsigned j = 0; j < 5; j++) {
            x = yyjson_mut_obj_get(s, keys[j]);
            if (!x)
                continue;
            if (j == 4
                && yyjson_mut_equals_str(
                    x,
                    "^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$")) {
                if (yyjson_mut_is_str(v))
                    ok = base64(yyjson_mut_get_str(v), yyjson_mut_get_len(v));
            } else if (j == 4 && yyjson_mut_is_str(v)) {
                size_t length = yyjson_mut_get_len(v);
                const char *text = yyjson_mut_get_str(v);
                if (length && text[length - 1] == '\n')
                    length--;
                yyjson_mut_val *pattern_schema = yyjson_mut_obj(atomic);
                yyjson_mut_obj_add_val(atomic,
                                       pattern_schema,
                                       "pattern",
                                       yyjson_mut_val_mut_copy(atomic, x));
                yyjson_mut_val *text_value = yyjson_mut_strncpy(atomic, text, length);
                if (!format_json_shape(text_value, pattern_schema, NULL, e, z))
                    ok = false;
            } else if (j < 2 && (yyjson_mut_is_num(v) || yyjson_mut_is_raw(v))
                       && !(integer(v) && integer(x))) {
                long double a = numeric(v), b = numeric(x);
                if (j ? a > b : a < b)
                    ok = false;
            } else if (!yyjson_mut_obj_add_val(atomic,
                                               subset,
                                               keys[j],
                                               yyjson_mut_val_mut_copy(atomic, x)))
                ok = false;
        }
        if (ok)
            ok = format_json_shape(v, subset, NULL, e, z);
        yyjson_mut_doc_free(atomic);
        if (!ok)
            return fail(e, z, "scalar constraint");
    }
    return true;
}

bool inspect_report_schema(yyjson_mut_val *v,
                           yyjson_mut_val *s,
                           yyjson_mut_val *doc,
                           const char *dir,
                           char *e,
                           size_t z) {
    if (e && z)
        e[0] = 0;
    return check(v, s, doc, dir, 0, e, z);
}
