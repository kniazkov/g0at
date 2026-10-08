/** @file c_generation.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Transactional function emission and per-signature type access.
 */
#include "c_generation.h"

#include "c_lowering.h"
#include "graph/node.h"
#include "graph/replacement.h"
#include "graph/variable.h"
#include "lib/allocate.h"

c_value_type_t c_generation_parameter_type(const c_generation_context_t *context, size_t index) {
    const function_summary_t *summary = context ? context->summary : NULL;
    if (!summary || index >= summary->parameter_count || !summary->parameter_types
        || !summary->parameter_types[index])
        return C_VALUE_UNKNOWN;
    return classify_c_value_type(summary->parameter_types[index]->type);
}

c_value_type_t c_generation_return_type(const c_generation_context_t *context) {
    const function_summary_t *summary = context ? context->summary : NULL;
    return summary && summary->return_type ? classify_c_value_type(summary->return_type->type)
                                           : C_VALUE_UNKNOWN;
}

c_value_type_t c_generation_expression_type(const c_generation_context_t *context,
                                            const node_t *node) {
    if (context && context->replacement_proof && context->replacement_proof->node == node)
        return context->replacement_proof->type;
    c_expression_context_t proofs = {
        .head = context && context->summary ? context->summary->c_expressions : NULL};
    return node ? c_expression_type(&proofs, replacement_original(node)) : C_VALUE_UNKNOWN;
}

bool fail_c_generation(c_generation_context_t *context,
                       const node_t *node,
                       c_generation_status_t status) {
    if (context && context->status == C_GENERATION_OK) {
        context->status = status;
        context->failed_node = node;
    }
    return false;
}

void destroy_c_expression(c_generated_expression_t *expression) {
    FREE_STRING(expression->value);
    if (expression->prelude)
        destroy_source_builder(expression->prelude);
    *expression = (c_generated_expression_t){0};
}

/** @brief Backend-owned names avoid C keywords and arbitrary source fragments. */
bool c_function_name_is_valid(string_view_t name) {
    size_t prefix = name.data && name.length > 2 && !wmemcmp(name.data, L"g_", 2) ? 2 : 5;
    if (!name.data || name.length <= prefix || (prefix == 5 && wmemcmp(name.data, L"goat_", 5)))
        return false;
    for (size_t i = prefix; i < name.length; i++) {
        wchar_t c = name.data[i];
        if (c != L'_' && !(c >= L'a' && c <= L'z') && !(c >= L'A' && c <= L'Z')
            && !(c >= L'0' && c <= L'9'))
            return false;
    }
    return true;
}

/** @brief Monotone liveness for the selected C tree, including restored initializers. */
static void collect_required_bindings(const node_t *node, c_generation_context_t *context) {
    const node_t *selected = c_generation_replacement(context, node);
    if (selected != node) {
        collect_required_bindings(selected, context);
        return;
    }
    if (is_deletion(node))
        return;
    if (node->vtbl->type == NODE_VARIABLE) {
        const declarator_t *binding = ((const variable_t *)node)->declarator;
        if (!binding)
            return;
        for (size_t i = 0; i < context->required_bindings->size; i++)
            if (context->required_bindings->data[i] == binding)
                return;
        append_to_vector(context->required_bindings, (void *)binding);
        return;
    }
    if (node->vtbl->type == NODE_IF_ELSE) {
        abstract_truth_t truth = c_generation_condition_truth(context, get_node_child(node, 0));
        if (truth == ABSTRACT_TRUE || truth == ABSTRACT_FALSE) {
            const node_t *branch = get_node_child(node, truth == ABSTRACT_TRUE ? 1 : 2);
            if (branch)
                collect_required_bindings(branch, context);
            return;
        }
    }
    /* Untransformed stores also require storage, including those in restored subtrees. */
    for (size_t i = 0; i < get_node_child_count(node); i++)
        collect_required_bindings(get_node_child(node, i), context);
}

