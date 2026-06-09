/**
 * @file lattice.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of the abstract-value lattice.
 *
 * This file implements construction, formatting, join, and meet operations for
 * lattice elements declared in @ref lattice.h.
 *
 * The implementation intentionally keeps the lattice rules explicit. Each
 * non-trivial element kind has a small dedicated helper for @ref lattice_join
 * and @ref lattice_meet. This makes the ordering rules visible in code instead
 * of hiding them behind a clever generic mechanism, because clever generic
 * mechanisms in C age about as well as milk in a server room.
 *
 * General elements such as top, bottom, null, numeric, string, boolean,
 * function, array, and user-defined object are represented as immutable
 * singletons. More specific elements that carry payload data, such as integer
 * ranges, integer constants, real constants, string constants, and typed arrays,
 * are allocated from the caller-provided arena.
 *
 * The lattice follows these broad rules:
 * - @ref LATTICE_TOP is the least upper bound of null and all non-null values;
 * - @ref LATTICE_BOTTOM is the empty set of possible values;
 * - @ref LATTICE_NOT_NULL contains every non-null value category;
 * - @ref LATTICE_NUMERIC contains integer and real values;
 * - constants and ranges are preserved when possible and widened only when the
 *   join operation requires it;
 * - meet narrows values and returns bottom when two facts are incompatible.
 *
 * The string conversion helpers produce compact human-readable labels intended
 * for graph output and debugging.
 */

#include "lattice.h"
#include "lib/string_ext.h"

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
    integer_range_element_t *element = alloc_from_arena(arena, sizeof(integer_range_element_t));
    element->base.type = LATTICE_INTEGER_RANGE;
    element->min = min;
    element->max = max;
    return &element->base;
}

const lattice_element_t *make_integer_constant_element(arena_t *arena, int64_t value) {
    integer_constant_element_t *element =
        alloc_from_arena(arena, sizeof(integer_constant_element_t));
    element->base.type = LATTICE_INTEGER_CONSTANT;
    element->value = value;
    return &element->base;
}

const lattice_element_t *make_real_element() {
    return &real_element;
}

const lattice_element_t *make_real_constant_element(arena_t *arena, double value) {
    real_constant_element_t *element = alloc_from_arena(arena, sizeof(real_constant_element_t));
    element->base.type = LATTICE_REAL_CONSTANT;
    element->value = value;
    return &element->base;
}

const lattice_element_t *make_string_element() {
    return &string_element;
}

