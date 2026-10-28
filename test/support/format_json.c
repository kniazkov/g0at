/** @file format_json.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent strict JSON, canonical bytes, and declarative schema oracle.
 */
#include "format_json.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fail(char *error, size_t size, const char *message) {
    if (error && size)
        snprintf(error, size, "%s", message);
    return false;
}

/* Count Unicode scalars, rejecting overlong encodings and surrogate code points.
 * This also protects callers that construct mutable values instead of parsing them. */
static bool unicode(const char *text, size_t length, size_t *count) {
    size_t n = 0;
    for (size_t i = 0; i < length; n++) {
        unsigned char c = (unsigned char)text[i++];
        uint32_t code;
        size_t extra;
        if (c < 0x80)
            continue;
        if (c >= 0xc2 && c <= 0xdf) {
            code = c & 31;
            extra = 1;
        } else if (c >= 0xe0 && c <= 0xef) {
            code = c & 15;
            extra = 2;
        } else if (c >= 0xf0 && c <= 0xf4) {
            code = c & 7;
            extra = 3;
        } else
            return false;
        if (extra > length - i)
            return false;
        for (size_t j = 0; j < extra; j++) {
            c = (unsigned char)text[i++];
            if ((c & 0xc0) != 0x80)
                return false;
            code = (code << 6) | (c & 63);
        }
        if ((extra == 1 && code < 0x80) || (extra == 2 && code < 0x800)
            || (extra == 3 && code < 0x10000) || code > 0x10ffff
            || (code >= 0xd800 && code <= 0xdfff))
            return false;
    }
    if (count)
        *count = n;
    return true;
}

static bool integer_token(const char *s, size_t n) {
    size_t i = n && s[0] == '-' ? 1 : 0;
    if (i == n)
        return false;
    if (s[i] == '0')
        return i + 1 == n;
    if (s[i] < '1' || s[i] > '9')
        return false;
    for (i++; i < n; i++)
        if (s[i] < '0' || s[i] > '9')
            return false;
    return true;
}

static bool check_parsed(yyjson_mut_val *v, char *error, size_t size) {
    if (yyjson_mut_is_raw(v)) {
        const char *s = yyjson_mut_get_raw(v);
        size_t n = yyjson_mut_get_len(v), start = n && s[0] == '-' ? 1 : 0;
        if (!integer_token(s, n))
            return fail(error, size, "non-integer number spelling");
        uint64_t magnitude = 0;
        for (size_t i = start; i < n; i++) {
            unsigned digit = (unsigned)(s[i] - '0');
            if (magnitude > (UINT64_MAX - digit) / 10)
                return true;
            magnitude = magnitude * 10 + digit;
        }
        if (start) {
            if (magnitude <= (UINT64_C(1) << 63))
                yyjson_mut_set_sint(v,
                                    magnitude == (UINT64_C(1) << 63) ? INT64_MIN
                                                                     : -(int64_t)magnitude);
        } else if (magnitude <= INT64_MAX)
            yyjson_mut_set_sint(v, (int64_t)magnitude);
        else
            yyjson_mut_set_uint(v, magnitude);
    } else if (yyjson_mut_is_obj(v)) {
        size_t i, n;
        yyjson_mut_val *k, *item;
        yyjson_mut_obj_foreach(v, i, n, k, item) {
            size_t j, m;
            yyjson_mut_val *other, *ignored;
            yyjson_mut_obj_foreach(v, j, m, other, ignored) {
                (void)ignored;
                if (j >= i)
                    break;
                if (yyjson_mut_get_len(k) == yyjson_mut_get_len(other)
                    && !memcmp(yyjson_mut_get_str(k),
                               yyjson_mut_get_str(other),
                               yyjson_mut_get_len(k)))
                    return fail(error, size, "duplicate JSON key");
            }
            if (!check_parsed(item, error, size))
                return false;
        }
    } else if (yyjson_mut_is_arr(v)) {
        size_t i, n;
        yyjson_mut_val *item;
        yyjson_mut_arr_foreach(v, i, n, item) if (!check_parsed(item, error, size)) return false;
    }
    return true;
}

