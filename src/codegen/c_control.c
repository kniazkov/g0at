/** @file c_control.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Exact numeric comparisons and structured C control flow.
 */
#include "c_control.h"

#include "c_arithmetic.h"
#include "c_locals.h"
#include "c_lowering.h"
#include "graph/comparison.h"
#include "graph/replacement.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"

void c_emit_prelude(const source_builder_t *prelude, source_builder_t *builder, size_t indent) {
    for (size_t i = 0; prelude && i < prelude->count; i++) {
        const line_of_code_t *line = &prelude->lines[i];
        add_source(builder, indent + line->indent, L"%s", line->text.data);
    }
}

static bool numeric(c_value_type_t type) {
    return type == C_VALUE_INT64 || type == C_VALUE_DOUBLE;
}

c_generated_expression_t c_comparison(const node_t *node, c_generation_context_t *context) {
    c_generated_expression_t left = generate_c_code_from_node(get_node_child(node, 0), context);
    if (!left.success)
        return left;
    c_generated_expression_t right = generate_c_code_from_node(get_node_child(node, 1), context);
    if (!right.success) {
        destroy_c_expression(&left);
        return right;
    }
    if (!numeric(left.type) || !numeric(right.type)
        || c_generation_expression_type(context, node) != C_VALUE_BOOL) {
        destroy_c_expression(&left);
        destroy_c_expression(&right);
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    source_builder_t *prelude = create_source_builder();
    string_value_t a = c_capture_operand(prelude, context, &left, left.type);
    string_value_t b = c_capture_operand(prelude, context, &right, right.type);
    comparison_kind_t kind = node_comparison_kind(node->vtbl->type);
    string_value_t value = format_string(L"g_t%zu", context->temporary_count++);
    if (left.type == right.type) {
        static const wchar_t *symbols[] = {L"<", L"<=", L">", L">=", L"==", L"!="};
        add_source(prelude, 0, L"bool %s = %s %s %s;", value.data, a.data, symbols[kind], b.data);
    } else {
        context->helper_flags |= C_HELPER_COMPARISON;
        /* Helper bits: less=1, equal=2, greater=4, unordered=8. */
        static const unsigned masks[] = {1, 3, 4, 6, 2, 13};
        static const unsigned reversed[] = {4, 6, 1, 3, 2, 13};
        bool integer_left = left.type == C_VALUE_INT64;
        add_source(prelude,
                   0,
                   L"bool %s = (g_compare_i64_double(%s, %s) & %u) != 0;",
                   value.data,
                   integer_left ? a.data : b.data,
                   integer_left ? b.data : a.data,
                   integer_left ? masks[kind] : reversed[kind]);
    }
    FREE_STRING(a);
    FREE_STRING(b);
    destroy_c_expression(&left);
    destroy_c_expression(&right);
    return (c_generated_expression_t){.success = true,
                                      .type = C_VALUE_BOOL,
                                      .value = value,
                                      .prelude = prelude};
}

c_generated_expression_t c_boolean(const node_t *node, c_generation_context_t *context) {
    if (c_generation_expression_type(context, node) != C_VALUE_BOOL) {
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    return (c_generated_expression_t){
        .success = true,
        .type = C_VALUE_BOOL,
        .literal_truth = node->vtbl->type == NODE_TRUE ? ABSTRACT_TRUE : ABSTRACT_FALSE,
        .value = node->vtbl->type == NODE_TRUE ? STATIC_STRING(L"true") : STATIC_STRING(L"false")};
}

bool c_emit_if(const node_t *node,
               c_generation_context_t *context,
               source_builder_t *builder,
               size_t indent) {
    abstract_truth_t proven = c_generation_condition_truth(context, get_node_child(node, 0));
    if (proven == ABSTRACT_TRUE || proven == ABSTRACT_FALSE) {
        context->terminates = false;
        const node_t *chosen = get_node_child(node, proven == ABSTRACT_TRUE ? 1 : 2);
        return !chosen || generate_indented_c_code_from_node(chosen, context, builder, indent);
    }
    c_generated_expression_t condition =
        generate_c_code_from_node(get_node_child(node, 0), context);
    if (!condition.success)
        return false;
    if (!numeric(condition.type) && condition.type != C_VALUE_BOOL) {
        destroy_c_expression(&condition);
        return fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
    }
    c_emit_prelude(condition.prelude, builder, indent);
    abstract_truth_t truth = condition.literal_truth;
    if (truth == ABSTRACT_TRUE || truth == ABSTRACT_FALSE) {
        add_source(builder, indent, L"(void)(%s);", condition.value.data);
        destroy_c_expression(&condition);
        context->terminates = false;
        const node_t *chosen = get_node_child(node, truth == ABSTRACT_TRUE ? 1 : 2);
        return !chosen || generate_indented_c_code_from_node(chosen, context, builder, indent);
    }
    /* Goat and C both consider NaN true and either signed zero false. */
    add_source(builder, indent, L"if (%s) {", condition.value.data);
    destroy_c_expression(&condition);
    context->terminates = false;
    if (!generate_indented_c_code_from_node(get_node_child(node, 1), context, builder, indent + 1))
        return false;
    bool true_returns = context->terminates;
    context->terminates = false;
    const node_t *otherwise = get_node_child(node, 2);
    if (otherwise) {
        add_static_source(builder, indent, L"} else {");
        if (!generate_indented_c_code_from_node(otherwise, context, builder, indent + 1))
            return false;
    }
    context->terminates = true_returns && context->terminates;
    add_static_source(builder, indent, L"}");
    return true;
}

bool c_emit_block(const node_t *node,
                  c_generation_context_t *context,
                  source_builder_t *builder,
                  size_t indent) {
    add_static_source(builder, indent, L"{");
    if (!c_emit_body(node, context, builder, indent + 1))
        return false;
    add_static_source(builder, indent, L"}");
    return true;
}

bool c_emit_statement(const node_t *node,
                      c_generation_context_t *context,
                      source_builder_t *builder,
                      size_t indent) {
    context->terminates = false;
    const node_t *child = get_node_child(node, 0);
    if (child && is_deletion(child) && c_generation_replacement(context, child) == child)
        return true;
    if (!child) {
        add_static_source(builder, indent, L";");
        return true;
    }
    if (replacement_original(child)->vtbl->type == NODE_STATEMENT_LIST)
        return generate_indented_c_code_from_node(child, context, builder, indent);
    c_generated_expression_t expression = generate_c_code_from_node(child, context);
    if (!expression.success)
        return false;
    c_emit_prelude(expression.prelude, builder, indent);
    add_source(builder, indent, L"(void)(%s);", expression.value.data);
    destroy_c_expression(&expression);
    return true;
}

void c_control_helpers(source_builder_t *builder) {
    add_static_source(builder, 0, L"");
    add_static_source(builder, 0, L"#ifndef GOAT_C_COMPARISON_HELPERS");
    add_static_source(builder, 0, L"#define GOAT_C_COMPARISON_HELPERS");
    add_static_source(
        builder,
        0,
        L"static inline unsigned g_compare_i64_double(int64_t integer, double real) {");
    add_static_source(builder, 1, L"if (isnan(real)) return 8;");
    add_static_source(builder, 1, L"if (real >= 0x1p63) return 1;");
    add_static_source(builder, 1, L"if (real < -0x1p63) return 4;");
    add_static_source(builder, 1, L"int64_t truncated = (int64_t)real;");
    add_static_source(builder, 1, L"if (integer < truncated) return 1;");
    add_static_source(builder, 1, L"if (integer > truncated) return 4;");
    add_static_source(builder, 1, L"double whole;");
    add_static_source(builder, 1, L"double fraction = modf(real, &whole);");
    add_static_source(builder, 1, L"return fraction > 0 ? 1 : fraction < 0 ? 4 : 2;");
    add_static_source(builder, 0, L"}");
    add_static_source(builder, 0, L"#endif");
}

bool c_emit_for(const node_t *node,
                c_generation_context_t *context,
                source_builder_t *builder,
                size_t indent) {
    const c_generation_binding_t *saved = context->bindings;
    add_static_source(builder, indent, L"{");
    bool success = c_prepare_locals(node, context, builder, indent + 1)
                   && generate_indented_c_code_from_node(get_node_child(node, 0),
                                                         context,
                                                         builder,
                                                         indent + 1);
    c_generated_expression_t condition = {0};
    if (success) {
        condition = generate_c_code_from_node(get_node_child(node, 1), context);
        success = condition.success;
    }
    if (success) {
        add_static_source(builder, indent + 1, L"for (;;) {");
        c_emit_prelude(condition.prelude, builder, indent + 2);
        add_source(builder, indent + 2, L"if (!(%s)) break;", condition.value.data);
        context->terminates = false;
        success = generate_indented_c_code_from_node(get_node_child(node, 3),
                                                     context,
                                                     builder,
                                                     indent + 2);
        if (success && !context->terminates)
            success = generate_indented_c_code_from_node(get_node_child(node, 2),
                                                         context,
                                                         builder,
                                                         indent + 2);
        add_static_source(builder, indent + 1, L"}");
    }
    destroy_c_expression(&condition);
    c_release_locals(context, saved);
    context->terminates =
        c_generation_condition_truth(context, get_node_child(node, 1)) == ABSTRACT_TRUE;
    add_static_source(builder, indent, L"}");
    return success;
}
