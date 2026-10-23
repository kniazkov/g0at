/** @file c_control.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Exact numeric comparisons and structured C control flow.
 */
#include "c_control.h"

#include "c_arithmetic.h"
#include "c_locals.h"
#include "c_lowering.h"
#include "graph/comparison.h"
#include "graph/loop.h"
#include "graph/replacement.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"

void c_emit_prelude(const source_builder_t *prelude, source_builder_t *builder, size_t indent) {
    for (size_t i = 0; prelude && i < prelude->count; i++) {
        const line_of_code_t *line = &prelude->lines[i];
        add_source(builder, indent + line->indent, U"%s", line->text.data);
    }
}

static bool numeric(c_value_type_t type) {
    return type == C_VALUE_INT64 || type == C_VALUE_DOUBLE || type == C_VALUE_CHAR;
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
    bool left_number = left.type == C_VALUE_INT64 || left.type == C_VALUE_DOUBLE;
    bool right_number = right.type == C_VALUE_INT64 || right.type == C_VALUE_DOUBLE;
    bool same_domain = (left_number && right_number) || left.type == right.type;
    bool equality = node->vtbl->type == NODE_EQUAL || node->vtbl->type == NODE_NOT_EQUAL;
    if (!c_type_name(left.type) || !c_type_name(right.type)
        || (same_domain && !numeric(left.type) && left.type != C_VALUE_BOOL)
        || (!same_domain && !equality)
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
    string_value_t value = format_string(U"g_t%zu", context->temporary_count++);
    bool integer_like = (left.type == C_VALUE_INT64 || left.type == C_VALUE_CHAR)
                        && (right.type == C_VALUE_INT64 || right.type == C_VALUE_CHAR);
    if (!same_domain) {
        add_source(prelude, 0, U"(void)%s; (void)%s;", a.data, b.data);
        add_source(prelude,
                   0,
                   U"bool %s = %s;",
                   value.data,
                   equality && node->vtbl->type == NODE_NOT_EQUAL ? U"true" : U"false");
    } else if (integer_like || left.type == right.type) {
        static const gchar_t *symbols[] = {U"<", U"<=", U">", U">=", U"==", U"!="};
        add_source(prelude, 0, U"bool %s = %s %s %s;", value.data, a.data, symbols[kind], b.data);
    } else {
        context->helper_flags |= C_HELPER_COMPARISON;
        /* Helper bits: less=1, equal=2, greater=4, unordered=8. */
        static const unsigned masks[] = {1, 3, 4, 6, 2, 13};
        static const unsigned reversed[] = {4, 6, 1, 3, 2, 13};
        bool integer_left = left.type == C_VALUE_INT64 || left.type == C_VALUE_CHAR;
        add_source(prelude,
                   0,
                   U"bool %s = (g_compare_i64_double(%s, %s) & %u) != 0;",
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
        .value = node->vtbl->type == NODE_TRUE ? STATIC_STRING(U"true") : STATIC_STRING(U"false")};
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
        add_source(builder, indent, U"(void)(%s);", condition.value.data);
        destroy_c_expression(&condition);
        context->terminates = false;
        const node_t *chosen = get_node_child(node, truth == ABSTRACT_TRUE ? 1 : 2);
        return !chosen || generate_indented_c_code_from_node(chosen, context, builder, indent);
    }
    /* Goat and C both consider NaN true and either signed zero false. */
    add_source(builder, indent, U"if (%s) {", condition.value.data);
    destroy_c_expression(&condition);
    context->terminates = false;
    if (!generate_indented_c_code_from_node(get_node_child(node, 1), context, builder, indent + 1))
        return false;
    bool true_returns = context->terminates;
    context->terminates = false;
    const node_t *otherwise = get_node_child(node, 2);
    if (otherwise) {
        add_static_source(builder, indent, U"} else {");
        if (!generate_indented_c_code_from_node(otherwise, context, builder, indent + 1))
            return false;
    }
    context->terminates = true_returns && context->terminates;
    add_static_source(builder, indent, U"}");
    return true;
}

bool c_emit_block(const node_t *node,
                  c_generation_context_t *context,
                  source_builder_t *builder,
                  size_t indent) {
    add_static_source(builder, indent, U"{");
    if (!c_emit_body(node, context, builder, indent + 1))
        return false;
    add_static_source(builder, indent, U"}");
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
        add_static_source(builder, indent, U";");
        return true;
    }
    if (replacement_original(child)->vtbl->type == NODE_STATEMENT_LIST)
        return generate_indented_c_code_from_node(child, context, builder, indent);
    c_generated_expression_t expression = generate_c_code_from_node(child, context);
    if (!expression.success)
        return false;
    c_emit_prelude(expression.prelude, builder, indent);
    add_source(builder, indent, U"(void)(%s);", expression.value.data);
    destroy_c_expression(&expression);
    return true;
}

static string_value_t truth_value(const c_generated_expression_t *expression);

bool c_emit_loop_jump(const node_t *node,
                      c_generation_context_t *context,
                      source_builder_t *builder,
                      size_t indent) {
    if (!context->in_loop)
        return fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
    add_source(builder,
               indent,
               U"goto g_loop_%zu_%s;",
               context->loop_id,
               node->vtbl->type == NODE_BREAK ? U"end" : U"next");
    context->terminates = true;
    return true;
}

