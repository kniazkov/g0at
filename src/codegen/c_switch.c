/** @file c_switch.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Native ordered comparisons preserve selector and label side effects.
 */
#include "c_switch.h"

#include "analysis/c_body.h"
#include "analysis/comparison.h"
#include "c_arithmetic.h"
#include "c_control.h"
#include "c_lowering.h"
#include "graph/replacement.h"
#include "graph/switch.h"
#include "lib/memory/allocate.h"
#include "lib/strings/string_ext.h"

static bool number(c_value_type_t type) {
    return type == C_VALUE_INT64 || type == C_VALUE_DOUBLE;
}

static abstract_truth_t
match(const node_t *left, const node_t *right, const c_expression_context_t *proofs) {
    left = replacement_original(left);
    right = replacement_original(right);
    c_value_type_t a = c_expression_type(proofs, left), b = c_expression_type(proofs, right);
    if (a != C_VALUE_UNKNOWN && b != C_VALUE_UNKNOWN && a != b && !(number(a) && number(b)))
        return ABSTRACT_FALSE;
    const lattice_element_t *x = c_expression_constant(proofs, left),
                            *y = c_expression_constant(proofs, right);
    return x && y ? lattice_truth(lattice_compare(x, y, COMPARE_EQUAL)) : ABSTRACT_EITHER;
}

bool c_switch_supported(const node_t *node,
                        const lattice_element_t *value,
                        const c_expression_context_t *proofs) {
    if (!proofs || !proofs->graph || !c_body_node_supported(get_node_child(node, 0), proofs))
        return false;
    bool pending = true;
    for (size_t i = 0; i < switch_branch_count(node) && pending; i++) {
        const switch_branch_t *branch = switch_branch(node, i);
        bool selected = !branch->count;
        for (size_t j = 0; j < branch->count && pending; j++) {
            if (!c_body_node_supported(branch->labels[j], proofs))
                return false;
            abstract_truth_t truth = match(get_node_child(node, 0), branch->labels[j], proofs);
            selected |= truth != ABSTRACT_FALSE;
            if (truth == ABSTRACT_TRUE)
                pending = false;
        }
        if (selected && !c_body_node_supported(branch->body, proofs))
            return false;
    }
    return true;
}

bool c_switch_falls_through(const node_t *node, const c_expression_context_t *proofs) {
    bool pending = true;
    for (size_t i = 0; i < switch_branch_count(node) && pending; i++) {
        const switch_branch_t *branch = switch_branch(node, i);
        bool selected = !branch->count;
        if (!branch->count)
            pending = false;
        for (size_t j = 0; j < branch->count && pending; j++) {
            abstract_truth_t truth = match(get_node_child(node, 0), branch->labels[j], proofs);
            selected |= truth != ABSTRACT_FALSE;
            if (truth == ABSTRACT_TRUE)
                pending = false;
        }
        if (selected && c_body_falls_through(branch->body, proofs))
            return true;
    }
    return pending;
}

static string_value_t equality(c_generation_context_t *context,
                               c_value_type_t a,
                               c_value_type_t b,
                               string_value_t left,
                               string_value_t right) {
    if (a != b && !(number(a) && number(b)))
        return STATIC_STRING(U"false");
    if (a == C_VALUE_STRING)
        return format_string(U"(%s.length == %s.length && (!%s.length || MEMCMP(%s.data, %s.data, "
                             U"%s.length) == 0))",
                             left.data,
                             right.data,
                             left.data,
                             left.data,
                             right.data,
                             left.data);
    if (a == b)
        return format_string(U"(%s == %s)", left.data, right.data);
    context->helper_flags |= C_HELPER_COMPARISON;
    return format_string(U"((g_compare_i64_double(%s, %s) & 2) != 0)",
                         a == C_VALUE_INT64 ? left.data : right.data,
                         a == C_VALUE_INT64 ? right.data : left.data);
}

bool c_emit_switch(const node_t *node,
                   c_generation_context_t *context,
                   source_builder_t *builder,
                   size_t indent) {
    c_expression_context_t proofs = {.head = context->summary->c_expressions};
    c_generated_expression_t selector = generate_c_code_from_node(get_node_child(node, 0), context);
    if (!selector.success)
        return false;
    source_builder_t *prelude = create_source_builder();
    string_value_t saved = c_capture_operand(prelude, context, &selector, selector.type);
    c_emit_prelude(prelude, builder, indent);
    destroy_source_builder(prelude);
    add_source(builder, indent, U"(void)%s;", saved.data);
    size_t id = context->temporary_count++;
    bool pending = true, success = true, can_finish = false;
    for (size_t i = 0; i < switch_branch_count(node) && pending && success; i++) {
        const switch_branch_t *branch = switch_branch(node, i);
        bool selected = !branch->count;
        if (!branch->count)
            pending = false;
        for (size_t j = 0; j < branch->count && pending && success; j++) {
            c_generated_expression_t label = generate_c_code_from_node(branch->labels[j], context);
            if (!label.success) {
                success = false;
                break;
            }
            prelude = create_source_builder();
            string_value_t value = c_capture_operand(prelude, context, &label, label.type);
            c_emit_prelude(prelude, builder, indent);
            destroy_source_builder(prelude);
            add_source(builder, indent, U"(void)%s;", value.data);
            abstract_truth_t truth = match(get_node_child(node, 0), branch->labels[j], &proofs);
            if (truth == ABSTRACT_TRUE) {
                add_source(builder, indent, U"goto g_switch_%zu_case_%zu;", id, i);
                selected = true;
                pending = false;
            } else if (truth != ABSTRACT_FALSE) {
                string_value_t test = equality(context, selector.type, label.type, saved, value);
                add_source(builder,
                           indent,
                           U"if (%s) goto g_switch_%zu_case_%zu;",
                           test.data,
                           id,
                           i);
                FREE_STRING(test);
                selected = true;
            }
            FREE_STRING(value);
            destroy_c_expression(&label);
        }
        if (selected && success) {
            if (branch->count && pending)
                add_source(builder, indent, U"goto g_switch_%zu_next_%zu;", id, i);
            if (branch->count)
                add_source(builder, indent, U"g_switch_%zu_case_%zu:;", id, i);
            context->terminates = false;
            success = generate_indented_c_code_from_node(branch->body, context, builder, indent);
            can_finish |= !context->terminates;
            add_source(builder, indent, U"goto g_switch_%zu_end;", id);
            if (branch->count && pending)
                add_source(builder, indent, U"g_switch_%zu_next_%zu:;", id, i);
        }
    }
    add_source(builder, indent, U"goto g_switch_%zu_end;", id);
    add_source(builder, indent, U"g_switch_%zu_end:;", id);
    context->terminates = !pending && !can_finish;
    FREE_STRING(saved);
    destroy_c_expression(&selector);
    return success;
}