const lattice_element_t *make_string_constant_element(arena_t *arena, string_view_t value) {
    string_constant_element_t *element = alloc_from_arena(arena, sizeof(string_constant_element_t));
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
    typed_array_element_t *element = alloc_from_arena(arena, sizeof(typed_array_element_t));
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

/**
 * @brief Joins NOT_NULL with another lattice element.
 *
 * The left operand is assumed to be exactly LATTICE_NOT_NULL.
 *
 * @param right Right lattice element.
 * @return Constant pointer to the least upper bound.
 */
static const lattice_element_t *lattice_join_not_null(const lattice_element_t *right) {
    if (right->type == LATTICE_TOP || right->type == LATTICE_NULL) {
        return make_top_element();
    }
    return make_not_null_element();
}

/**
 * @brief Joins NULL with another lattice element.
 *
 * The left operand is assumed to be exactly LATTICE_NULL.
 *
 * @param right Right lattice element.
 * @return Constant pointer to the least upper bound.
 */
static const lattice_element_t *lattice_join_null(const lattice_element_t *right) {
    if (right->type == LATTICE_BOTTOM || right->type == LATTICE_NULL) {
        return make_null_element();
    }
    return make_top_element();
}

/**
 * @brief Joins NUMERIC with another lattice element.
 *
 * The left operand is assumed to be exactly LATTICE_NUMERIC.
 *
 * @param right Right lattice element.
 * @return Constant pointer to the least upper bound.
 */
static const lattice_element_t *lattice_join_numeric(const lattice_element_t *right) {
    switch (right->type) {
        case LATTICE_BOTTOM:
        case LATTICE_NUMERIC:
        case LATTICE_INTEGER:
        case LATTICE_INTEGER_RANGE:
        case LATTICE_INTEGER_CONSTANT:
        case LATTICE_REAL:
        case LATTICE_REAL_CONSTANT:
            return make_numeric_element();

        case LATTICE_TOP:
        case LATTICE_NULL:
            return make_top_element();

        case LATTICE_NOT_NULL:
        case LATTICE_STRING:
        case LATTICE_STRING_CONSTANT:
        case LATTICE_BOOLEAN:
        case LATTICE_TRUE:
        case LATTICE_FALSE:
        case LATTICE_FUNCTION:
        case LATTICE_ARRAY:
        case LATTICE_TYPED_ARRAY:
        case LATTICE_USER_DEFINED_OBJECT:
            return make_not_null_element();
    }

    return make_top_element();
}

/**
 * @brief Joins INTEGER with another lattice element.
 *
 * The left operand is assumed to be exactly LATTICE_INTEGER.
 *
 * @param right Right lattice element.
 * @return Constant pointer to the least upper bound.
 */
static const lattice_element_t *lattice_join_integer(const lattice_element_t *right) {
    switch (right->type) {
        case LATTICE_BOTTOM:
        case LATTICE_INTEGER:
        case LATTICE_INTEGER_RANGE:
        case LATTICE_INTEGER_CONSTANT:
            return make_integer_element();

        case LATTICE_NUMERIC:
        case LATTICE_REAL:
        case LATTICE_REAL_CONSTANT:
            return make_numeric_element();

        case LATTICE_TOP:
        case LATTICE_NULL:
            return make_top_element();

        case LATTICE_NOT_NULL:
        case LATTICE_STRING:
        case LATTICE_STRING_CONSTANT:
        case LATTICE_BOOLEAN:
        case LATTICE_TRUE:
        case LATTICE_FALSE:
        case LATTICE_FUNCTION:
        case LATTICE_ARRAY:
        case LATTICE_TYPED_ARRAY:
        case LATTICE_USER_DEFINED_OBJECT:
            return make_not_null_element();
    }

    return make_top_element();
}

/**
 * @brief Joins INTEGER_RANGE with another lattice element.
 *
 * The left operand is assumed to be exactly LATTICE_INTEGER_RANGE.
 *
 * @param arena Memory arena for allocating a new range element if needed.
 * @param left Left integer range lattice element.
 * @param right Right lattice element.
 * @return Constant pointer to the least upper bound.
 */
static const lattice_element_t *lattice_join_integer_range(arena_t *arena,
        const lattice_element_t *left, const lattice_element_t *right) {
    const integer_range_element_t *left_range = (const integer_range_element_t *)left;

    switch (right->type) {
        case LATTICE_BOTTOM:
            return left;

        case LATTICE_INTEGER_RANGE: {
            const integer_range_element_t *right_range = (const integer_range_element_t *)right;

            if (left_range->min <= right_range->min && left_range->max >= right_range->max) {
                return left;
            }

            if (right_range->min <= left_range->min && right_range->max >= left_range->max) {
                return right;
            }

            return make_integer_range_element(
                arena,
                left_range->min < right_range->min ? left_range->min : right_range->min,
                left_range->max > right_range->max ? left_range->max : right_range->max
            );
        }

        case LATTICE_INTEGER_CONSTANT: {
            const integer_constant_element_t *right_constant =
                (const integer_constant_element_t *)right;

            if (left_range->min <= right_constant->value
                    && left_range->max >= right_constant->value) {
                return left;
            }

            return make_integer_range_element(
                arena,
                left_range->min < right_constant->value ? left_range->min : right_constant->value,
                left_range->max > right_constant->value ? left_range->max : right_constant->value
            );
        }

        case LATTICE_INTEGER:
            return make_integer_element();

        case LATTICE_NUMERIC:
        case LATTICE_REAL:
        case LATTICE_REAL_CONSTANT:
            return make_numeric_element();

        case LATTICE_TOP:
        case LATTICE_NULL:
            return make_top_element();

        case LATTICE_NOT_NULL:
        case LATTICE_STRING:
        case LATTICE_STRING_CONSTANT:
        case LATTICE_BOOLEAN:
        case LATTICE_TRUE:
        case LATTICE_FALSE:
        case LATTICE_FUNCTION:
        case LATTICE_ARRAY:
        case LATTICE_TYPED_ARRAY:
        case LATTICE_USER_DEFINED_OBJECT:
            return make_not_null_element();
    }

    return make_top_element();
}

/**
 * @brief Joins INTEGER_CONSTANT with another lattice element.
 *
 * The left operand is assumed to be exactly LATTICE_INTEGER_CONSTANT.
 *
 * @param arena Memory arena for allocating a new range element if needed.
 * @param left Left integer constant lattice element.
 * @param right Right lattice element.
 * @return Constant pointer to the least upper bound.
 */
static const lattice_element_t *lattice_join_integer_constant(arena_t *arena,
        const lattice_element_t *left, const lattice_element_t *right) {
    const integer_constant_element_t *left_constant =(const integer_constant_element_t *)left;

    switch (right->type) {
        case LATTICE_BOTTOM:
            return left;

        case LATTICE_INTEGER_CONSTANT: {
            const integer_constant_element_t *right_constant =
                (const integer_constant_element_t *)right;

            if (left_constant->value == right_constant->value) {
                return left;
            }

            return make_integer_range_element(
                arena,
                left_constant->value < right_constant->value
                    ? left_constant->value
                    : right_constant->value,
                left_constant->value > right_constant->value
                    ? left_constant->value
                    : right_constant->value
            );
        }

        case LATTICE_INTEGER_RANGE: {
            const integer_range_element_t *right_range =
                (const integer_range_element_t *)right;

            if (right_range->min <= left_constant->value
                    && right_range->max >= left_constant->value) {
                return right;
            }

            return make_integer_range_element(
                arena,
                left_constant->value < right_range->min
                    ? left_constant->value
                    : right_range->min,
                left_constant->value > right_range->max
                    ? left_constant->value
                    : right_range->max
            );
        }

        case LATTICE_INTEGER:
            return make_integer_element();

        case LATTICE_NUMERIC:
        case LATTICE_REAL:
        case LATTICE_REAL_CONSTANT:
            return make_numeric_element();

        case LATTICE_TOP:
        case LATTICE_NULL:
            return make_top_element();

        case LATTICE_NOT_NULL:
        case LATTICE_STRING:
        case LATTICE_STRING_CONSTANT:
        case LATTICE_BOOLEAN:
        case LATTICE_TRUE:
        case LATTICE_FALSE:
        case LATTICE_FUNCTION:
        case LATTICE_ARRAY:
        case LATTICE_TYPED_ARRAY:
        case LATTICE_USER_DEFINED_OBJECT:
            return make_not_null_element();
    }

    return make_top_element();
}

/**
 * @brief Joins REAL with another lattice element.
 *
 * The left operand is assumed to be exactly LATTICE_REAL.
 *
 * @param right Right lattice element.
 * @return Constant pointer to the least upper bound.
 */
static const lattice_element_t *lattice_join_real(const lattice_element_t *right) {
    switch (right->type) {
        case LATTICE_BOTTOM:
        case LATTICE_REAL:
        case LATTICE_REAL_CONSTANT:
            return make_real_element();

        case LATTICE_NUMERIC:
        case LATTICE_INTEGER:
        case LATTICE_INTEGER_RANGE:
        case LATTICE_INTEGER_CONSTANT:
            return make_numeric_element();

        case LATTICE_TOP:
        case LATTICE_NULL:
            return make_top_element();

        case LATTICE_NOT_NULL:
        case LATTICE_STRING:
        case LATTICE_STRING_CONSTANT:
        case LATTICE_BOOLEAN:
        case LATTICE_TRUE:
        case LATTICE_FALSE:
        case LATTICE_FUNCTION:
        case LATTICE_ARRAY:
        case LATTICE_TYPED_ARRAY:
        case LATTICE_USER_DEFINED_OBJECT:
            return make_not_null_element();
    }

    return make_top_element();
}

/**
 * @brief Joins REAL_CONSTANT with another lattice element.
 *
 * The left operand is assumed to be exactly LATTICE_REAL_CONSTANT.
 *
 * @param left Left real constant lattice element.
 * @param right Right lattice element.
 * @return Constant pointer to the least upper bound.
 */
static const lattice_element_t *lattice_join_real_constant(const lattice_element_t *left,
        const lattice_element_t *right) {
    const real_constant_element_t *left_constant = (const real_constant_element_t *)left;

    switch (right->type) {
        case LATTICE_BOTTOM:
            return left;

        case LATTICE_REAL_CONSTANT: {
            const real_constant_element_t *right_constant = (const real_constant_element_t *)right;

            if (left_constant->value == right_constant->value) {
                return left;
            }

            return make_real_element();
        }

        case LATTICE_REAL:
            return make_real_element();

        case LATTICE_NUMERIC:
        case LATTICE_INTEGER:
        case LATTICE_INTEGER_RANGE:
        case LATTICE_INTEGER_CONSTANT:
            return make_numeric_element();

        case LATTICE_TOP:
        case LATTICE_NULL:
            return make_top_element();

        case LATTICE_NOT_NULL:
        case LATTICE_STRING:
        case LATTICE_STRING_CONSTANT:
        case LATTICE_BOOLEAN:
        case LATTICE_TRUE:
        case LATTICE_FALSE:
        case LATTICE_FUNCTION:
        case LATTICE_ARRAY:
        case LATTICE_TYPED_ARRAY:
        case LATTICE_USER_DEFINED_OBJECT:
            return make_not_null_element();
    }

    return make_top_element();
}

/**
 * @brief Joins STRING with another lattice element.
 *
 * The left operand is assumed to be exactly LATTICE_STRING.
 *
 * @param right Right lattice element.
 * @return Constant pointer to the least upper bound.
 */
static const lattice_element_t *lattice_join_string(const lattice_element_t *right) {
    switch (right->type) {
        case LATTICE_BOTTOM:
        case LATTICE_STRING:
        case LATTICE_STRING_CONSTANT:
            return make_string_element();

        case LATTICE_TOP:
        case LATTICE_NULL:
            return make_top_element();

        case LATTICE_NOT_NULL:
        case LATTICE_NUMERIC:
        case LATTICE_INTEGER:
        case LATTICE_INTEGER_RANGE:
        case LATTICE_INTEGER_CONSTANT:
        case LATTICE_REAL:
        case LATTICE_REAL_CONSTANT:
        case LATTICE_BOOLEAN:
        case LATTICE_TRUE:
        case LATTICE_FALSE:
        case LATTICE_FUNCTION:
        case LATTICE_ARRAY:
        case LATTICE_TYPED_ARRAY:
        case LATTICE_USER_DEFINED_OBJECT:
            return make_not_null_element();
    }

    return make_top_element();
}

/**
 * @brief Joins STRING_CONSTANT with another lattice element.
 *
 * The left operand is assumed to be exactly LATTICE_STRING_CONSTANT.
 *
 * @param left Left string constant lattice element.
 * @param right Right lattice element.
 * @return Constant pointer to the least upper bound.
 */
static const lattice_element_t *lattice_join_string_constant(const lattice_element_t *left,
        const lattice_element_t *right) {
    const string_constant_element_t *left_constant = (const string_constant_element_t *)left;

    switch (right->type) {
        case LATTICE_BOTTOM:
            return left;

        case LATTICE_STRING_CONSTANT: {
            const string_constant_element_t *right_constant =
                (const string_constant_element_t *)right;

            if (left_constant->value.length == right_constant->value.length
                    && wmemcmp(
                        left_constant->value.data,
                        right_constant->value.data,
                        left_constant->value.length
                    ) == 0) {
                return left;
            }

            return make_string_element();
        }

        case LATTICE_STRING:
            return make_string_element();

        case LATTICE_TOP:
        case LATTICE_NULL:
            return make_top_element();

        case LATTICE_NOT_NULL:
        case LATTICE_NUMERIC:
        case LATTICE_INTEGER:
        case LATTICE_INTEGER_RANGE:
        case LATTICE_INTEGER_CONSTANT:
        case LATTICE_REAL:
        case LATTICE_REAL_CONSTANT:
        case LATTICE_BOOLEAN:
        case LATTICE_TRUE:
        case LATTICE_FALSE:
        case LATTICE_FUNCTION:
        case LATTICE_ARRAY:
        case LATTICE_TYPED_ARRAY:
        case LATTICE_USER_DEFINED_OBJECT:
            return make_not_null_element();
    }

    return make_top_element();
}

/**
 * @brief Joins BOOLEAN with another lattice element.
 *
 * The left operand is assumed to be exactly LATTICE_BOOLEAN.
 *
 * @param right Right lattice element.
 * @return Constant pointer to the least upper bound.
 */
static const lattice_element_t *lattice_join_boolean(const lattice_element_t *right) {
    switch (right->type) {
        case LATTICE_BOTTOM:
        case LATTICE_BOOLEAN:
        case LATTICE_TRUE:
        case LATTICE_FALSE:
            return make_boolean_element();

        case LATTICE_TOP:
        case LATTICE_NULL:
            return make_top_element();

        case LATTICE_NOT_NULL:
        case LATTICE_NUMERIC:
        case LATTICE_INTEGER:
        case LATTICE_INTEGER_RANGE:
        case LATTICE_INTEGER_CONSTANT:
        case LATTICE_REAL:
        case LATTICE_REAL_CONSTANT:
        case LATTICE_STRING:
        case LATTICE_STRING_CONSTANT:
        case LATTICE_FUNCTION:
        case LATTICE_ARRAY:
        case LATTICE_TYPED_ARRAY:
        case LATTICE_USER_DEFINED_OBJECT:
            return make_not_null_element();
    }

    return make_top_element();
}

/**
 * @brief Joins TRUE with another lattice element.
 *
 * The left operand is assumed to be exactly LATTICE_TRUE.
 *
 * @param right Right lattice element.
 * @return Constant pointer to the least upper bound.
 */
static const lattice_element_t *lattice_join_true(const lattice_element_t *right) {
    switch (right->type) {
        case LATTICE_BOTTOM:
        case LATTICE_TRUE:
            return make_true_element();

        case LATTICE_BOOLEAN:
        case LATTICE_FALSE:
            return make_boolean_element();

        case LATTICE_TOP:
        case LATTICE_NULL:
            return make_top_element();

        case LATTICE_NOT_NULL:
        case LATTICE_NUMERIC:
        case LATTICE_INTEGER:
        case LATTICE_INTEGER_RANGE:
        case LATTICE_INTEGER_CONSTANT:
        case LATTICE_REAL:
        case LATTICE_REAL_CONSTANT:
        case LATTICE_STRING:
        case LATTICE_STRING_CONSTANT:
        case LATTICE_FUNCTION:
        case LATTICE_ARRAY:
        case LATTICE_TYPED_ARRAY:
        case LATTICE_USER_DEFINED_OBJECT:
            return make_not_null_element();
    }

    return make_top_element();
}

/**
 * @brief Joins FALSE with another lattice element.
 *
 * The left operand is assumed to be exactly LATTICE_FALSE.
 *
 * @param right Right lattice element.
 * @return Constant pointer to the least upper bound.
 */
static const lattice_element_t *lattice_join_false(const lattice_element_t *right) {
    switch (right->type) {
        case LATTICE_BOTTOM:
        case LATTICE_FALSE:
            return make_false_element();

        case LATTICE_BOOLEAN:
        case LATTICE_TRUE:
            return make_boolean_element();

        case LATTICE_TOP:
        case LATTICE_NULL:
            return make_top_element();

        case LATTICE_NOT_NULL:
        case LATTICE_NUMERIC:
        case LATTICE_INTEGER:
        case LATTICE_INTEGER_RANGE:
        case LATTICE_INTEGER_CONSTANT:
        case LATTICE_REAL:
        case LATTICE_REAL_CONSTANT:
        case LATTICE_STRING:
        case LATTICE_STRING_CONSTANT:
        case LATTICE_FUNCTION:
        case LATTICE_ARRAY:
        case LATTICE_TYPED_ARRAY:
        case LATTICE_USER_DEFINED_OBJECT:
            return make_not_null_element();
    }

    return make_top_element();
}

/**
 * @brief Joins FUNCTION with another lattice element.
 *
 * The left operand is assumed to be exactly LATTICE_FUNCTION.
 *
 * @param right Right lattice element.
 * @return Constant pointer to the least upper bound.
 */
static const lattice_element_t *lattice_join_function(const lattice_element_t *right) {
    switch (right->type) {
        case LATTICE_BOTTOM:
        case LATTICE_FUNCTION:
            return make_function_element();

        case LATTICE_TOP:
        case LATTICE_NULL:
            return make_top_element();

        case LATTICE_NOT_NULL:
        case LATTICE_NUMERIC:
        case LATTICE_INTEGER:
        case LATTICE_INTEGER_RANGE:
        case LATTICE_INTEGER_CONSTANT:
        case LATTICE_REAL:
        case LATTICE_REAL_CONSTANT:
        case LATTICE_STRING:
        case LATTICE_STRING_CONSTANT:
        case LATTICE_BOOLEAN:
        case LATTICE_TRUE:
        case LATTICE_FALSE:
        case LATTICE_ARRAY:
        case LATTICE_TYPED_ARRAY:
        case LATTICE_USER_DEFINED_OBJECT:
            return make_not_null_element();
    }

    return make_top_element();
}

/**
 * @brief Joins ARRAY with another lattice element.
 *
 * The left operand is assumed to be exactly LATTICE_ARRAY.
 *
 * @param right Right lattice element.
 * @return Constant pointer to the least upper bound.
 */
static const lattice_element_t *lattice_join_array(const lattice_element_t *right) {
    switch (right->type) {
        case LATTICE_BOTTOM:
        case LATTICE_ARRAY:
        case LATTICE_TYPED_ARRAY:
            return make_array_element();

        case LATTICE_TOP:
        case LATTICE_NULL:
            return make_top_element();

        case LATTICE_NOT_NULL:
        case LATTICE_NUMERIC:
        case LATTICE_INTEGER:
        case LATTICE_INTEGER_RANGE:
        case LATTICE_INTEGER_CONSTANT:
        case LATTICE_REAL:
        case LATTICE_REAL_CONSTANT:
        case LATTICE_STRING:
        case LATTICE_STRING_CONSTANT:
        case LATTICE_BOOLEAN:
        case LATTICE_TRUE:
        case LATTICE_FALSE:
        case LATTICE_FUNCTION:
        case LATTICE_USER_DEFINED_OBJECT:
            return make_not_null_element();
    }

    return make_top_element();
}

/**
 * @brief Joins TYPED_ARRAY with another lattice element.
 *
 * The left operand is assumed to be exactly LATTICE_TYPED_ARRAY.
 *
 * @param left Left typed array lattice element.
 * @param right Right lattice element.
 * @return Constant pointer to the least upper bound.
 */
static const lattice_element_t *lattice_join_typed_array(const lattice_element_t *left,
        const lattice_element_t *right) {
    const typed_array_element_t *left_array = (const typed_array_element_t *)left;

    switch (right->type) {
        case LATTICE_BOTTOM:
            return left;

        case LATTICE_TYPED_ARRAY: {
            const typed_array_element_t *right_array = (const typed_array_element_t *)right;

            if (left_array->element_type == right_array->element_type) {
                return left;
            }

            return make_array_element();
        }

        case LATTICE_ARRAY:
            return make_array_element();

        case LATTICE_TOP:
        case LATTICE_NULL:
            return make_top_element();

        case LATTICE_NOT_NULL:
        case LATTICE_NUMERIC:
        case LATTICE_INTEGER:
        case LATTICE_INTEGER_RANGE:
        case LATTICE_INTEGER_CONSTANT:
        case LATTICE_REAL:
        case LATTICE_REAL_CONSTANT:
        case LATTICE_STRING:
        case LATTICE_STRING_CONSTANT:
        case LATTICE_BOOLEAN:
        case LATTICE_TRUE:
        case LATTICE_FALSE:
        case LATTICE_FUNCTION:
        case LATTICE_USER_DEFINED_OBJECT:
            return make_not_null_element();
    }

    return make_top_element();
}

/**
 * @brief Joins USER_DEFINED_OBJECT with another lattice element.
 *
 * The left operand is assumed to be exactly LATTICE_USER_DEFINED_OBJECT.
 *
 * @param right Right lattice element.
 * @return Constant pointer to the least upper bound.
 */
static const lattice_element_t *lattice_join_user_defined_object(const lattice_element_t *right) {
    switch (right->type) {
        case LATTICE_BOTTOM:
        case LATTICE_USER_DEFINED_OBJECT:
            return make_user_defined_object_element();

        case LATTICE_TOP:
        case LATTICE_NULL:
            return make_top_element();

        case LATTICE_NOT_NULL:
        case LATTICE_NUMERIC:
        case LATTICE_INTEGER:
        case LATTICE_INTEGER_RANGE:
        case LATTICE_INTEGER_CONSTANT:
        case LATTICE_REAL:
        case LATTICE_REAL_CONSTANT:
        case LATTICE_STRING:
        case LATTICE_STRING_CONSTANT:
        case LATTICE_BOOLEAN:
        case LATTICE_TRUE:
        case LATTICE_FALSE:
        case LATTICE_FUNCTION:
        case LATTICE_ARRAY:
        case LATTICE_TYPED_ARRAY:
            return make_not_null_element();
    }

    return make_top_element();
}

const lattice_element_t *lattice_join(arena_t *arena,
        const lattice_element_t *left, const lattice_element_t *right) {
    switch (left->type) {
        case LATTICE_TOP:
            return make_top_element();

        case LATTICE_NOT_NULL:
            return lattice_join_not_null(right);

        case LATTICE_NULL:
            return lattice_join_null(right);

        case LATTICE_NUMERIC:
            return lattice_join_numeric(right);

        case LATTICE_INTEGER:
            return lattice_join_integer(right);

        case LATTICE_INTEGER_RANGE:
            return lattice_join_integer_range(arena, left, right);

        case LATTICE_INTEGER_CONSTANT:
            return lattice_join_integer_constant(arena, left, right);

        case LATTICE_REAL:
            return lattice_join_real(right);

        case LATTICE_REAL_CONSTANT:
            return lattice_join_real_constant(left, right);

        case LATTICE_STRING:
            return lattice_join_string(right);

        case LATTICE_STRING_CONSTANT:
            return lattice_join_string_constant(left, right);

        case LATTICE_BOOLEAN:
            return lattice_join_boolean(right);

        case LATTICE_TRUE:
            return lattice_join_true(right);

        case LATTICE_FALSE:
            return lattice_join_false(right);

        case LATTICE_FUNCTION:
            return lattice_join_function(right);

        case LATTICE_ARRAY:
            return lattice_join_array(right);

        case LATTICE_TYPED_ARRAY:
            return lattice_join_typed_array(left, right);

        case LATTICE_USER_DEFINED_OBJECT:
            return lattice_join_user_defined_object(right);

        case LATTICE_BOTTOM:
            return right;
    }

    return make_top_element();
}

/**
 * @brief Meets NOT_NULL with another lattice element.
 *
 * The left operand is assumed to be exactly LATTICE_NOT_NULL.
 *
 * @param right Right lattice element.
 * @return Constant pointer to the greatest lower bound.
 */
static const lattice_element_t *lattice_meet_not_null(const lattice_element_t *right) {
    if (right->type == LATTICE_TOP || right->type == LATTICE_NOT_NULL) {
        return make_not_null_element();
    }
    if (right->type == LATTICE_NULL) {
        return make_bottom_element();
    }
    return right;
}

/**
 * @brief Meets NULL with another lattice element.
 *
 * The left operand is assumed to be exactly LATTICE_NULL.
 *
 * @param right Right lattice element.
 * @return Constant pointer to the greatest lower bound.
 */
static const lattice_element_t *lattice_meet_null(const lattice_element_t *right) {
    if (right->type == LATTICE_TOP || right->type == LATTICE_NULL) {
        return make_null_element();
    }
    return make_bottom_element();
}

/**
 * @brief Meets NUMERIC with another lattice element.
 *
 * The left operand is assumed to be exactly LATTICE_NUMERIC.
 *
 * @param right Right lattice element.
 * @return Constant pointer to the greatest lower bound.
 */
static const lattice_element_t *lattice_meet_numeric(const lattice_element_t *right) {
    switch (right->type) {
        case LATTICE_TOP:
        case LATTICE_NOT_NULL:
        case LATTICE_NUMERIC:
            return make_numeric_element();

        case LATTICE_INTEGER:
        case LATTICE_INTEGER_RANGE:
        case LATTICE_INTEGER_CONSTANT:
        case LATTICE_REAL:
        case LATTICE_REAL_CONSTANT:
            return right;

        case LATTICE_NULL:
        case LATTICE_STRING:
        case LATTICE_STRING_CONSTANT:
        case LATTICE_BOOLEAN:
        case LATTICE_TRUE:
        case LATTICE_FALSE:
        case LATTICE_FUNCTION:
        case LATTICE_ARRAY:
        case LATTICE_TYPED_ARRAY:
        case LATTICE_USER_DEFINED_OBJECT:
        case LATTICE_BOTTOM:
            return make_bottom_element();
    }

    return make_bottom_element();
}

/**
 * @brief Meets INTEGER with another lattice element.
 *
 * The left operand is assumed to be exactly LATTICE_INTEGER.
 *
 * @param right Right lattice element.
 * @return Constant pointer to the greatest lower bound.
 */
static const lattice_element_t *lattice_meet_integer(const lattice_element_t *right) {
    switch (right->type) {
        case LATTICE_TOP:
        case LATTICE_NOT_NULL:
        case LATTICE_NUMERIC:
        case LATTICE_INTEGER:
            return make_integer_element();

        case LATTICE_INTEGER_RANGE:
        case LATTICE_INTEGER_CONSTANT:
            return right;

        case LATTICE_REAL:
        case LATTICE_REAL_CONSTANT:
        case LATTICE_NULL:
        case LATTICE_STRING:
        case LATTICE_STRING_CONSTANT:
        case LATTICE_BOOLEAN:
        case LATTICE_TRUE:
        case LATTICE_FALSE:
        case LATTICE_FUNCTION:
        case LATTICE_ARRAY:
        case LATTICE_TYPED_ARRAY:
        case LATTICE_USER_DEFINED_OBJECT:
        case LATTICE_BOTTOM:
            return make_bottom_element();
    }

    return make_bottom_element();
}

/**
 * @brief Meets INTEGER_RANGE with another lattice element.
 *
 * The left operand is assumed to be exactly LATTICE_INTEGER_RANGE.
 *
 * @param arena Memory arena for allocating a new range element if needed.
 * @param left Left integer range lattice element.
 * @param right Right lattice element.
 * @return Constant pointer to the greatest lower bound.
 */
static const lattice_element_t *lattice_meet_integer_range(arena_t *arena,
        const lattice_element_t *left, const lattice_element_t *right) {
    const integer_range_element_t *left_range = (const integer_range_element_t *)left;

    switch (right->type) {
        case LATTICE_TOP:
        case LATTICE_NOT_NULL:
        case LATTICE_NUMERIC:
        case LATTICE_INTEGER:
            return left;

        case LATTICE_INTEGER_RANGE: {
            const integer_range_element_t *right_range = (const integer_range_element_t *)right;

            int64_t min = left_range->min > right_range->min
                ? left_range->min
                : right_range->min;

            int64_t max = left_range->max < right_range->max
                ? left_range->max
                : right_range->max;

            if (min > max) {
                return make_bottom_element();
            }

            if (min == left_range->min && max == left_range->max) {
                return left;
            }

            if (min == right_range->min && max == right_range->max) {
                return right;
            }

            if (min == max) {
                return make_integer_constant_element(arena, min);
            }

            return make_integer_range_element(arena, min, max);
        }

        case LATTICE_INTEGER_CONSTANT: {
            const integer_constant_element_t *right_constant =
                (const integer_constant_element_t *)right;

            if (left_range->min <= right_constant->value
                    && right_constant->value <= left_range->max) {
                return right;
            }

            return make_bottom_element();
        }

        case LATTICE_REAL:
        case LATTICE_REAL_CONSTANT:
        case LATTICE_NULL:
        case LATTICE_STRING:
        case LATTICE_STRING_CONSTANT:
        case LATTICE_BOOLEAN:
        case LATTICE_TRUE:
        case LATTICE_FALSE:
        case LATTICE_FUNCTION:
        case LATTICE_ARRAY:
        case LATTICE_TYPED_ARRAY:
        case LATTICE_USER_DEFINED_OBJECT:
        case LATTICE_BOTTOM:
            return make_bottom_element();
    }

    return make_bottom_element();
}

/**
 * @brief Meets INTEGER_CONSTANT with another lattice element.
 *
 * The left operand is assumed to be exactly LATTICE_INTEGER_CONSTANT.
 *
 * @param left Left integer constant lattice element.
 * @param right Right lattice element.
 * @return Constant pointer to the greatest lower bound.
 */
static const lattice_element_t *lattice_meet_integer_constant(const lattice_element_t *left,
        const lattice_element_t *right) {
    const integer_constant_element_t *left_constant = (const integer_constant_element_t *)left;

    switch (right->type) {
        case LATTICE_TOP:
        case LATTICE_NOT_NULL:
        case LATTICE_NUMERIC:
        case LATTICE_INTEGER:
            return left;

        case LATTICE_INTEGER_RANGE: {
            const integer_range_element_t *right_range = (const integer_range_element_t *)right;

            if (right_range->min <= left_constant->value
                    && left_constant->value <= right_range->max) {
                return left;
            }

            return make_bottom_element();
        }

        case LATTICE_INTEGER_CONSTANT: {
            const integer_constant_element_t *right_constant =
                (const integer_constant_element_t *)right;

            if (left_constant->value == right_constant->value) {
                return left;
            }

            return make_bottom_element();
        }

        case LATTICE_REAL:
        case LATTICE_REAL_CONSTANT:
        case LATTICE_NULL:
        case LATTICE_STRING:
        case LATTICE_STRING_CONSTANT:
        case LATTICE_BOOLEAN:
        case LATTICE_TRUE:
        case LATTICE_FALSE:
        case LATTICE_FUNCTION:
        case LATTICE_ARRAY:
        case LATTICE_TYPED_ARRAY:
        case LATTICE_USER_DEFINED_OBJECT:
        case LATTICE_BOTTOM:
            return make_bottom_element();
    }

    return make_bottom_element();
}

/**
 * @brief Meets REAL with another lattice element.
 *
 * The left operand is assumed to be exactly LATTICE_REAL.
 *
 * @param right Right lattice element.
 * @return Constant pointer to the greatest lower bound.
 */
static const lattice_element_t *lattice_meet_real(const lattice_element_t *right) {
    switch (right->type) {
        case LATTICE_TOP:
        case LATTICE_NOT_NULL:
        case LATTICE_NUMERIC:
        case LATTICE_REAL:
            return make_real_element();

        case LATTICE_REAL_CONSTANT:
            return right;

        case LATTICE_INTEGER:
        case LATTICE_INTEGER_RANGE:
        case LATTICE_INTEGER_CONSTANT:
        case LATTICE_NULL:
        case LATTICE_STRING:
        case LATTICE_STRING_CONSTANT:
        case LATTICE_BOOLEAN:
        case LATTICE_TRUE:
        case LATTICE_FALSE:
        case LATTICE_FUNCTION:
        case LATTICE_ARRAY:
        case LATTICE_TYPED_ARRAY:
        case LATTICE_USER_DEFINED_OBJECT:
        case LATTICE_BOTTOM:
            return make_bottom_element();
    }

    return make_bottom_element();
}

/**
 * @brief Meets REAL_CONSTANT with another lattice element.
 *
 * The left operand is assumed to be exactly LATTICE_REAL_CONSTANT.
 *
 * @param left Left real constant lattice element.
 * @param right Right lattice element.
 * @return Constant pointer to the greatest lower bound.
 */
static const lattice_element_t *lattice_meet_real_constant(const lattice_element_t *left,
        const lattice_element_t *right) {
    const real_constant_element_t *left_constant = (const real_constant_element_t *)left;

    switch (right->type) {
        case LATTICE_TOP:
        case LATTICE_NOT_NULL:
        case LATTICE_NUMERIC:
        case LATTICE_REAL:
            return left;

        case LATTICE_REAL_CONSTANT: {
            const real_constant_element_t *right_constant =
                (const real_constant_element_t *)right;

            if (left_constant->value == right_constant->value) {
                return left;
            }

            return make_bottom_element();
        }

        case LATTICE_INTEGER:
        case LATTICE_INTEGER_RANGE:
        case LATTICE_INTEGER_CONSTANT:
        case LATTICE_NULL:
        case LATTICE_STRING:
        case LATTICE_STRING_CONSTANT:
        case LATTICE_BOOLEAN:
        case LATTICE_TRUE:
        case LATTICE_FALSE:
        case LATTICE_FUNCTION:
        case LATTICE_ARRAY:
        case LATTICE_TYPED_ARRAY:
        case LATTICE_USER_DEFINED_OBJECT:
        case LATTICE_BOTTOM:
            return make_bottom_element();
    }

    return make_bottom_element();
}

/**
 * @brief Meets STRING with another lattice element.
 *
 * The left operand is assumed to be exactly LATTICE_STRING.
 *
 * @param right Right lattice element.
 * @return Constant pointer to the greatest lower bound.
 */
static const lattice_element_t *lattice_meet_string(const lattice_element_t *right) {
    switch (right->type) {
        case LATTICE_TOP:
        case LATTICE_NOT_NULL:
        case LATTICE_STRING:
            return make_string_element();

        case LATTICE_STRING_CONSTANT:
            return right;

        case LATTICE_NULL:
        case LATTICE_NUMERIC:
        case LATTICE_INTEGER:
        case LATTICE_INTEGER_RANGE:
        case LATTICE_INTEGER_CONSTANT:
        case LATTICE_REAL:
        case LATTICE_REAL_CONSTANT:
        case LATTICE_BOOLEAN:
        case LATTICE_TRUE:
        case LATTICE_FALSE:
        case LATTICE_FUNCTION:
        case LATTICE_ARRAY:
        case LATTICE_TYPED_ARRAY:
        case LATTICE_USER_DEFINED_OBJECT:
        case LATTICE_BOTTOM:
            return make_bottom_element();
    }

    return make_bottom_element();
}

/**
 * @brief Meets STRING_CONSTANT with another lattice element.
 *
 * The left operand is assumed to be exactly LATTICE_STRING_CONSTANT.
 *
 * @param left Left string constant lattice element.
 * @param right Right lattice element.
 * @return Constant pointer to the greatest lower bound.
 */
static const lattice_element_t *lattice_meet_string_constant(const lattice_element_t *left,
        const lattice_element_t *right) {
    const string_constant_element_t *left_constant = (const string_constant_element_t *)left;

    switch (right->type) {
        case LATTICE_TOP:
        case LATTICE_NOT_NULL:
        case LATTICE_STRING:
            return left;

        case LATTICE_STRING_CONSTANT: {
            const string_constant_element_t *right_constant =
                (const string_constant_element_t *)right;

            if (left_constant->value.length == right_constant->value.length
                    && wmemcmp(
                        left_constant->value.data,
                        right_constant->value.data,
                        left_constant->value.length
                    ) == 0) {
                return left;
            }

            return make_bottom_element();
        }

        case LATTICE_NULL:
        case LATTICE_NUMERIC:
        case LATTICE_INTEGER:
        case LATTICE_INTEGER_RANGE:
        case LATTICE_INTEGER_CONSTANT:
        case LATTICE_REAL:
        case LATTICE_REAL_CONSTANT:
        case LATTICE_BOOLEAN:
        case LATTICE_TRUE:
        case LATTICE_FALSE:
        case LATTICE_FUNCTION:
        case LATTICE_ARRAY:
        case LATTICE_TYPED_ARRAY:
        case LATTICE_USER_DEFINED_OBJECT:
        case LATTICE_BOTTOM:
            return make_bottom_element();
    }

    return make_bottom_element();
}

/**
 * @brief Meets BOOLEAN with another lattice element.
 *
 * The left operand is assumed to be exactly LATTICE_BOOLEAN.
 *
 * @param right Right lattice element.
 * @return Constant pointer to the greatest lower bound.
 */
static const lattice_element_t *lattice_meet_boolean(const lattice_element_t *right) {
    switch (right->type) {
        case LATTICE_TOP:
        case LATTICE_NOT_NULL:
        case LATTICE_BOOLEAN:
            return make_boolean_element();

        case LATTICE_TRUE:
        case LATTICE_FALSE:
            return right;

        case LATTICE_NULL:
        case LATTICE_NUMERIC:
        case LATTICE_INTEGER:
        case LATTICE_INTEGER_RANGE:
        case LATTICE_INTEGER_CONSTANT:
        case LATTICE_REAL:
        case LATTICE_REAL_CONSTANT:
        case LATTICE_STRING:
        case LATTICE_STRING_CONSTANT:
        case LATTICE_FUNCTION:
        case LATTICE_ARRAY:
        case LATTICE_TYPED_ARRAY:
        case LATTICE_USER_DEFINED_OBJECT:
        case LATTICE_BOTTOM:
            return make_bottom_element();
    }

    return make_bottom_element();
}

/**
 * @brief Meets TRUE with another lattice element.
 *
 * The left operand is assumed to be exactly LATTICE_TRUE.
 *
 * @param right Right lattice element.
 * @return Constant pointer to the greatest lower bound.
 */
static const lattice_element_t *lattice_meet_true(const lattice_element_t *right) {
    switch (right->type) {
        case LATTICE_TOP:
        case LATTICE_NOT_NULL:
        case LATTICE_BOOLEAN:
        case LATTICE_TRUE:
            return make_true_element();

        case LATTICE_FALSE:
        case LATTICE_NULL:
        case LATTICE_NUMERIC:
        case LATTICE_INTEGER:
        case LATTICE_INTEGER_RANGE:
        case LATTICE_INTEGER_CONSTANT:
        case LATTICE_REAL:
        case LATTICE_REAL_CONSTANT:
        case LATTICE_STRING:
        case LATTICE_STRING_CONSTANT:
        case LATTICE_FUNCTION:
        case LATTICE_ARRAY:
        case LATTICE_TYPED_ARRAY:
        case LATTICE_USER_DEFINED_OBJECT:
        case LATTICE_BOTTOM:
            return make_bottom_element();
    }

    return make_bottom_element();
}

/**
 * @brief Meets FALSE with another lattice element.
 *
 * The left operand is assumed to be exactly LATTICE_FALSE.
 *
 * @param right Right lattice element.
 * @return Constant pointer to the greatest lower bound.
 */
static const lattice_element_t *lattice_meet_false(const lattice_element_t *right) {
    switch (right->type) {
        case LATTICE_TOP:
        case LATTICE_NOT_NULL:
        case LATTICE_BOOLEAN:
        case LATTICE_FALSE:
            return make_false_element();

        case LATTICE_TRUE:
        case LATTICE_NULL:
        case LATTICE_NUMERIC:
        case LATTICE_INTEGER:
        case LATTICE_INTEGER_RANGE:
        case LATTICE_INTEGER_CONSTANT:
        case LATTICE_REAL:
        case LATTICE_REAL_CONSTANT:
        case LATTICE_STRING:
        case LATTICE_STRING_CONSTANT:
        case LATTICE_FUNCTION:
        case LATTICE_ARRAY:
        case LATTICE_TYPED_ARRAY:
        case LATTICE_USER_DEFINED_OBJECT:
        case LATTICE_BOTTOM:
            return make_bottom_element();
    }

    return make_bottom_element();
}

/**
 * @brief Meets FUNCTION with another lattice element.
 *
 * The left operand is assumed to be exactly LATTICE_FUNCTION.
 *
 * @param right Right lattice element.
 * @return Constant pointer to the greatest lower bound.
 */
static const lattice_element_t *lattice_meet_function(const lattice_element_t *right) {
    switch (right->type) {
        case LATTICE_TOP:
        case LATTICE_NOT_NULL:
        case LATTICE_FUNCTION:
            return make_function_element();

        case LATTICE_NULL:
        case LATTICE_NUMERIC:
        case LATTICE_INTEGER:
        case LATTICE_INTEGER_RANGE:
        case LATTICE_INTEGER_CONSTANT:
        case LATTICE_REAL:
        case LATTICE_REAL_CONSTANT:
        case LATTICE_STRING:
        case LATTICE_STRING_CONSTANT:
        case LATTICE_BOOLEAN:
        case LATTICE_TRUE:
        case LATTICE_FALSE:
        case LATTICE_ARRAY:
        case LATTICE_TYPED_ARRAY:
        case LATTICE_USER_DEFINED_OBJECT:
        case LATTICE_BOTTOM:
            return make_bottom_element();
    }

    return make_bottom_element();
}

/**
 * @brief Meets ARRAY with another lattice element.
 *
 * The left operand is assumed to be exactly LATTICE_ARRAY.
 *
 * @param right Right lattice element.
 * @return Constant pointer to the greatest lower bound.
 */
static const lattice_element_t *lattice_meet_array(const lattice_element_t *right) {
    switch (right->type) {
        case LATTICE_TOP:
        case LATTICE_NOT_NULL:
        case LATTICE_ARRAY:
            return make_array_element();

        case LATTICE_TYPED_ARRAY:
            return right;

        case LATTICE_NULL:
        case LATTICE_NUMERIC:
        case LATTICE_INTEGER:
        case LATTICE_INTEGER_RANGE:
        case LATTICE_INTEGER_CONSTANT:
        case LATTICE_REAL:
        case LATTICE_REAL_CONSTANT:
        case LATTICE_STRING:
        case LATTICE_STRING_CONSTANT:
        case LATTICE_BOOLEAN:
        case LATTICE_TRUE:
        case LATTICE_FALSE:
        case LATTICE_FUNCTION:
        case LATTICE_USER_DEFINED_OBJECT:
        case LATTICE_BOTTOM:
            return make_bottom_element();
    }

    return make_bottom_element();
}

/**
 * @brief Meets TYPED_ARRAY with another lattice element.
 *
 * The left operand is assumed to be exactly LATTICE_TYPED_ARRAY.
 *
 * @param left Left typed array lattice element.
 * @param right Right lattice element.
 * @return Constant pointer to the greatest lower bound.
 */
static const lattice_element_t *lattice_meet_typed_array(const lattice_element_t *left,
        const lattice_element_t *right) {
    const typed_array_element_t *left_array = (const typed_array_element_t *)left;

    switch (right->type) {
        case LATTICE_TOP:
        case LATTICE_NOT_NULL:
        case LATTICE_ARRAY:
            return left;

        case LATTICE_TYPED_ARRAY: {
            const typed_array_element_t *right_array = (const typed_array_element_t *)right;

            if (left_array->element_type == right_array->element_type) {
                return left;
            }

            return make_bottom_element();
        }

        case LATTICE_NULL:
        case LATTICE_NUMERIC:
        case LATTICE_INTEGER:
        case LATTICE_INTEGER_RANGE:
        case LATTICE_INTEGER_CONSTANT:
        case LATTICE_REAL:
        case LATTICE_REAL_CONSTANT:
        case LATTICE_STRING:
        case LATTICE_STRING_CONSTANT:
        case LATTICE_BOOLEAN:
        case LATTICE_TRUE:
        case LATTICE_FALSE:
        case LATTICE_FUNCTION:
        case LATTICE_USER_DEFINED_OBJECT:
        case LATTICE_BOTTOM:
            return make_bottom_element();
    }

    return make_bottom_element();
}

/**
 * @brief Meets USER_DEFINED_OBJECT with another lattice element.
 *
 * The left operand is assumed to be exactly LATTICE_USER_DEFINED_OBJECT.
 *
 * @param right Right lattice element.
 * @return Constant pointer to the greatest lower bound.
 */
static const lattice_element_t *lattice_meet_user_defined_object(const lattice_element_t *right) {
    switch (right->type) {
        case LATTICE_TOP:
        case LATTICE_NOT_NULL:
        case LATTICE_USER_DEFINED_OBJECT:
            return make_user_defined_object_element();

        case LATTICE_NULL:
        case LATTICE_NUMERIC:
        case LATTICE_INTEGER:
        case LATTICE_INTEGER_RANGE:
        case LATTICE_INTEGER_CONSTANT:
        case LATTICE_REAL:
        case LATTICE_REAL_CONSTANT:
        case LATTICE_STRING:
        case LATTICE_STRING_CONSTANT:
        case LATTICE_BOOLEAN:
        case LATTICE_TRUE:
        case LATTICE_FALSE:
        case LATTICE_FUNCTION:
        case LATTICE_ARRAY:
        case LATTICE_TYPED_ARRAY:
        case LATTICE_BOTTOM:
            return make_bottom_element();
    }

    return make_bottom_element();
}

const lattice_element_t *lattice_meet(arena_t *arena,
        const lattice_element_t *left, const lattice_element_t *right) {
    switch (left->type) {
        case LATTICE_TOP:
            return right;

        case LATTICE_NOT_NULL:
            return lattice_meet_not_null(right);

        case LATTICE_NULL:
            return lattice_meet_null(right);

        case LATTICE_NUMERIC:
            return lattice_meet_numeric(right);

        case LATTICE_INTEGER:
            return lattice_meet_integer(right);

        case LATTICE_INTEGER_RANGE:
            return lattice_meet_integer_range(arena, left, right);

        case LATTICE_INTEGER_CONSTANT:
            return lattice_meet_integer_constant(left, right);

        case LATTICE_REAL:
            return lattice_meet_real(right);

        case LATTICE_REAL_CONSTANT:
            return lattice_meet_real_constant(left, right);

        case LATTICE_STRING:
            return lattice_meet_string(right);

        case LATTICE_STRING_CONSTANT:
            return lattice_meet_string_constant(left, right);

        case LATTICE_BOOLEAN:
            return lattice_meet_boolean(right);

        case LATTICE_TRUE:
            return lattice_meet_true(right);

        case LATTICE_FALSE:
            return lattice_meet_false(right);

        case LATTICE_FUNCTION:
            return lattice_meet_function(right);

        case LATTICE_ARRAY:
            return lattice_meet_array(right);

        case LATTICE_TYPED_ARRAY:
            return lattice_meet_typed_array(left, right);

        case LATTICE_USER_DEFINED_OBJECT:
            return lattice_meet_user_defined_object(right);

        case LATTICE_BOTTOM:
            return make_bottom_element();
    }

    return make_bottom_element();
}

/**
 * @brief Converts a lattice type to a short human-readable string.
 *
 * @param type Lattice type.
 * @return String representation of the lattice type.
 */
static const wchar_t* lattice_type_to_string(lattice_type_t type) {
    switch (type) {
        case LATTICE_TOP:
            return L"⊤";

        case LATTICE_BOTTOM:
            return L"⊥";

        case LATTICE_NOT_NULL:
            return L"¬null";

        case LATTICE_NULL:
            return L"null";

        case LATTICE_NUMERIC:
            return L"numeric";

        case LATTICE_INTEGER:
            return L"integer";

        case LATTICE_INTEGER_RANGE:
            return L"integer range";

        case LATTICE_INTEGER_CONSTANT:
            return L"const integer";

        case LATTICE_REAL:
            return L"real";

        case LATTICE_REAL_CONSTANT:
            return L"const real";

        case LATTICE_STRING:
            return L"string";

        case LATTICE_STRING_CONSTANT:
            return L"const string";

        case LATTICE_BOOLEAN:
            return L"boolean";

        case LATTICE_TRUE:
            return L"true";

        case LATTICE_FALSE:
            return L"false";

        case LATTICE_FUNCTION:
            return L"function";

        case LATTICE_ARRAY:
            return L"array";

        case LATTICE_TYPED_ARRAY:
            return L"array<>";

        case LATTICE_USER_DEFINED_OBJECT:
            return L"object";
    }

    return L"?";
}

string_value_t lattice_to_string(const lattice_element_t *element) {
    switch (element->type) {
        case LATTICE_TOP:
            return STATIC_STRING(L"⊤");

        case LATTICE_BOTTOM:
            return STATIC_STRING(L"⊥");

        case LATTICE_NOT_NULL:
            return STATIC_STRING(L"¬null");

        case LATTICE_NULL:
            return STATIC_STRING(L"null");

        case LATTICE_NUMERIC:
            return STATIC_STRING(L"numeric");

        case LATTICE_INTEGER:
            return STATIC_STRING(L"integer");

        case LATTICE_INTEGER_RANGE: {
            const integer_range_element_t *range = (const integer_range_element_t *)element;
            return format_string(L"integer[%ld..%ld]", range->min, range->max);
        }

        case LATTICE_INTEGER_CONSTANT: {
            const integer_constant_element_t *constant =
                (const integer_constant_element_t *)element;
            return format_string(L"%ld", constant->value);
        }

        case LATTICE_REAL:
            return STATIC_STRING(L"real");

        case LATTICE_REAL_CONSTANT: {
            const real_constant_element_t *constant = (const real_constant_element_t *)element;
            return format_string(L"%f", constant->value);
        }

        case LATTICE_STRING:
            return STATIC_STRING(L"string");

        case LATTICE_STRING_CONSTANT: {
            const string_constant_element_t *constant =
                (const string_constant_element_t *)element;
            return string_to_string_notation(L"", VIEW_TO_VALUE(constant->value));
        }

        case LATTICE_BOOLEAN:
            return STATIC_STRING(L"boolean");

        case LATTICE_TRUE:
            return STATIC_STRING(L"true");

        case LATTICE_FALSE:
            return STATIC_STRING(L"false");

        case LATTICE_FUNCTION:
            return STATIC_STRING(L"function");

        case LATTICE_ARRAY:
            return STATIC_STRING(L"array");

        case LATTICE_TYPED_ARRAY: {
            const typed_array_element_t *array = (const typed_array_element_t *)element;
            return format_string(L"array<%s>", lattice_type_to_string(array->element_type));
        }

        case LATTICE_USER_DEFINED_OBJECT:
            return STATIC_STRING(L"object");
    }

    return STATIC_STRING(L"?");
}
