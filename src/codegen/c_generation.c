/** @file c_generation.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Transactional function emission and per-signature type access.
 */
#include "c_generation.h"

#include "c_lowering.h"
#include "graph/node.h"
#include "graph/replacement.h"
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
    if (!name.data || name.length < 6 || wmemcmp(name.data, L"goat_", 5))
        return false;
    for (size_t i = 5; i < name.length; i++) {
        wchar_t c = name.data[i];
        if (c != L'_' && !(c >= L'a' && c <= L'z') && !(c >= L'A' && c <= L'Z')
            && !(c >= L'0' && c <= L'9'))
            return false;
    }
    return true;
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
    source_builder_t *builder = create_source_builder();
    bool success = generate_indented_c_code_from_node(function, &context, builder, 0);
    if (!success || !builder->count)
        fail_c_generation(&context, function, C_GENERATION_UNSUPPORTED);
    c_generation_result_t result = {.status = context.status, .failed_node = context.failed_node};
    if (result.status == C_GENERATION_OK)
        result.source = build_source(builder);
    destroy_source_builder(builder);
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
