/** @file addition.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Abstract normal results of left-dispatched addition.
 */
#include "addition.h"
#include "lib/integer_math.h"
#include "lib/string_ext.h"
#include "lib/allocate.h"

/**
 * @brief Safely adds two int64_t values.
 * `out`: Output sum if there is no overflow.
 * @return `true` if addition succeeded without overflow, `false` otherwise.
 */
static bool add_int64_checked(int64_t left, int64_t right, int64_t *out) {
    if ((right > 0 && left > INT64_MAX - right) ||
            (right < 0 && left < INT64_MIN - right)) {
        return false;
    }
    *out = left + right;
    return true;
}

/** @brief Calculates integer constant plus integer constant. */
static const lattice_element_t *add_integer_constants(arena_t *arena,
        const lattice_element_t *left, const lattice_element_t *right) {
    const integer_constant_element_t *left_int = (const integer_constant_element_t*)left;
    const integer_constant_element_t *right_int = (const integer_constant_element_t*)right;
    return make_integer_constant_element(arena,
        add_int64_wrapping(left_int->value, right_int->value));
}

/** @brief Calculates integer range plus integer constant. */
static const lattice_element_t *add_integer_range_and_constant(arena_t *arena,
        const lattice_element_t *range, const lattice_element_t *constant) {
    const integer_range_element_t *int_range = (const integer_range_element_t*)range;
    const integer_constant_element_t *int_constant = (const integer_constant_element_t*)constant;
    int64_t min;
    int64_t max;
    if (!add_int64_checked(int_range->min, int_constant->value, &min) ||
            !add_int64_checked(int_range->max, int_constant->value, &max)) {
        return make_integer_element();
    }
    return make_integer_range_element(arena, min, max);
}

/** @brief Calculates integer range plus integer range. */
static const lattice_element_t *add_integer_ranges(arena_t *arena,
        const lattice_element_t *left, const lattice_element_t *right) {
    const integer_range_element_t *left_range = (const integer_range_element_t*)left;
    const integer_range_element_t *right_range = (const integer_range_element_t*)right;
    int64_t min;
    int64_t max;
    if (!add_int64_checked(left_range->min, right_range->min, &min) ||
            !add_int64_checked(left_range->max, right_range->max, &max)) {
        return make_integer_element();
    }
    return make_integer_range_element(arena, min, max);
}

/** @brief Calculates string constant plus string constant. */
static const lattice_element_t *add_string_constants(arena_t *arena,
        const lattice_element_t *left, const lattice_element_t *right) {
    const string_constant_element_t *left_string = (const string_constant_element_t*)left;
    const string_constant_element_t *right_string = (const string_constant_element_t*)right;

    if (left_string->value.length == 0) {
        return right;
    }
    if (right_string->value.length == 0) {
        return left;
    }
    size_t length = left_string->value.length + right_string->value.length;
    wchar_t *data = (wchar_t*)alloc_from_arena(arena, sizeof(wchar_t) * length);
    wmemcpy(data, left_string->value.data, left_string->value.length);
    wmemcpy(data + left_string->value.length,
            right_string->value.data,
            right_string->value.length);
    return make_string_constant_element(
        arena,
        (string_view_t){
            .data = data,
            .length = length
        }
    );
}

/** @brief Calculates the abstract result of integer-like addition. */
static const lattice_element_t *calculate_integer_addition(arena_t *arena,
        const lattice_element_t *left, const lattice_element_t *right) {
    switch (right->type) {
        case LATTICE_TOP:
        case LATTICE_NOT_NULL:
        case LATTICE_NUMERIC:
            return make_numeric_element();

        case LATTICE_INTEGER:
            return make_integer_element();

        case LATTICE_INTEGER_CONSTANT:
            switch (left->type) {
                case LATTICE_INTEGER_CONSTANT:
                    return add_integer_constants(arena, left, right);
                case LATTICE_INTEGER_RANGE:
                    return add_integer_range_and_constant(arena, left, right);
                case LATTICE_INTEGER:
                    return make_integer_element();
                default:
                    return make_bottom_element();
            }

        case LATTICE_INTEGER_RANGE:
            switch (left->type) {
                case LATTICE_INTEGER_CONSTANT:
                    return add_integer_range_and_constant(arena, right, left);
                case LATTICE_INTEGER_RANGE:
                    return add_integer_ranges(arena, left, right);
                case LATTICE_INTEGER:
                    return make_integer_element();
                default:
                    return make_bottom_element();
            }

        case LATTICE_REAL:
            return make_real_element();

        case LATTICE_REAL_CONSTANT:
            if (left->type == LATTICE_INTEGER_CONSTANT) {
                const integer_constant_element_t *int_constant =
                    (const integer_constant_element_t*)left;
                const real_constant_element_t *real_constant =
                    (const real_constant_element_t*)right;

                return make_real_constant_element(
                    arena,
                    (double)int_constant->value + real_constant->value
                );
            }
            return make_real_element();

        default:
            /* Incompatible operand: no normal result (BOTTOM). */
            return make_bottom_element();
    }
}

