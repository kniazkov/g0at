/** @file format_fixtures.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Port of module_interface_fixtures.py, independent of production bytecode.
 */
#include "format_fixtures.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned char *data;
    size_t size, capacity;
} buffer;

typedef struct {
    yyjson_mut_val *key, *value;
} external;

static uint32_t get32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put32(unsigned char *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (unsigned char)(v >> (8 * i));
}

static bool append(buffer *b, const void *p, size_t n) {
    if (n > UINT32_MAX - b->size)
        return false;
    size_t total = b->size + n;
    if (total > b->capacity) {
        size_t cap = total < 128 ? 128 : total;
        unsigned char *next = realloc(b->data, cap);
        if (!next)
            return false;
        b->data = next;
        b->capacity = cap;
    }
    if (n)
        memcpy(b->data + b->size, p, n);
    b->size = total;
    return true;
}

static bool word(buffer *b, uint32_t v) {
    unsigned char p[4];
    put32(p, v);
    return append(b, p, 4);
}

static bool align8(buffer *b) {
    static const unsigned char zeros[8] = {0};
    return append(b, zeros, (8 - b->size % 8) % 8);
}

static bool record(buffer *b, uint32_t a, uint32_t c, uint32_t d, uint32_t e) {
    return word(b, a) && word(b, c) && word(b, d) && word(b, e);
}

static bool instruction(buffer *b, unsigned char op, uint32_t arg) {
    unsigned char p[8] = {0};
    p[0] = op;
    put32(p + 4, arg);
    return append(b, p, 8);
}

static int compare_external(const void *a, const void *b) {
    const external *x = a, *y = b;
    size_t nx = yyjson_mut_get_len(x->key), ny = yyjson_mut_get_len(y->key);
    int c = memcmp(yyjson_mut_get_str(x->key), yyjson_mut_get_str(y->key), nx < ny ? nx : ny);
    return c ? c : (nx > ny) - (nx < ny);
}

/* Strict UTF-8 decoding preserves non-BMP characters and embedded U+0000. */
static bool utf32(buffer *b, const unsigned char *p, size_t n, uint32_t *count) {
    *count = 0;
    for (size_t i = 0; i < n;) {
        uint32_t c = p[i++], minimum = 0;
        unsigned tail = 0;
        if (c >= 0xc2 && c <= 0xdf) {
            c &= 31;
            tail = 1;
            minimum = 0x80;
        } else if (c >= 0xe0 && c <= 0xef) {
            c &= 15;
            tail = 2;
            minimum = 0x800;
        } else if (c >= 0xf0 && c <= 0xf4) {
            c &= 7;
            tail = 3;
            minimum = 0x10000;
        } else if (c >= 0x80)
            return false;
        if (tail > n - i)
            return false;
        for (unsigned j = 0; j < tail; ++j) {
            unsigned char t = p[i++];
            if ((t & 0xc0) != 0x80)
                return false;
            c = (c << 6) | (t & 63);
        }
        if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
            return false;
        if (!word(b, c))
            return false;
        ++*count;
    }
    return word(b, 0);
}

