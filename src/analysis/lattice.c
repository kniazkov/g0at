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
    const integer_range_lattice_element_t *left_range =
        (const integer_range_lattice_element_t *)left;

    switch (right->type) {
        case LATTICE_BOTTOM:
            return left;

        case LATTICE_INTEGER_RANGE: {
            const integer_range_lattice_element_t *right_range =
                (const integer_range_lattice_element_t *)right;

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
            const integer_constant_lattice_element_t *right_constant =
                (const integer_constant_lattice_element_t *)right;

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
        const lattice_element_t *left, const lattice_element_t *right
) {
    const integer_constant_lattice_element_t *left_constant =
        (const integer_constant_lattice_element_t *)left;

    switch (right->type) {
        case LATTICE_BOTTOM:
            return left;

        case LATTICE_INTEGER_CONSTANT: {
            const integer_constant_lattice_element_t *right_constant =
                (const integer_constant_lattice_element_t *)right;

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
            const integer_range_lattice_element_t *right_range =
                (const integer_range_lattice_element_t *)right;

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
    const real_constant_lattice_element_t *left_constant =
        (const real_constant_lattice_element_t *)left;

    switch (right->type) {
        case LATTICE_BOTTOM:
            return left;

        case LATTICE_REAL_CONSTANT: {
            const real_constant_lattice_element_t *right_constant =
                (const real_constant_lattice_element_t *)right;

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
    const string_constant_lattice_element_t *left_constant =
        (const string_constant_lattice_element_t *)left;

    switch (right->type) {
        case LATTICE_BOTTOM:
            return left;

        case LATTICE_STRING_CONSTANT: {
            const string_constant_lattice_element_t *right_constant =
                (const string_constant_lattice_element_t *)right;

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
    const typed_array_lattice_element_t *left_array =
        (const typed_array_lattice_element_t *)left;

    switch (right->type) {
        case LATTICE_BOTTOM:
            return left;

        case LATTICE_TYPED_ARRAY: {
            const typed_array_lattice_element_t *right_array =
                (const typed_array_lattice_element_t *)right;

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

const lattice_element_t *lattice_meet(arena_t *arena,
        const lattice_element_t *left, const lattice_element_t *right) {
    return make_bottom_element();
}