/** @brief Calculates the abstract result of real-like addition. */
static const lattice_element_t *calculate_real_addition(arena_t *arena,
        const lattice_element_t *left, const lattice_element_t *right) {
    switch (right->type) {
        case LATTICE_TOP:
        case LATTICE_NOT_NULL:
        case LATTICE_NUMERIC:
        case LATTICE_INTEGER:
        case LATTICE_INTEGER_RANGE:
        case LATTICE_REAL:
            return make_real_element();

        case LATTICE_INTEGER_CONSTANT:
            if (left->type == LATTICE_REAL_CONSTANT) {
                const real_constant_element_t *real_constant =
                    (const real_constant_element_t*)left;
                const integer_constant_element_t *int_constant =
                    (const integer_constant_element_t*)right;

                return make_real_constant_element(
                    arena,
                    real_constant->value + (double)int_constant->value
                );
            }
            return make_real_element();

        case LATTICE_REAL_CONSTANT:
            if (left->type == LATTICE_REAL_CONSTANT) {
                const real_constant_element_t *left_value =
                    (const real_constant_element_t*)left;
                const real_constant_element_t *right_value =
                    (const real_constant_element_t*)right;

                return make_real_constant_element(
                    arena,
                    left_value->value + right_value->value
                );
            }
            return make_real_element();

        default:
            /* Incompatible operand: no normal result (BOTTOM). */
            return make_bottom_element();
    }
}

/** @brief Calculates the abstract result of broad numeric addition. */
static const lattice_element_t *calculate_numeric_addition(const lattice_element_t *right) {
    switch (right->type) {
        case LATTICE_TOP:
        case LATTICE_NOT_NULL:
        case LATTICE_NUMERIC:
        case LATTICE_INTEGER:
        case LATTICE_INTEGER_RANGE:
        case LATTICE_INTEGER_CONSTANT:
        case LATTICE_REAL:
        case LATTICE_REAL_CONSTANT:
            return make_numeric_element();

        default:
            /* Incompatible operand: no normal result (BOTTOM). */
            return make_bottom_element();
    }
}

/** @brief Calculates the abstract result of string concatenation. */
static const lattice_element_t *calculate_string_addition(arena_t *arena,
        const lattice_element_t *left, const lattice_element_t *right) {
    if (left->type != LATTICE_STRING_CONSTANT) return make_string_element();
    if (right->type == LATTICE_STRING_CONSTANT) return add_string_constants(arena, left, right);
    string_value_t text;
    switch (right->type) {
        case LATTICE_NULL: text = format_string(L"null"); break;
        case LATTICE_TRUE: text = format_string(L"true"); break;
        case LATTICE_FALSE: text = format_string(L"false"); break;
        case LATTICE_INTEGER_CONSTANT:
            text = format_string(L"%ld", ((const integer_constant_element_t*)right)->value);
            break;
        case LATTICE_REAL_CONSTANT:
            text = format_string(L"%f", ((const real_constant_element_t*)right)->value);
            break;
        default: return make_string_element();
    }
    /* Copy even for an empty left operand: text is temporary storage. */
    const string_constant_element_t *str = (const string_constant_element_t*)left;
    size_t length = str->value.length + text.length;
    wchar_t *data = alloc_from_arena(arena, (length + 1) * sizeof(wchar_t));
    wmemcpy(data, str->value.data, str->value.length);
    wmemcpy(data + str->value.length, text.data, text.length);
    data[length] = 0;
    FREE_STRING(text);
    return make_string_constant_element(arena, (string_view_t){data, length});
}

const lattice_element_t *lattice_add(arena_t *arena,
        const lattice_element_t *left, const lattice_element_t *right) {
    if (left->type == LATTICE_BOTTOM || right->type == LATTICE_BOTTOM)
        return make_bottom_element();
    switch (left->type) {
        case LATTICE_BOTTOM:
            return make_bottom_element();

        case LATTICE_TOP:
        case LATTICE_NOT_NULL:
            /* A string on the left can concatenate any existing value. */
            return make_top_element();

        case LATTICE_NULL:
        case LATTICE_BOOLEAN:
        case LATTICE_TRUE:
        case LATTICE_FALSE:
        case LATTICE_FUNCTION:
            /* Incompatible operand: no normal result (BOTTOM). */
            return make_bottom_element();

        case LATTICE_NUMERIC:
            return calculate_numeric_addition(right);

        case LATTICE_INTEGER:
        case LATTICE_INTEGER_RANGE:
        case LATTICE_INTEGER_CONSTANT:
            return calculate_integer_addition(arena, left, right);

        case LATTICE_REAL:
        case LATTICE_REAL_CONSTANT:
            return calculate_real_addition(arena, left, right);

        case LATTICE_STRING:
        case LATTICE_STRING_CONSTANT:
            return calculate_string_addition(arena, left, right);

        case LATTICE_ARRAY:
        case LATTICE_TYPED_ARRAY:
            /* Array models are not implemented yet. */
            return make_top_element();

        case LATTICE_USER_DEFINED_OBJECT:
            return make_bottom_element();
    }

    return make_bottom_element();
}