yyjson_mut_doc *format_json_parse(const char *data, size_t size, char *error, size_t error_size) {
    if (error && error_size)
        error[0] = 0;
    if (!data || (size >= 3 && !memcmp(data, "\xef\xbb\xbf", 3))) {
        fail(error, error_size, "JSON BOM or missing input");
        return NULL;
    }
    yyjson_read_err err;
    yyjson_doc *input = yyjson_read_opts((char *)data, size, YYJSON_READ_NUMBER_AS_RAW, NULL, &err);
    if (!input) {
        fail(error, error_size, err.msg ? err.msg : "invalid JSON");
        return NULL;
    }
    yyjson_mut_doc *doc = yyjson_doc_mut_copy(input, NULL);
    yyjson_doc_free(input);
    if (!doc) {
        fail(error, error_size, "allocation failure");
        return NULL;
    }
    if (!check_parsed(yyjson_mut_doc_get_root(doc), error, error_size)) {
        yyjson_mut_doc_free(doc);
        return NULL;
    }
    return doc;
}

typedef struct {
    char *data;
    size_t size, capacity;
    bool ok;
} buffer_t;

static void append(buffer_t *b, const char *s, size_t n) {
    if (!b->ok)
        return;
    if (n > SIZE_MAX - b->size - 1) {
        b->ok = false;
        return;
    }
    size_t need = b->size + n + 1;
    if (need > b->capacity) {
        size_t capacity = b->capacity ? b->capacity : 128;
        while (capacity < need) {
            if (capacity > SIZE_MAX / 2) {
                capacity = need;
                break;
            }
            capacity *= 2;
        }
        char *data = realloc(b->data, capacity);
        if (!data) {
            b->ok = false;
            return;
        }
        b->data = data;
        b->capacity = capacity;
    }
    memcpy(b->data + b->size, s, n);
    b->size += n;
    b->data[b->size] = 0;
}

static void string(buffer_t *b, yyjson_mut_val *v) {
    const char *s = yyjson_mut_get_str(v);
    size_t n = yyjson_mut_get_len(v);
    if (!s || !unicode(s, n, NULL)) {
        b->ok = false;
        return;
    }
    append(b, "\"", 1);
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 32) {
            static const char hex[] = "0123456789abcdef";
            char escaped[] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 15]};
            append(b, escaped, sizeof(escaped));
        } else {
            if (c == '\\' || c == '"')
                append(b, "\\", 1);
            append(b, s + i, 1);
        }
    }
    append(b, "\"", 1);
}

typedef struct {
    yyjson_mut_val *key, *value;
} pair_t;

static int pair_order(const void *pa, const void *pb) {
    const pair_t *a = pa, *b = pb;
    size_t na = yyjson_mut_get_len(a->key), nb = yyjson_mut_get_len(b->key);
    int cmp = memcmp(yyjson_mut_get_str(a->key), yyjson_mut_get_str(b->key), na < nb ? na : nb);
    return cmp ? cmp : (na > nb) - (na < nb);
}

