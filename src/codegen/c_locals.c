/** @file c_locals.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Scoped storage identities, sequenced initializers and stable assignment results.
 */
#include "c_locals.h"

#include "c_arithmetic.h"
#include "c_control.h"
#include "c_lowering.h"
#include "graph/replacement.h"
#include "graph/variable.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"

static const c_generation_binding_t *find_binding(const c_generation_context_t *context,
                                                  const node_t *declaration) {
    for (const c_generation_binding_t *binding = context->bindings; binding;
         binding = binding->next) {
        if (binding->declaration == declaration)
            return binding;
    }
    return NULL;
}

/** @brief Descends through branches, but leaves nested lexical scopes to their own emitter. */
static bool prepare(const node_t *node,
                    c_generation_context_t *context,
                    source_builder_t *builder,
                    size_t indent) {
    node = c_generation_replacement(context, node);
    if (node->vtbl->type == NODE_IF_ELSE) {
        abstract_truth_t truth = c_generation_condition_truth(context, get_node_child(node, 0));
        if (truth == ABSTRACT_TRUE || truth == ABSTRACT_FALSE) {
            const node_t *chosen = get_node_child(node, truth == ABSTRACT_TRUE ? 1 : 2);
            return !chosen || prepare(chosen, context, builder, indent);
        }
    }
    if (node->vtbl->type == NODE_FUNCTION_OBJECT || node->vtbl->type == NODE_STATEMENT_LIST)
        return true;
    if (node->vtbl->type == NODE_VARIABLE_DECLARATOR
        || node->vtbl->type == NODE_CONSTANT_DECLARATOR) {
        const node_t *initial = get_node_child(node, 0);
        c_value_type_t type = c_generation_expression_type(context, initial);
        if (!initial || !c_type_name(type) || find_binding(context, node))
            return fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        string_value_t name = format_string(L"goat_l%zu", context->local_count++);
        c_generation_binding_t *binding = ALLOC(sizeof(*binding));
        *binding = (c_generation_binding_t){.next = context->bindings,
                                            .declaration = node,
                                            .name = {name.data, name.length},
                                            .type = type};
        context->bindings = binding;
        add_source(builder,
                   indent,
                   L"%s%s %s;",
                   type == C_VALUE_DOUBLE ? L"volatile " : L"",
                   c_type_name(type),
                   name.data);
        /* Suppress unused-storage warnings without reading an uninitialized value. */
        add_source(builder, indent, L"(void)&%s;", name.data);
        return true;
    }
    for (size_t i = 0; i < get_node_child_count(node); i++) {
        if (!prepare(get_node_child(node, i), context, builder, indent))
            return false;
    }
    return true;
}

bool c_prepare_locals(const node_t *scope,
                      c_generation_context_t *context,
                      source_builder_t *builder,
                      size_t indent) {
    for (size_t i = 0; i < get_node_child_count(scope); i++) {
        if (!prepare(get_node_child(scope, i), context, builder, indent))
            return false;
    }
    return true;
}

void c_release_locals(c_generation_context_t *context, const c_generation_binding_t *saved) {
    while (context->bindings != saved) {
        const c_generation_binding_t *binding = context->bindings;
        context->bindings = binding->next;
        FREE((void *)binding->name.data);
        FREE((void *)binding);
    }
}

bool c_emit_declarations(const node_t *node,
                         c_generation_context_t *context,
                         source_builder_t *builder,
                         size_t indent) {
    context->terminates = false;
    for (size_t i = 0; i < get_node_child_count(node); i++) {
        if (!generate_indented_c_code_from_node(get_node_child(node, i), context, builder, indent))
            return false;
    }
    return true;
}

bool c_emit_declarator(const node_t *node,
                       c_generation_context_t *context,
                       source_builder_t *builder,
                       size_t indent) {
    const c_generation_binding_t *binding = find_binding(context, node);
    const node_t *initial = get_node_child(node, 0);
    if (!binding || !initial)
        return fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
    c_generated_expression_t value = generate_c_code_from_node(initial, context);
    if (!value.success)
        return false;
    bool valid = binding->type == value.type;
    if (valid) {
        c_emit_prelude(value.prelude, builder, indent);
        add_source(builder, indent, L"%s = %s;", binding->name.data, value.value.data);
    }
    destroy_c_expression(&value);
    context->terminates = false;
    return valid || fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
}

c_generated_expression_t c_assignment(const node_t *node, c_generation_context_t *context) {
    const node_t *left = replacement_original(get_node_child(node, 0));
    const declarator_t *declaration =
        left && left->vtbl->type == NODE_VARIABLE ? ((const variable_t *)left)->declarator : NULL;
    const c_generation_binding_t *binding =
        declaration ? find_binding(context, &declaration->base) : NULL;
    if (!binding || declaration->base.vtbl->type == NODE_CONSTANT_DECLARATOR) {
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    c_generated_expression_t right = generate_c_code_from_node(get_node_child(node, 1), context);
    if (!right.success)
        return right;
    if (right.type != binding->type
        || c_generation_expression_type(context, node) != binding->type) {
        destroy_c_expression(&right);
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    source_builder_t *prelude = create_source_builder();
    string_value_t value = c_capture_operand(prelude, context, &right, binding->type);
    add_source(prelude, 0, L"%s = %s;", binding->name.data, value.data);
    destroy_c_expression(&right);
    return (c_generated_expression_t){.success = true,
                                      .type = binding->type,
                                      .value = value,
                                      .prelude = prelude};
}
