/** @file json_canonical.c
 * @brief Locale-independent canonical JSON bytes for module v1.
 */
#include "module_json_internal.h"

#include <stdlib.h>
#include <string.h>

/* No intermediate double, signed overflow, or platform-sized integer. */
bool goat_json_integer(const yyjson_val *v, int64_t *out) {
    if (yyjson_is_sint(v)) {
        *out = yyjson_get_sint(v);
        return true;
    }
    if (yyjson_is_uint(v)) {
        uint64_t value = yyjson_get_uint(v);
        if (value > INT64_MAX)
            return false;
        *out = (int64_t)value;
        return true;
    }
    if (!yyjson_is_raw(v))
        return false;
    const char *s = yyjson_get_raw(v);
    size_t n = yyjson_get_len(v), i = 0;
    bool negative = n && s[0] == '-';
    if (negative)
        i++;
    if (i == n || (s[i] == '0' && i + 1 != n))
        return false;
    uint64_t value = 0;
    uint64_t limit = negative ? UINT64_C(9223372036854775808) : INT64_MAX;
    for (; i < n; i++) {
        unsigned digit = (unsigned)(s[i] - '0');
        if (digit > 9 || value > (limit - digit) / 10)
            return false;
        value = value * 10 + digit;
    }
    *out = negative ? (value == UINT64_C(9223372036854775808) ? INT64_MIN : -(int64_t)value)
                    : (int64_t)value;
    return true;
}

size_t goat_json_scalar_count(const char *text, size_t length) {
    if (!text && length)
        return SIZE_MAX;
    size_t count = 0;
    for (size_t i = 0; i < length; count++) {
        unsigned ch = (unsigned char)text[i++];
        if (ch < 0x80)
            continue;
        unsigned n, scalar, minimum;
        if (ch >= 0xc2 && ch <= 0xdf) {
            n = 1;
            scalar = ch & 0x1f;
            minimum = 0x80;
        } else if (ch >= 0xe0 && ch <= 0xef) {
            n = 2;
            scalar = ch & 0xf;
            minimum = 0x800;
        } else if (ch >= 0xf0 && ch <= 0xf4) {
            n = 3;
            scalar = ch & 7;
            minimum = 0x10000;
        } else
            return SIZE_MAX;
        if (length - i < n)
            return SIZE_MAX;
        while (n--) {
            unsigned next = (unsigned char)text[i++];
            if ((next & 0xc0) != 0x80)
                return SIZE_MAX;
            scalar = (scalar << 6) | (next & 0x3f);
        }
        if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))
            return SIZE_MAX;
    }
    return count;
}

typedef struct {
    char *data;
    size_t length, capacity;
    goat_module_json_error error;
    bool main_metadata;
} output_buffer;

static void append(output_buffer *out, const char *data, size_t length) {
    if (out->error)
        return;
    if (length > SIZE_MAX - out->length - 1) {
        out->error = GOAT_MODULE_JSON_LIMIT;
        return;
    }
    size_t needed = out->length + length + 1;
    if (needed > out->capacity) {
        size_t capacity = out->capacity ? out->capacity : 128;
        while (capacity < needed) {
            if (capacity > SIZE_MAX / 2) {
                capacity = needed;
                break;
            }
            capacity *= 2;
        }
        char *buffer = realloc(out->data, capacity);
        if (!buffer) {
            out->error = GOAT_MODULE_JSON_MEMORY;
            return;
        }
        out->data = buffer;
        out->capacity = capacity;
    }
    memcpy(out->data + out->length, data, length);
    out->length += length;
    out->data[out->length] = 0;
}

static void string(output_buffer *out, const yyjson_val *value) {
    const char *text = yyjson_get_str(value);
    size_t length = yyjson_get_len(value);
    if (goat_json_scalar_count(text, length) == SIZE_MAX) {
        out->error = GOAT_MODULE_JSON_INVALID_FIELD;
        return;
    }
    append(out, "\"", 1);
    size_t start = 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char ch = (unsigned char)text[i];
        if (ch >= 32 && ch != '"' && ch != '\\')
            continue;
        append(out, text + start, i - start);
        if (ch < 32) {
            const char *hex = "0123456789abcdef";
            char escape[] = {'\\', 'u', '0', '0', hex[ch >> 4], hex[ch & 15]};
            append(out, escape, 6);
        } else {
            char escape[] = {'\\', (char)ch};
            append(out, escape, 2);
        }
        start = i + 1;
    }
    append(out, text + start, length - start);
    append(out, "\"", 1);
}

static int key_compare(const void *left, const void *right) {
    const yyjson_val *a = *(const yyjson_val *const *)left;
    const yyjson_val *b = *(const yyjson_val *const *)right;
    size_t na = yyjson_get_len(a), nb = yyjson_get_len(b);
    int cmp = memcmp(yyjson_get_str(a), yyjson_get_str(b), na < nb ? na : nb);
    return cmp ? cmp : (na > nb) - (na < nb);
}