static c_generation_result_t generate_function(const function_summary_t *summary,
                                               string_view_t name,
                                               const c_generation_binding_t *bindings,
                                               const c_generation_callee_t *callees,
                                               bool definition) {
    const node_t *function = summary ? summary->function : NULL;
    if (!function || function->vtbl->type != NODE_FUNCTION_OBJECT
        || !c_function_name_is_valid(name))
        return (c_generation_result_t){.status = C_GENERATION_INVALID_REQUEST,
                                       .failed_node = function};
    if (summary->status != FUNCTION_ANALYZED || summary->c_support != FUNCTION_C_SUPPORTED
        || summary->c_blockers || !function_summary_is_pure(summary))
        return (c_generation_result_t){.status = C_GENERATION_NOT_PROVEN, .failed_node = function};
    c_generation_context_t context = {.summary = summary,
                                      .function_name = name,
                                      .bindings = bindings,
                                      .callees = callees,
                                      .module_definition = definition};
    if (c_generation_return_type(&context) != C_VALUE_INT64
        && c_generation_return_type(&context) != C_VALUE_DOUBLE)
        return (c_generation_result_t){.status = C_GENERATION_NOT_PROVEN, .failed_node = function};
    for (size_t i = 0; i < summary->parameter_count; i++) {
        c_value_type_t type = c_generation_parameter_type(&context, i);
        if (type != C_VALUE_INT64 && type != C_VALUE_DOUBLE)
            return (c_generation_result_t){.status = C_GENERATION_NOT_PROVEN,
                                           .failed_node = function};
    }
    context.required_bindings = create_vector();
    size_t previous;
    do {
        previous = context.required_bindings->size;
        collect_required_bindings(get_node_child(function, 1), &context);
    } while (context.required_bindings->size != previous);
    source_builder_t *builder = create_source_builder();
    bool success = generate_indented_c_code_from_node(function, &context, builder, 0);
    /* Bound the frame allocated before its entry guard. */
    if (definition
        && summary->parameter_count + context.local_count + context.temporary_count > 128)
        success = false;
    if (!success || !builder->count)
        fail_c_generation(&context, function, C_GENERATION_UNSUPPORTED);
    c_generation_result_t result = {.status = context.status, .failed_node = context.failed_node};
    if (result.status == C_GENERATION_OK) {
        result.helper_flags = context.helper_flags;
        if (definition) {
            result.source = build_source(builder);
        } else {
            source_builder_t *complete = create_source_builder();
            add_static_source(complete, 0, L"#include <stdint.h>");
            c_emit_headers(complete, context.helper_flags);
            add_formatted_source(complete, 0, build_source(builder));
            result.source = build_source(complete);
            destroy_source_builder(complete);
        }
    }
    destroy_source_builder(builder);
    destroy_vector(context.required_bindings);
    return result;
}

c_generation_result_t generate_c_function(const function_summary_t *summary,
                                          string_view_t name,
                                          const c_generation_binding_t *bindings,
                                          const c_generation_callee_t *callees) {
    return generate_function(summary, name, bindings, callees, false);
}

c_generation_result_t c_generate_definition(const function_summary_t *summary,
                                            string_view_t name,
                                            const c_generation_callee_t *callees) {
    return generate_function(summary, name, NULL, callees, true);
}

static const lattice_element_t *constant(const c_generation_context_t *context,
                                         const node_t *node) {
    c_expression_context_t proofs = {
        .head = context && context->summary ? context->summary->c_expressions : NULL};
    return c_expression_constant(&proofs, replacement_original(node));
}

abstract_truth_t c_generation_condition_truth(const c_generation_context_t *context,
                                              const node_t *condition) {
    const lattice_element_t *value = constant(context, condition);
    return value ? lattice_truth(value) : ABSTRACT_EITHER;
}

const node_t *c_generation_replacement(const c_generation_context_t *context, const node_t *node) {
    if (!is_replacement(node) && !is_deletion(node))
        return node;
    const node_t *archived = get_node_child(node, 0);
    const node_t *binding = NULL;
    if (archived->vtbl->type == NODE_VARIABLE_DECLARATOR
        || archived->vtbl->type == NODE_CONSTANT_DECLARATOR)
        binding = archived;
    else if (archived->vtbl->type == NODE_SIMPLE_ASSIGNMENT) {
        const node_t *left = get_node_child(archived, 0);
        if (left->vtbl->type == NODE_VARIABLE)
            binding = (const node_t *)((const variable_t *)left)->declarator;
    }
    if (binding && context && context->required_bindings) {
        for (size_t i = 0; i < context->required_bindings->size; i++)
            if (context->required_bindings->data[i] == binding)
                return archived;
        return is_deletion(node) ? node : get_node_child(node, 1);
    }
    if (is_deletion(node))
        return archived;
    const node_t *original = replacement_original(node);
    const node_t *result = replacement_result(node);
    if (node->vtbl->type == NODE_EXPRESSION_REPLACEMENT) {
        node_type_t type = result->vtbl->type;
        if (type == NODE_INTEGER || type == NODE_REAL || type == NODE_TRUE || type == NODE_FALSE) {
            /* Literal calculation returns its immutable payload and needs no state. */
            const lattice_element_t *value = calculate_node((node_t *)result, NULL, NULL);
            if (c_constants_equal(constant(context, original), value))
                return result;
        }
    } else if (original->vtbl->type == NODE_IF_ELSE) {
        abstract_truth_t truth = c_generation_condition_truth(context, get_node_child(original, 0));
        if (truth == ABSTRACT_TRUE || truth == ABSTRACT_FALSE) {
            const node_t *chosen = get_node_child(original, truth == ABSTRACT_TRUE ? 1 : 2);
            if (chosen && replacement_original(chosen) == replacement_original(result))
                return chosen;
            if (!chosen && result->vtbl->type == NODE_STATEMENT_EXPRESSION
                && !get_node_child_count(result))
                return result;
        }
    }
    return original;
}