bool c_emit_for(const node_t *node,
                c_generation_context_t *context,
                source_builder_t *builder,
                size_t indent) {
    const c_generation_binding_t *saved = context->bindings;
    size_t outer_id = context->loop_id;
    bool outer_loop = context->in_loop;
    size_t id = context->temporary_count++;
    context->loop_id = id;
    context->in_loop = true;
    bool post = node->vtbl->type == NODE_DO_WHILE;
    add_static_source(builder, indent, U"{");
    bool success = c_prepare_locals(node, context, builder, indent + 1)
                   && generate_indented_c_code_from_node(get_node_child(node, 0),
                                                         context,
                                                         builder,
                                                         indent + 1);
    bool body_stops = false;
    c_generated_expression_t condition = {0};
    if (success && !post) {
        condition = generate_c_code_from_node(get_node_child(node, 1), context);
        success = condition.success;
    }
    if (success) {
        string_value_t truth = post ? (string_value_t){0} : truth_value(&condition);
        add_static_source(builder, indent + 1, U"for (;;) {");
        if (!post) {
            c_emit_prelude(condition.prelude, builder, indent + 2);
            add_source(builder, indent + 2, U"if (!(%s)) goto g_loop_%zu_end;", truth.data, id);
        }
        context->terminates = false;
        success = generate_indented_c_code_from_node(get_node_child(node, 3),
                                                     context,
                                                     builder,
                                                     indent + 2);
        bool reaches_step = !context->terminates || loop_has_jump(node, true);
        body_stops = !reaches_step;
        add_source(builder, indent + 2, U"goto g_loop_%zu_next;", id);
        add_source(builder, indent + 2, U"g_loop_%zu_next:;", id);
        context->terminates = false;
        if (success && reaches_step)
            success = generate_indented_c_code_from_node(get_node_child(node, 2),
                                                         context,
                                                         builder,
                                                         indent + 2);
        if (success && post && reaches_step) {
            condition = generate_c_code_from_node(get_node_child(node, 1), context);
            success = condition.success;
            if (success) {
                truth = truth_value(&condition);
                c_emit_prelude(condition.prelude, builder, indent + 2);
                add_source(builder, indent + 2, U"if (!(%s)) goto g_loop_%zu_end;", truth.data, id);
            }
        }
        add_static_source(builder, indent + 1, U"}");
        add_source(builder, indent + 1, U"goto g_loop_%zu_end;", id);
        add_source(builder, indent + 1, U"g_loop_%zu_end:;", id);
        FREE_STRING(truth);
    }
    destroy_c_expression(&condition);
    c_release_locals(context, saved);
    context->in_loop = outer_loop;
    context->loop_id = outer_id;
    context->terminates =
        ((post && body_stops)
         || c_generation_condition_truth(context, get_node_child(node, 1)) == ABSTRACT_TRUE)
        && !loop_has_jump(node, false);
    add_static_source(builder, indent, U"}");
    return success;
}

/** @brief Tests native scalar truth without changing the represented value. */
static string_value_t truth_value(const c_generated_expression_t *expression) {
    return format_string(expression->type == C_VALUE_STRING ? U"(%s).length != 0" : U"(%s) != 0",
                         expression->value.data);
}

c_generated_expression_t c_logic(const node_t *node, c_generation_context_t *context) {
    if (c_generation_expression_type(context, node) != C_VALUE_BOOL) {
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    c_generated_expression_t left = generate_c_code_from_node(get_node_child(node, 0), context);
    if (!left.success)
        return left;
    source_builder_t *prelude = create_source_builder();
    c_emit_prelude(left.prelude, prelude, 0);
    string_value_t truth = truth_value(&left);
    string_value_t value = format_string(U"g_t%zu", context->temporary_count++);
    node_type_t type = node->vtbl->type;
    add_source(prelude,
               0,
               U"bool %s = %s(%s);",
               value.data,
               type == NODE_LOGICAL_NOT ? U"!" : U"",
               truth.data);
    FREE_STRING(truth);
    destroy_c_expression(&left);
    if (type == NODE_LOGICAL_AND || type == NODE_LOGICAL_OR) {
        abstract_truth_t proven = c_generation_condition_truth(context, get_node_child(node, 0));
        bool skip = proven == (type == NODE_LOGICAL_OR ? ABSTRACT_TRUE : ABSTRACT_FALSE);
        if (!skip) {
            c_generated_expression_t right =
                generate_c_code_from_node(get_node_child(node, 1), context);
            if (!right.success) {
                FREE_STRING(value);
                destroy_source_builder(prelude);
                return right;
            }
            add_source(prelude,
                       0,
                       U"if (%s%s) {",
                       type == NODE_LOGICAL_OR ? U"!" : U"",
                       value.data);
            c_emit_prelude(right.prelude, prelude, 1);
            truth = truth_value(&right);
            add_source(prelude, 1, U"%s = %s;", value.data, truth.data);
            add_static_source(prelude, 0, U"}");
            FREE_STRING(truth);
            destroy_c_expression(&right);
        }
    }
    return (c_generated_expression_t){.success = true,
                                      .type = C_VALUE_BOOL,
                                      .value = value,
                                      .prelude = prelude};
}
