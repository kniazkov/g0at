/**
 * @file addition.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of the addition binary operation expression node.
 */

#include "binary_operation.h"
#include "common_methods.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "analysis/lattice.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/source_builder.h"

/** @brief An addition operation expression node. */
typedef struct {
    /** @brief Base binary operation structure from which addition_t inherits. */
    binary_operation_t base;
} addition_t;

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
    int64_t result;
    if (!add_int64_checked(left_int->value, right_int->value, &result)) {
        return make_integer_element();
    }
    return make_integer_constant_element(arena, result);
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
    switch (right->type) {
        case LATTICE_TOP:
        case LATTICE_NOT_NULL:
        case LATTICE_STRING:
            return make_string_element();

        case LATTICE_STRING_CONSTANT:
            if (left->type == LATTICE_STRING_CONSTANT) {
                return add_string_constants(arena, left, right);
            }
            return make_string_element();

        default:
            /* Incompatible operand: no normal result (BOTTOM). */
            return make_bottom_element();
    }
}

/**
 * @brief Calculates the abstract result of array concatenation.
 *
 * If the right operand is top or not-null, the result is top: the operation may produce an array,
 * dispatch to something broader, or fail at runtime, and the abstract interpreter does not have
 * enough facts to narrow that mess.
 */
static const lattice_element_t *calculate_array_addition(
        const lattice_element_t *left, const lattice_element_t *right) {
    switch (right->type) {
        case LATTICE_TOP:
        case LATTICE_NOT_NULL:
            return make_top_element();

        case LATTICE_ARRAY:
            return make_array_element();

        case LATTICE_TYPED_ARRAY:
            if (left->type == LATTICE_TYPED_ARRAY) {
                const typed_array_element_t *left_array = (const typed_array_element_t*)left;
                const typed_array_element_t *right_array = (const typed_array_element_t*)right;
                if (left_array->element_type == right_array->element_type) {
                    return left;
                }
            }
            return make_array_element();

        default:
            /* Incompatible operand: no normal result (BOTTOM). */
            return make_bottom_element();
    }
}

/** @brief Implements @ref node_vtbl_t::calculate. */
static const lattice_element_t *calculate(node_t *node,
        abstract_state_t *state, arena_t *arena) {
    const binary_operation_t *expr = (const binary_operation_t *)node;
    const lattice_element_t *left = calculate_expression(expr->left_operand, state, arena);
    const lattice_element_t *right = calculate_expression(expr->right_operand, state, arena);

    switch (left->type) {
        case LATTICE_BOTTOM:

            return make_bottom_element();

        case LATTICE_TOP:
            /* TOP provides no usable operand domain. */
            return make_top_element();

        case LATTICE_NOT_NULL:
            /* Non-null does not determine the result type of +. */
            return is_addable_lattice_element(right)
                ? make_top_element()
                : make_bottom_element();

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
            return calculate_array_addition(left, right);

        case LATTICE_USER_DEFINED_OBJECT:
            /* User-defined + has an unknown result type. */
            return make_top_element();
    }

    return make_bottom_element();
}

/** @brief Implements @ref node_vtbl_t::generate_goat_code. */
static string_value_t generate_goat_code(const node_t *node) {
    const addition_t *expr = (const addition_t *)node;
    string_value_t left = generate_goat_code_from_expression(expr->base.left_operand);
    string_value_t right = generate_goat_code_from_expression(expr->base.right_operand);
    string_value_t result = format_string(L"%s + %s", left.data, right.data);
    FREE_STRING(left);
    FREE_STRING(right);
    return result;
}

/** @brief Implements @ref node_vtbl_t::generate_indented_goat_code. */
static void generate_indented_goat_code(const node_t *node, source_builder_t *builder,
        size_t indent) {
    const addition_t *expr = (const addition_t *)node;
    generate_indented_goat_code_from_expression(expr->base.left_operand, builder, indent);
    append_static_source(builder, L" + ");
    generate_indented_goat_code_from_expression(expr->base.right_operand, builder, indent);
}

/** @brief Generates bytecode for an addition operation node. */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code,
        data_builder_t *data) {
    const addition_t *expr = (const addition_t *)node;
    instr_index_t first = generate_bytecode_from_expression(
        expr->base.left_operand, code, data);
    generate_bytecode_from_expression(expr->base.right_operand, code, data);
    add_instruction(code, (instruction_t){ .opcode = ADD });
    return first;
}

/** @brief Virtual table for addition operations. */
static node_vtbl_t addition_vtbl = {
    .type = NODE_ADDITION,
    .type_name = L"addition",
    .get_data = no_data,
    .get_property_count = no_properties,
    .get_property = no_property,
    .get_child_count = binop_get_child_count,
    .get_child = binop_get_child,
    .get_child_tag = binop_get_tag,
    .insert_child_before = no_child_insertion,
    .replace_child = no_child_replacement,
    .get_related_count = no_related_nodes,
    .get_related = no_related_node,
    .get_relation_type = no_relation_type,
    .calculate = calculate,
    .execute = execute_nothing,
    .generate_goat_code = generate_goat_code,
    .generate_indented_goat_code = generate_indented_goat_code,
    .generate_bytecode = generate_bytecode
};

expression_t *create_addition_node(arena_t *arena, expression_t *left_operand,
        expression_t *right_operand) {
    addition_t *expr = (addition_t *)alloc_zeroed_from_arena(arena, sizeof(addition_t));
    expr->base.base.base.vtbl = &addition_vtbl;
    expr->base.left_operand = left_operand;
    expr->base.right_operand = right_operand;
    return &expr->base.base;
}
