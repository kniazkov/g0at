/** @file c_arithmetic.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Wrapping integers and sequenced binary64 expression evaluation.
 */
#include "c_arithmetic.h"

#include "c_lowering.h"
#include "graph/node.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"

static bool numeric(c_value_type_t type) {
    return type == C_VALUE_INT64 || type == C_VALUE_DOUBLE;
}

/** @brief Copies prelude statements before evaluating and rounding the operand once. */
string_value_t c_capture_operand(source_builder_t *prelude,
                                 c_generation_context_t *context,
                                 const c_generated_expression_t *operand,
                                 c_value_type_t type) {
    for (size_t i = 0; operand->prelude && i < operand->prelude->count; i++) {
        const line_of_code_t *line = &operand->prelude->lines[i];
        add_source(prelude, line->indent, L"%s", line->text.data);
    }
    string_value_t name = format_string(L"goat_t%zu", context->temporary_count++);
    add_source(prelude,
               0,
               L"%s%s %s = (%s)(%s);",
               type == C_VALUE_DOUBLE ? L"volatile " : L"",
               c_type_name(type),
               name.data,
               c_type_name(type),
               operand->value.data);
    return name;
}

c_generated_expression_t
c_binary_arithmetic(const node_t *node, c_generation_context_t *context, wchar_t operation) {
    if (operation != L'+' && operation != L'-' && operation != L'*') {
        fail_c_generation(context, node, C_GENERATION_UNSUPPORTED);
        return (c_generated_expression_t){0};
    }
    c_generated_expression_t left = generate_c_code_from_node(get_node_child(node, 0), context);
    if (!left.success)
        return left;
    c_generated_expression_t right = generate_c_code_from_node(get_node_child(node, 1), context);
    if (!right.success) {
        destroy_c_expression(&left);
        return right;
    }
    c_value_type_t type =
        left.type == C_VALUE_INT64 && right.type == C_VALUE_INT64 ? C_VALUE_INT64 : C_VALUE_DOUBLE;
    if (!numeric(left.type) || !numeric(right.type)
        || c_generation_expression_type(context, node) != type) {
        destroy_c_expression(&left);
        destroy_c_expression(&right);
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    source_builder_t *prelude = create_source_builder();
    string_value_t a = c_capture_operand(prelude, context, &left, type);
    string_value_t b = c_capture_operand(prelude, context, &right, type);
    string_value_t value = format_string(L"goat_t%zu", context->temporary_count++);
    if (type == C_VALUE_INT64) {
        context->helper_flags |= C_HELPER_INTEGER;
        add_source(prelude,
                   0,
                   L"int64_t %s = goat_i64_bits((uint64_t)%s %c (uint64_t)%s);",
                   value.data,
                   a.data,
                   operation,
                   b.data);
    } else
        add_source(prelude,
                   0,
                   L"volatile double %s = %s %c %s;",
                   value.data,
                   a.data,
                   operation,
                   b.data);
    FREE_STRING(a);
    FREE_STRING(b);
    destroy_c_expression(&left);
    destroy_c_expression(&right);
    return (c_generated_expression_t){.success = true,
                                      .type = type,
                                      .value = value,
                                      .prelude = prelude};
}

c_generated_expression_t
c_unary_arithmetic(const node_t *node, c_generation_context_t *context, bool negative) {
    c_generated_expression_t operand = generate_c_code_from_node(get_node_child(node, 0), context);
    if (!operand.success)
        return operand;
    if (!numeric(operand.type) || c_generation_expression_type(context, node) != operand.type) {
        destroy_c_expression(&operand);
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    if (!negative)
        return operand;
    source_builder_t *prelude = create_source_builder();
    string_value_t argument = c_capture_operand(prelude, context, &operand, operand.type);
    string_value_t value = format_string(L"goat_t%zu", context->temporary_count++);
    if (operand.type == C_VALUE_INT64) {
        context->helper_flags |= C_HELPER_INTEGER;
        add_source(prelude,
                   0,
                   L"int64_t %s = goat_i64_bits(UINT64_C(0) - (uint64_t)%s);",
                   value.data,
                   argument.data);
    } else
        add_source(prelude, 0, L"volatile double %s = -%s;", value.data, argument.data);
    c_value_type_t type = operand.type;
    abstract_truth_t truth = operand.literal_truth;
    FREE_STRING(argument);
    destroy_c_expression(&operand);
    return (c_generated_expression_t){.success = true,
                                      .type = type,
                                      .literal_truth = truth,
                                      .value = value,
                                      .prelude = prelude};
}

c_generated_expression_t c_parenthesized(const node_t *node, c_generation_context_t *context) {
    c_generated_expression_t operand = generate_c_code_from_node(get_node_child(node, 0), context);
    if (operand.success && c_generation_expression_type(context, node) != operand.type) {
        destroy_c_expression(&operand);
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
    }
    return operand;
}

void c_arithmetic_helpers(source_builder_t *builder) {
    add_static_source(builder, 0, L"#ifndef GOAT_C_NUMERIC_HELPERS");
    add_static_source(builder, 0, L"#define GOAT_C_NUMERIC_HELPERS");
    add_static_source(builder, 0, L"static inline int64_t goat_i64_bits(uint64_t value) {");
    add_static_source(builder, 1, L"return value <= INT64_MAX ? (int64_t)value :");
    add_static_source(builder, 2, L"INT64_MIN + (int64_t)(value - ((uint64_t)INT64_MAX + 1));");
    add_static_source(builder, 0, L"}");
    add_static_source(builder, 0, L"#endif");
}