static void encode(buffer_t *b, yyjson_mut_val *v) {
    if (!v) {
        b->ok = false;
        return;
    }
    if (yyjson_mut_is_str(v))
        string(b, v);
    else if (yyjson_mut_is_null(v))
        append(b, "null", 4);
    else if (yyjson_mut_is_bool(v)) {
        bool value = yyjson_mut_get_bool(v);
        append(b, value ? "true" : "false", value ? 4 : 5);
    } else if (yyjson_mut_is_int(v)) {
        char number[32];
        int n = yyjson_mut_is_sint(v)
                    ? snprintf(number, sizeof(number), "%" PRId64, yyjson_mut_get_sint(v))
                    : snprintf(number, sizeof(number), "%" PRIu64, yyjson_mut_get_uint(v));
        append(b, number, (size_t)n);
    } else if (yyjson_mut_is_raw(v)) {
        const char *s = yyjson_mut_get_raw(v);
        size_t n = yyjson_mut_get_len(v);
        if (!integer_token(s, n))
            b->ok = false;
        else if (n == 2 && !memcmp(s, "-0", 2))
            append(b, "0", 1);
        else
            append(b, s, n);
    } else if (yyjson_mut_is_arr(v)) {
        append(b, "[", 1);
        size_t i, n;
        yyjson_mut_val *item;
        yyjson_mut_arr_foreach(v, i, n, item) {
            if (i)
                append(b, ",", 1);
            encode(b, item);
        }
        append(b, "]", 1);
    } else if (yyjson_mut_is_obj(v)) {
        size_t count = yyjson_mut_obj_size(v);
        if (count > SIZE_MAX / sizeof(pair_t)) {
            b->ok = false;
            return;
        }
        pair_t *pairs = count ? malloc(count * sizeof(*pairs)) : NULL;
        if (count && !pairs) {
            b->ok = false;
            return;
        }
        size_t i, n;
        yyjson_mut_val *key, *item;
        yyjson_mut_obj_foreach(v, i, n, key, item) {
            pairs[i].key = key;
            pairs[i].value = item;
        }
        if (count > 1)
            qsort(pairs, count, sizeof(*pairs), pair_order);
        append(b, "{", 1);
        for (i = 0; i < count; i++) {
            if (i)
                append(b, ",", 1);
            string(b, pairs[i].key);
            append(b, ":", 1);
            encode(b, pairs[i].value);
        }
        append(b, "}", 1);
        free(pairs);
    } else
        b->ok = false;
}

char *format_json_canonical(yyjson_mut_val *value, size_t *size) {
    buffer_t b = {NULL, 0, 0, true};
    encode(&b, value);
    if (!b.ok) {
        free(b.data);
        return NULL;
    }
    if (size)
        *size = b.size;
    return b.data;
}

bool format_json_equal(yyjson_mut_val *a, yyjson_mut_val *b) {
    size_t na = 0, nb = 0;
    char *sa = format_json_canonical(a, &na), *sb = format_json_canonical(b, &nb);
    bool result = sa && sb && na == nb && !memcmp(sa, sb, na);
    free(sa);
    free(sb);
    return result;
}

static bool word(yyjson_mut_val *v, const char *s) {
    return yyjson_mut_is_str(v) && yyjson_mut_get_len(v) == strlen(s)
           && !memcmp(yyjson_mut_get_str(v), s, strlen(s));
}

static bool integer(yyjson_mut_val *v) {
    return yyjson_mut_is_int(v)
           || (yyjson_mut_is_raw(v) && integer_token(yyjson_mut_get_raw(v), yyjson_mut_get_len(v)));
}

static bool type_matches(yyjson_mut_val *v, yyjson_mut_val *type) {
    if (word(type, "null"))
        return yyjson_mut_is_null(v);
    if (word(type, "boolean"))
        return yyjson_mut_is_bool(v);
    if (word(type, "integer"))
        return integer(v);
    if (word(type, "string"))
        return yyjson_mut_is_str(v);
    if (word(type, "object"))
        return yyjson_mut_is_obj(v);
    if (word(type, "array"))
        return yyjson_mut_is_arr(v);
    return false;
}

/* Decimal comparison supports every Python integer, not just host int64.
 * Fixed buffers suffice for machine integers; arbitrary tokens stay in their document. */
static const char *integer_text(yyjson_mut_val *v, char buffer[32], size_t *length) {
    if (yyjson_mut_is_raw(v)) {
        *length = yyjson_mut_get_len(v);
        return yyjson_mut_get_raw(v);
    }
    int count = yyjson_mut_is_sint(v) ? snprintf(buffer, 32, "%" PRId64, yyjson_mut_get_sint(v))
                                      : snprintf(buffer, 32, "%" PRIu64, yyjson_mut_get_uint(v));
    *length = (size_t)count;
    return buffer;
}

static int integer_compare(yyjson_mut_val *a, yyjson_mut_val *b) {
    char ba[32], bb[32];
    size_t na, nb;
    const char *sa = integer_text(a, ba, &na), *sb = integer_text(b, bb, &nb);
    bool minus_a = sa[0] == '-', minus_b = sb[0] == '-';
    if (minus_a != minus_b)
        return minus_a ? -1 : 1;
    const char *da = sa + minus_a, *db = sb + minus_b;
    na -= minus_a;
    nb -= minus_b;
    int result = na == nb ? memcmp(da, db, na) : (na > nb ? 1 : -1);
    return minus_a ? -result : result;
}

