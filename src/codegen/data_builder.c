/**
 * @file data_builder.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implements the functions for managing data building in the Goat virtual machine.
 */

#include "data_builder.h"

#include "graph/declarations.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"

#include <memory.h>
#include <stdlib.h>

struct declaration_entry_t {
    const declarator_t *declaration;
    uint32_t index;
    struct declaration_entry_t *next;
};

uint32_t add_declaration_to_data_segment(data_builder_t *builder, const declarator_t *decl) {
    for (struct declaration_entry_t *e = builder->declarations; e; e = e->next)
        if (e->declaration == decl)
            return e->index;
    declaration_descriptor_t descriptor = {DECLARATION_TAG,
                                           add_string_to_data_segment_ex(builder, decl->name),
                                           0,
                                           decl->modifier};
    struct declaration_entry_t *entry = ALLOC(sizeof(*entry));
    *entry = (struct declaration_entry_t){
        decl,
        add_data_to_data_segment(builder, &descriptor, sizeof(descriptor)),
        builder->declarations};
    builder->declarations = entry;
    return entry->index;
}

/** @brief Initial capacity for the descriptors list */
#define INITIAL_DESCRIPTORS_LIST_CAPACITY 16

/** @brief Initial capacity for the data array */
#define INITIAL_DATA_ARRAY_CAPACITY 256

/** @brief Deduplication keys must survive growth of the packed data buffer. */
static int compare_string_keys(const void *a, const void *b) {
    const string_view_t *left = a, *right = b;
    size_t n = left->length < right->length ? left->length : right->length;
    int result = MEMCMP(left->data, right->data, n);
    return result ? result : left->length < right->length ? -1 : left->length > right->length;
}

static void *copy_string_key(void *key) {
    string_view_t *view = key;
    if (view->length > (SIZE_MAX - sizeof(*view)) / sizeof(gchar_t) - 1)
        abort();
    string_view_t *copy = ALLOC(sizeof(*copy) + (view->length + 1) * sizeof(gchar_t));
    *copy = (string_view_t){(gchar_t *)(copy + 1), view->length};
    MEMCPY((gchar_t *)copy->data, view->data, view->length);
    ((gchar_t *)copy->data)[view->length] = 0;
    return copy;
}

data_builder_t *create_data_builder(void) {
    data_builder_t *builder = (data_builder_t *)ALLOC(sizeof(data_builder_t));
    builder->descriptors =
        (data_descriptor_t *)ALLOC(INITIAL_DESCRIPTORS_LIST_CAPACITY * sizeof(data_descriptor_t));
    builder->data = (uint8_t *)ALLOC(INITIAL_DATA_ARRAY_CAPACITY * sizeof(uint8_t));
    builder->data_size = 0;
    builder->data_capacity = INITIAL_DATA_ARRAY_CAPACITY;
    builder->descriptors_count = 0;
    builder->descriptors_capacity = INITIAL_DESCRIPTORS_LIST_CAPACITY;
    builder->strings = create_avl_tree(compare_string_keys);
    builder->declarations = NULL;
    builder->strings->copy_key = copy_string_key;
    builder->strings->destroy_key = FREE;
    return builder;
}

uint32_t add_data_to_data_segment(data_builder_t *builder, void *data, size_t size) {
    if (size > UINT32_MAX - 3 || builder->data_size > SIZE_MAX - size - 3
        || builder->descriptors_count >= UINT32_MAX - 1)
        abort();
    size_t aligned_size = (size + 3) & ~(size_t)3;
    size_t new_size = builder->data_size + aligned_size;
    if (new_size > builder->data_capacity) {
        size_t new_capacity = builder->data_capacity * 2;
        if (new_capacity < new_size) {
            new_capacity = new_size;
        }
        builder->data_capacity = new_capacity;
        uint8_t *new_data = (uint8_t *)ALLOC(new_capacity);
        memcpy(new_data, builder->data, builder->data_size);
        FREE(builder->data);
        builder->data = new_data;
    }
    size_t offset = builder->data_size;
    builder->data_size += aligned_size;
    memcpy(&builder->data[offset], data, size);
    if (aligned_size > size) {
        memset(&builder->data[offset + size], 0, aligned_size - size);
    }
    if (builder->descriptors_count >= builder->descriptors_capacity) {
        builder->descriptors_capacity *= 2;
        data_descriptor_t *new_descriptors =
            (data_descriptor_t *)ALLOC(builder->descriptors_capacity * sizeof(data_descriptor_t));
        for (size_t descr_idx = 0; descr_idx < builder->descriptors_count; ++descr_idx) {
            new_descriptors[descr_idx] = builder->descriptors[descr_idx];
        }
        FREE(builder->descriptors);
        builder->descriptors = new_descriptors;
    }
    uint32_t index = (uint32_t)builder->descriptors_count++;
    data_descriptor_t descriptor = {.offset = (uint64_t)offset, .size = (uint32_t)size};
    builder->descriptors[index] = descriptor;
    return index;
}

uint32_t add_string_to_data_segment(data_builder_t *builder, const gchar_t *string) {
    return add_string_to_data_segment_ex(builder, (string_view_t){string, STRLEN(string)});
}

uint32_t add_string_to_data_segment_ex(data_builder_t *builder, string_view_t string) {
    uint32_t old_index = get_from_avl_tree(builder->strings, &string).uint32_val;
    if (old_index)
        return old_index - 1;
    if (string.length > UINT32_MAX / 4 - 1)
        abort();
    size_t bytes = (string.length + 1) * 4;
    uint8_t *encoded = CALLOC(bytes);
    for (size_t i = 0; i < string.length; ++i) {
        uint32_t scalar = string.data[i];
        if (!goat_is_scalar(scalar))
            abort();
        for (unsigned j = 0; j < 4; ++j)
            encoded[4 * i + j] = (uint8_t)(scalar >> (8 * j));
    }
    uint32_t index = add_data_to_data_segment(builder, encoded, bytes);
    FREE(encoded);
    set_in_avl_tree(builder->strings, &string, (value_t){.uint32_val = index + 1});
    return index;
}

void destroy_data_builder(data_builder_t *builder) {
    while (builder->declarations) {
        struct declaration_entry_t *entry = builder->declarations;
        builder->declarations = entry->next;
        FREE(entry);
    }
    FREE(builder->descriptors);
    FREE(builder->data);
    destroy_avl_tree(builder->strings);
    FREE(builder);
}
