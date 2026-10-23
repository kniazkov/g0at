/** @file c_arithmetic.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Saturating integers and sequenced binary64 expression evaluation.
 */
#include "c_arithmetic.h"

#include "c_lowering.h"
#include "c_strings.h"
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
        add_source(prelude, line->indent, U"%s", line->text.data);
    }
    string_value_t name = format_string(U"g_t%zu", context->temporary_count++);
    if (type == C_VALUE_STRING) {
        add_source(prelude, 0, U"%s %s = %s;", c_type_name(type), name.data, operand->value.data);
        return name;
    }
    add_source(prelude,
               0,
               U"%s%s %s = (%s)(%s);",
               type == C_VALUE_DOUBLE ? U"volatile " : U"",
               c_type_name(type),
               name.data,
               c_type_name(type),
               operand->value.data);
    return name;
}

c_generated_expression_t
c_binary_arithmetic(const node_t *node, c_generation_context_t *context, gchar_t operation) {
    if (operation != U'+' && operation != U'-' && operation != U'*') {
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
    if (operation == U'+' && (left.type == C_VALUE_STRING || left.type == C_VALUE_CHAR)
        && (right.type == C_VALUE_STRING || numeric(right.type) || right.type == C_VALUE_BOOL
            || right.type == C_VALUE_CHAR)
        && context->module_definition
        && c_generation_expression_type(context, node) == C_VALUE_STRING) {
        source_builder_t *prelude = create_source_builder();
        string_value_t first = c_capture_operand(prelude, context, &left, left.type);
        string_value_t a = c_string_from_value(prelude, context, left.type, first.data);
        FREE_STRING(first);
        string_value_t captured = c_capture_operand(prelude, context, &right, right.type);
        string_value_t b = c_string_from_value(prelude, context, right.type, captured.data);
        FREE_STRING(captured);
        string_value_t buffer = format_string(U"g_t%zu", context->temporary_count++);
        add_source(prelude, 0, U"goat_native_string_buffer_v2_t %s = {0};", buffer.data);
        add_source(prelude,
                   0,
                   U"if (goat_native_string_concat(g_context, %s, %s, &%s) != GOAT_NATIVE_OK) "
                   U"longjmp(g_guard->recovery, 1);",
                   a.data,
                   b.data,
                   buffer.data);
        string_value_t value = format_string(U"goat_native_string_view(%s)", buffer.data);
        FREE_STRING(a);
        FREE_STRING(b);
        FREE_STRING(buffer);
        destroy_c_expression(&left);
        destroy_c_expression(&right);
        return (c_generated_expression_t){.success = true,
                                          .type = C_VALUE_STRING,
                                          .value = value,
                                          .prelude = prelude};
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
    string_value_t value = format_string(U"g_t%zu", context->temporary_count++);
    if (type == C_VALUE_INT64) {
        context->helper_flags |= operation == U'+'   ? C_HELPER_I64_ADD
                                 : operation == U'-' ? C_HELPER_I64_SUB
                                                     : C_HELPER_I64_MUL;
        add_source(prelude,
                   0,
                   U"int64_t %s = g_i64_%s(%s, %s);",
                   value.data,
                   operation == U'+'   ? U"add"
                   : operation == U'-' ? U"sub"
                                       : U"mul",
                   a.data,
                   b.data);
    } else
        add_source(prelude,
                   0,
                   U"volatile double %s = %s %c %s;",
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
    string_value_t value = format_string(U"g_t%zu", context->temporary_count++);
    if (operand.type == C_VALUE_INT64) {
        context->helper_flags |= C_HELPER_I64_NEG;
        add_source(prelude, 0, U"int64_t %s = g_i64_neg(%s);", value.data, argument.data);
    } else
        add_source(prelude, 0, U"volatile double %s = -%s;", value.data, argument.data);
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