unsigned char *format_fixture_external_declarations(yyjson_mut_doc *doc,
                                                    yyjson_mut_val *metadata,
                                                    const unsigned char *image,
                                                    size_t image_size,
                                                    size_t *result_size,
                                                    char *error,
                                                    size_t error_size) {
    buffer instructions = {0}, descriptors = {0}, payload = {0}, output = {0};
    external *items = NULL;
    const char *reason = "invalid external declaration fixture";
    if (result_size)
        *result_size = 0;
    if (error && error_size)
        error[0] = 0;
    if (!doc || !metadata || !result_size || (!image && image_size))
        goto fail;
    yyjson_mut_val *exports = yyjson_mut_obj_get(metadata, "externals");
    size_t count = exports ? yyjson_mut_obj_size(exports) : 0;
    if (exports && !yyjson_mut_is_obj(exports))
        goto fail;
    if (!count) {
        output.data = malloc(image_size ? image_size : 1);
        if (!output.data)
            goto fail;
        if (image_size)
            memcpy(output.data, image, image_size);
        *result_size = image_size;
        return output.data;
    }
    if (image_size < 64 || image_size > UINT32_MAX || memcmp(image, "GOATBC01", 8))
        goto fail;
    uint32_t h[15] = {0};
    for (unsigned i = 1; i < 15; ++i)
        h[i] = get32(image + 4 + i * 4);
    yyjson_mut_val *main = yyjson_mut_obj_get(metadata, "main");
    yyjson_mut_val *file = yyjson_mut_arr_get(yyjson_mut_obj_get(metadata, "files"), 0);
    if (!yyjson_mut_is_obj(file) || !yyjson_mut_is_int(main) || yyjson_mut_get_uint(main) != 0
        || h[6] != 1 || h[9] != 64 || h[3] != image_size)
        goto fail;
    for (unsigned i = 9; i < 14; ++i)
        if (h[i] > h[i + 1])
            goto fail;
    if (h[14] > image_size || (uint64_t)h[10] - h[9] != (uint64_t)h[4] * 8
        || (uint64_t)h[11] - h[10] != (uint64_t)h[5] * 16 || h[12] - h[11] != 16
        || (uint64_t)h[13] - h[12] != (uint64_t)h[7] * 32
        || (uint64_t)h[14] - h[13] != (uint64_t)h[8] * 24
        || count > (UINT32_MAX - h[4] - (uint64_t)1) / 2 || count > (UINT32_MAX - h[5]) / 2
        || h[8] == UINT32_MAX || count > SIZE_MAX / sizeof(*items))
        goto fail;
    items = malloc(count * sizeof(*items));
    if (!items)
        goto fail;
    size_t index, maximum;
    yyjson_mut_val *key, *value;
    yyjson_mut_obj_foreach(exports, index, maximum, key, value) {
        items[index].key = key;
        items[index].value = value;
        if (!yyjson_mut_is_bool(yyjson_mut_obj_get(value, "mutable")))
            goto fail;
    }
    qsort(items, count, sizeof(*items), compare_external);
    if (!append(&instructions, image + h[9], h[10] - h[9])
        || !append(&descriptors, image + h[10], h[11] - h[10])
        || !append(&payload, image + h[14], image_size - h[14]))
        goto fail;
    uint32_t nd = h[5];
    for (size_t i = 0; i < count; ++i) {
        uint32_t chars;
        if (!align8(&payload))
            goto fail;
        uint32_t offset = (uint32_t)payload.size;
        if (!utf32(&payload,
                   (const unsigned char *)yyjson_mut_get_str(items[i].key),
                   yyjson_mut_get_len(items[i].key),
                   &chars)
            || !record(&descriptors, offset, (uint32_t)payload.size - offset, 1, chars)
            || !align8(&payload))
            goto fail;
        offset = (uint32_t)payload.size;
        if (!record(&payload, 0x4445434c, nd, 0, 2) || !record(&descriptors, offset, 16, 3, 1)
            || !instruction(&instructions, 7, 0)
            || !instruction(&instructions,
                            yyjson_mut_get_bool(yyjson_mut_obj_get(items[i].value, "mutable")) ? 69
                                                                                               : 70,
                            nd + 1))
            goto fail;
        nd += 2;
    }
    uint32_t file_record[4];
    for (unsigned i = 0; i < 4; ++i)
        file_record[i] = get32(image + h[11] + i * 4);
    uint32_t extra_count = (uint32_t)(2 * count + 1);
    if (file_record[1] > UINT32_MAX - extra_count || !instruction(&instructions, 3, file_record[2]))
        goto fail;
    file_record[1] += extra_count;
    file_record[2] = h[4];
    if (!append(&output, image, 64) || !append(&output, instructions.data, instructions.size)
        || !append(&output, descriptors.data, descriptors.size)
        || !record(&output, file_record[0], file_record[1], file_record[2], file_record[3])
        || !append(&output, image + h[12], h[14] - h[12])
        || !record(&output, h[4], extra_count, 0, 0) || !word(&output, 0) || !word(&output, 0)
        || !append(&output, payload.data, payload.size))
        goto fail;
    uint32_t entry = h[4];
    h[4] += extra_count;
    h[5] = nd;
    h[8]++;
    uint32_t functions_size = h[13] - h[12], locations_size = h[14] - h[13];
    h[10] = h[9] + (uint32_t)instructions.size;
    h[11] = h[10] + (uint32_t)descriptors.size;
    h[12] = h[11] + 16;
    h[13] = h[12] + functions_size;
    h[14] = h[13] + locations_size + 24;
    h[3] = (uint32_t)output.size;
    for (unsigned i = 1; i < 15; ++i)
        put32(output.data + 4 + i * 4, h[i]);
    yyjson_mut_val *entry_key = yyjson_mut_str(doc, "entry"),
                   *entry_value = yyjson_mut_uint(doc, entry);
    if (!entry_key || !entry_value || !yyjson_mut_obj_put(file, entry_key, entry_value))
        goto fail;
    free(items);
    free(instructions.data);
    free(descriptors.data);
    free(payload.data);
    *result_size = output.size;
    return output.data;
fail:
    free(items);
    free(instructions.data);
    free(descriptors.data);
    free(payload.data);
    free(output.data);
    if (error && error_size)
        snprintf(error, error_size, "%s", reason);
    return NULL;
}
