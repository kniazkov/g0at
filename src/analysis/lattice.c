/**
 * @file lattice.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Join and meet operations for abstract-interpretation lattice elements.
 *
 * This file implements the two fundamental lattice operations:
 * - join: computes the least upper bound of two abstract values;
 * - meet: computes the greatest lower bound of two abstract values.
 *
 * In less ceremonial terms, join merges facts coming from different execution
 * paths, while meet narrows facts when additional constraints are known. This
 * is where uncertainty is either politely combined or beaten into a smaller
 * shape.
 */

#include "lattice.h"

/**
 * @brief Top lattice element singleton.
 */
static const lattice_element_t top_element = {
    .type = LATTICE_TOP
};

/**
 * @brief Not-null lattice element singleton.
 */
static const lattice_element_t not_null_element = {
    .type = LATTICE_NOT_NULL
};

/**
 * @brief Null lattice element singleton.
 */
static const lattice_element_t null_element = {
    .type = LATTICE_NULL
};

/**
 * @brief Numeric lattice element singleton.
 */
static const lattice_element_t numeric_element = {
    .type = LATTICE_NUMERIC
};

/**
 * @brief Integer lattice element singleton.
 */
static const lattice_element_t integer_element = {
    .type = LATTICE_INTEGER
};

/**
 * @brief Real lattice element singleton.
 */
static const lattice_element_t real_element = {
    .type = LATTICE_REAL
};

/**
 * @brief String lattice element singleton.
 */
static const lattice_element_t string_element = {
    .type = LATTICE_STRING
};

/**
 * @brief Boolean lattice element singleton.
 */
static const lattice_element_t boolean_element = {
    .type = LATTICE_BOOLEAN
};

/**
 * @brief True lattice element singleton.
 */
static const lattice_element_t true_element = {
    .type = LATTICE_TRUE
};

/**
 * @brief False lattice element singleton.
 */
static const lattice_element_t false_element = {
    .type = LATTICE_FALSE
};

/**
 * @brief Function lattice element singleton.
 */
static const lattice_element_t function_element = {
    .type = LATTICE_FUNCTION
};

/**
 * @brief Array lattice element singleton.
 */
static const lattice_element_t array_element = {
    .type = LATTICE_ARRAY
};

/**
 * @brief User-defined object lattice element singleton.
 */
static const lattice_element_t user_defined_object_element = {
    .type = LATTICE_USER_DEFINED_OBJECT
};

/**
 * @brief Bottom lattice element singleton.
 */
static const lattice_element_t bottom_element = {
    .type = LATTICE_BOTTOM
};

const lattice_element_t *make_top_element() {
    return &top_element;
}

const lattice_element_t *make_not_null_element() {
    return &not_null_element;
}

const lattice_element_t *make_null_element() {
    return &null_element;
}

const lattice_element_t *make_numeric_element() {
    return &numeric_element;
}

const lattice_element_t *make_integer_element() {
    return &integer_element;
}

const lattice_element_t *make_integer_range_element(arena_t *arena, int64_t min, int64_t max) {
    integer_range_lattice_element_t *element =
        alloc_from_arena(arena, sizeof(integer_range_lattice_element_t));
    element->base.type = LATTICE_INTEGER_RANGE;
    element->min = min;
    element->max = max;
    return &element->base;
}

const lattice_element_t *make_integer_constant_element(arena_t *arena, int64_t value) {
    integer_constant_lattice_element_t *element =
        alloc_from_arena(arena, sizeof(integer_constant_lattice_element_t));
    element->base.type = LATTICE_INTEGER_CONSTANT;
    element->value = value;
    return &element->base;
}

const lattice_element_t *make_real_element() {
    return &real_element;
}

const lattice_element_t *make_real_constant_element(arena_t *arena, double value) {
    real_constant_lattice_element_t *element =
        alloc_from_arena(arena, sizeof(real_constant_lattice_element_t));
    element->base.type = LATTICE_REAL_CONSTANT;
    element->value = value;
    return &element->base;
}

const lattice_element_t *make_string_element() {
    return &string_element;
}

const lattice_element_t *make_string_constant_element(arena_t *arena, string_view_t value) {
    string_constant_lattice_element_t *element =
        alloc_from_arena(arena, sizeof(string_constant_lattice_element_t));
    element->base.type = LATTICE_STRING_CONSTANT;
    element->value = value;
    return &element->base;
}

const lattice_element_t *make_boolean_element() {
    return &boolean_element;
}

const lattice_element_t *make_true_element() {
    return &true_element;
}

const lattice_element_t *make_false_element() {
    return &false_element;
}

const lattice_element_t *make_function_element() {
    return &function_element;
}

const lattice_element_t *make_array_element() {
    return &array_element;
}

const lattice_element_t *make_typed_array_element(arena_t *arena, lattice_type_t element_type) {
    typed_array_lattice_element_t *element =
        alloc_from_arena(arena, sizeof(typed_array_lattice_element_t));
    element->base.type = LATTICE_TYPED_ARRAY;
    element->element_type = element_type;
    return &element->base;
}

const lattice_element_t *make_user_defined_object_element() {
    return &user_defined_object_element;
}

const lattice_element_t *make_bottom_element() {
    return &bottom_element;
}

const lattice_element_t *lattice_join(arena_t *arena,
        const lattice_element_t *left, const lattice_element_t *right) {
    return make_top_element();
}

const lattice_element_t *lattice_meet(arena_t *arena,
        const lattice_element_t *left, const lattice_element_t *right) {
    return make_bottom_element();
}