static bool main_key(yyjson_val *key) {
    const char *fields[] = {"formatVersion",
                            "bytecodeVersion",
                            "name",
                            "version",
                            "main",
                            "files",
                            "dependencies",
                            "functions",
                            "externals"};
    for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); i++)
        if (yyjson_get_len(key) == strlen(fields[i])
            && !memcmp(yyjson_get_str(key), fields[i], strlen(fields[i])))
            return true;
    return false;
}

static void encode(output_buffer *out, const yyjson_val *value, size_t depth) {
    if (out->error)
        return;
    if (!value || depth > GOAT_JSON_MAX_DEPTH) {
        out->error = value ? GOAT_MODULE_JSON_LIMIT : GOAT_MODULE_JSON_INVALID_FIELD;
        return;
    }
    if (yyjson_is_obj(value)) {
        size_t n = yyjson_obj_size(value);
        if (n > SIZE_MAX / sizeof(yyjson_val *)) {
            out->error = GOAT_MODULE_JSON_LIMIT;
            return;
        }
        yyjson_val **keys = n ? malloc(n * sizeof(*keys)) : NULL;
        if (n && !keys) {
            out->error = GOAT_MODULE_JSON_MEMORY;
            return;
        }
        yyjson_obj_iter it = yyjson_obj_iter_with((yyjson_val *)value);
        yyjson_val *key;
        size_t i = 0;
        while ((key = yyjson_obj_iter_next(&it)))
            keys[i++] = key;
        if (n > 1)
            qsort(keys, n, sizeof(*keys), key_compare);
        append(out, "{", 1);
        bool first = true;
        for (i = 0; i < n; i++) {
            if (i && !key_compare(keys + i - 1, keys + i)) {
                out->error = GOAT_MODULE_JSON_DUPLICATE_KEY;
                break;
            }
            if (depth == 0 && out->main_metadata && !main_key(keys[i]))
                continue;
            if (!first)
                append(out, ",", 1);
            first = false;
            string(out, keys[i]);
            append(out, ":", 1);
            encode(out, yyjson_obj_iter_get_val(keys[i]), depth + 1);
        }
        append(out, "}", 1);
        free(keys);
    } else if (yyjson_is_arr(value)) {
        append(out, "[", 1);
        yyjson_arr_iter it = yyjson_arr_iter_with((yyjson_val *)value);
        yyjson_val *item;
        bool first = true;
        while ((item = yyjson_arr_iter_next(&it))) {
            if (!first)
                append(out, ",", 1);
            first = false;
            encode(out, item, depth + 1);
        }
        append(out, "]", 1);
    } else if (yyjson_is_str(value))
        string(out, value);
    else if (yyjson_is_null(value))
        append(out, "null", 4);
    else if (yyjson_is_bool(value))
        append(out, yyjson_get_bool(value) ? "true" : "false", yyjson_get_bool(value) ? 4 : 5);
    else {
        int64_t number;
        if (!goat_json_integer(value, &number)) {
            out->error = GOAT_MODULE_JSON_INVALID_FIELD;
            return;
        }
        char buffer[21];
        size_t pos = sizeof(buffer);
        uint64_t magnitude = number < 0 ? (uint64_t)(-(number + 1)) + 1 : (uint64_t)number;
        do {
            buffer[--pos] = (char)('0' + magnitude % 10);
            magnitude /= 10;
        } while (magnitude);
        if (number < 0)
            buffer[--pos] = '-';
        append(out, buffer + pos, sizeof(buffer) - pos);
    }
}

static goat_module_json_error
write_value(const yyjson_val *value, bool main_metadata, char **out, size_t *length) {
    if (out)
        *out = NULL;
    if (length)
        *length = 0;
    if (!out || !length)
        return GOAT_MODULE_JSON_INVALID_FIELD;
    output_buffer buffer = {.main_metadata = main_metadata};
    encode(&buffer, value, 0);
    if (buffer.error) {
        free(buffer.data);
        return buffer.error;
    }
    *out = buffer.data;
    *length = buffer.length;
    return GOAT_MODULE_JSON_OK;
}

goat_module_json_error goat_json_canonical(const yyjson_val *value, char **out, size_t *length) {
    return write_value(value, false, out, length);
}

goat_module_json_error goat_module_json_write(const yyjson_doc *doc,
                                              goat_module_json_kind kind,
                                              bool main_metadata,
                                              char **out,
                                              size_t *length) {
    if (out)
        *out = NULL;
    if (length)
        *length = 0;
    if (!doc || !out || !length || (main_metadata && kind != GOAT_MODULE_METADATA))
        return GOAT_MODULE_JSON_INVALID_FIELD;
    char *input = NULL;
    size_t input_length;
    goat_module_json_error error =
        goat_json_canonical(yyjson_doc_get_root(doc), &input, &input_length);
    if (error)
        return error;
    yyjson_doc *normalized = NULL;
    error = goat_module_json_read(input, input_length, kind, &normalized);
    free(input);
    if (!error) {
        error = write_value(yyjson_doc_get_root(normalized), main_metadata, out, length);
        yyjson_doc_free(normalized);
    }
    return error;
}

void goat_module_json_free(char *data) {
    free(data);
}