static bool ascii_alnum(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

static bool name_char(char c) {
    return ascii_alnum(c) || c == '_' || c == '-';
}

/* These are the five regular languages in the authoritative source, implemented
 * independently of the production pattern compiler. Unknown patterns fail closed. */
static bool pattern(const char *p, const char *s, size_t n) {
    if (!strcmp(p, "^[a-z0-9](?:[a-z0-9._-]*[a-z0-9_-])?$")) {
        if (!n || !ascii_alnum(s[0]) || !name_char(s[n - 1]))
            return false;
        for (size_t i = 1; i < n; i++)
            if (!name_char(s[i]) && s[i] != '.')
                return false;
        return true;
    }
    if (!strcmp(p, "^[0-9a-f]{64}$")) {
        if (n != 64)
            return false;
        for (size_t i = 0; i < n; i++)
            if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
                return false;
        return true;
    }
    if (!strcmp(p, "^goat_fn_[1-9][0-9]*_v1$")) {
        if (n < 12 || memcmp(s, "goat_fn_", 8) || memcmp(s + n - 3, "_v1", 3) || s[8] < '1'
            || s[8] > '9')
            return false;
        for (size_t i = 9; i < n - 3; i++)
            if (s[i] < '0' || s[i] > '9')
                return false;
        return true;
    }
    if (!strcmp(p, "^native/[a-z0-9][a-z0-9._-]*\\.(dll|so)$")) {
        size_t suffix = n >= 4 && !memcmp(s + n - 4, ".dll", 4) ? 4 : 3;
        if (n < 7 + 1 + suffix || memcmp(s, "native/", 7)
            || (suffix == 3 && memcmp(s + n - 3, ".so", 3)) || !ascii_alnum(s[7]))
            return false;
        for (size_t i = 8; i < n - suffix; i++)
            if (!name_char(s[i]) && s[i] != '.')
                return false;
        return true;
    }
    if (!strcmp(p, "^(host|[a-z0-9][a-z0-9_-]*(\\+[a-z0-9_-]+)*)$")) {
        if (!n || !ascii_alnum(s[0]))
            return false;
        for (size_t i = 1; i < n; i++) {
            if (s[i] == '+') {
                if (i + 1 == n || !name_char(s[i + 1]))
                    return false;
            } else if (!name_char(s[i]))
                return false;
        }
        return true;
    }
    return false;
}

static bool bound_size(size_t n, yyjson_mut_val *bound, bool minimum) {
    if (!bound)
        return true;
    if (!integer(bound))
        return false;
    if (yyjson_mut_is_sint(bound) && yyjson_mut_get_sint(bound) < 0)
        return minimum;
    if (yyjson_mut_is_raw(bound))
        return yyjson_mut_get_raw(bound)[0] == '-' ? minimum : !minimum;
    uint64_t value = yyjson_mut_get_uint(bound);
    return minimum ? n >= value : n <= value;
}

bool format_json_shape(yyjson_mut_val *value,
                       yyjson_mut_val *schema,
                       yyjson_mut_val *defs,
                       char *error,
                       size_t error_size) {
    if (!schema || !yyjson_mut_is_obj(schema))
        return fail(error, error_size, "invalid schema");
    yyjson_mut_val *ref = yyjson_mut_obj_get(schema, "$ref");
    if (ref) {
        const char *s = yyjson_mut_get_str(ref);
        const char *name = s ? strrchr(s, '/') : NULL;
        yyjson_mut_val *resolved = name ? yyjson_mut_obj_get(defs, name + 1) : NULL;
        if (!resolved)
            return fail(error, error_size, "unresolved schema reference");
        return format_json_shape(value, resolved, defs, error, error_size);
    }
    yyjson_mut_val *constant = yyjson_mut_obj_get(schema, "const");
    if (constant && !format_json_equal(value, constant))
        return fail(error, error_size, "const");
    yyjson_mut_val *choices = yyjson_mut_obj_get(schema, "enum");
    if (choices) {
        bool found = false;
        size_t i, n;
        yyjson_mut_val *item;
        yyjson_mut_arr_foreach(choices, i, n, item) {
            if (format_json_equal(value, item))
                found = true;
        }
        if (!found)
            return fail(error, error_size, "enum");
    }
    yyjson_mut_val *types = yyjson_mut_obj_get(schema, "type");
    if (types) {
        bool found = yyjson_mut_is_str(types) && type_matches(value, types);
        if (yyjson_mut_is_arr(types)) {
            size_t i, n;
            yyjson_mut_val *item;
            yyjson_mut_arr_foreach(types, i, n, item) {
                if (type_matches(value, item))
                    found = true;
            }
        }
        if (!found)
            return fail(error, error_size, "type");
    }
    if (yyjson_mut_is_obj(value)) {
        yyjson_mut_val *props = yyjson_mut_obj_get(schema, "properties");
        yyjson_mut_val *required = yyjson_mut_obj_get(schema, "required");
        yyjson_mut_val *extra = yyjson_mut_obj_get(schema, "additionalProperties");
        yyjson_mut_val *names = yyjson_mut_obj_get(schema, "propertyNames");
        size_t i, n;
        yyjson_mut_val *item, *key;
        yyjson_mut_arr_foreach(required, i, n, item) {
            if (!yyjson_mut_obj_getn(value, yyjson_mut_get_str(item), yyjson_mut_get_len(item)))
                return fail(error, error_size, "missing field");
        }
        yyjson_mut_obj_foreach(value, i, n, key, item) {
            if (names && !format_json_shape(key, names, defs, error, error_size))
                return false;
            yyjson_mut_val *property =
                yyjson_mut_obj_getn(props, yyjson_mut_get_str(key), yyjson_mut_get_len(key));
            if (property) {
                if (!format_json_shape(item, property, defs, error, error_size))
                    return false;
            } else if (yyjson_mut_is_false(extra))
                return fail(error, error_size, "unknown field");
            else if (yyjson_mut_is_obj(extra)
                     && !format_json_shape(item, extra, defs, error, error_size))
                return false;
        }
    } else if (yyjson_mut_is_arr(value)) {
        size_t count = yyjson_mut_arr_size(value);
        if (!bound_size(count, yyjson_mut_obj_get(schema, "minItems"), true)
            || !bound_size(count, yyjson_mut_obj_get(schema, "maxItems"), false))
            return fail(error, error_size, "array length");
        yyjson_mut_val *items = yyjson_mut_obj_get(schema, "items");
        bool unique = yyjson_mut_is_true(yyjson_mut_obj_get(schema, "uniqueItems"));
        size_t i, n;
        yyjson_mut_val *item;
        yyjson_mut_arr_foreach(value, i, n, item) {
            if (items && !format_json_shape(item, items, defs, error, error_size))
                return false;
            if (unique)
                for (size_t j = 0; j < i; j++)
                    if (format_json_equal(item, yyjson_mut_arr_get(value, j)))
                        return fail(error, error_size, "duplicate item");
        }
    } else if (yyjson_mut_is_str(value)) {
        const char *s = yyjson_mut_get_str(value);
        size_t n;
        if (!unicode(s, yyjson_mut_get_len(value), &n))
            return fail(error, error_size, "invalid Unicode");
        if (!bound_size(n, yyjson_mut_obj_get(schema, "minLength"), true)
            || !bound_size(n, yyjson_mut_obj_get(schema, "maxLength"), false))
            return fail(error, error_size, "string length");
        yyjson_mut_val *pat = yyjson_mut_obj_get(schema, "pattern");
        if (pat && !pattern(yyjson_mut_get_str(pat), s, yyjson_mut_get_len(value)))
            return fail(error, error_size, "pattern");
    } else if (integer(value)) {
        yyjson_mut_val *lo = yyjson_mut_obj_get(schema, "minimum"),
                       *hi = yyjson_mut_obj_get(schema, "maximum");
        if ((lo && integer_compare(value, lo) < 0) || (hi && integer_compare(value, hi) > 0))
            return fail(error, error_size, "range");
    }
    return true;
}
