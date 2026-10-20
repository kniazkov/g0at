/** @file c_conditional.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Conditional native results preserve lazy branch evaluation.
 */
#include "c_control.h"
#include "c_lowering.h"
#include "graph/node.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"

c_generated_expression_t c_conditional(const node_t *node, c_generation_context_t *context) {
    abstract_truth_t truth = c_generation_condition_truth(context, get_node_child(node, 0));
    /* This proof only accepts total, discardable conditions. */
    if (truth == ABSTRACT_TRUE || truth == ABSTRACT_FALSE)
        return generate_c_code_from_node(get_node_child(node, truth == ABSTRACT_TRUE ? 1 : 2),
                                         context);
    c_generated_expression_t parts[3] = {0};
    c_value_type_t type = c_generation_expression_type(context, node);
    if (!c_type_name(type)) {
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    for (size_t i = 0; i < 3; i++) {
        parts[i] = generate_c_code_from_node(get_node_child(node, i), context);
        if (!parts[i].success) {
            for (size_t j = 0; j <= i; j++)
                destroy_c_expression(&parts[j]);
            return (c_generated_expression_t){0};
        }
    }
    source_builder_t *prelude = create_source_builder();
    c_emit_prelude(parts[0].prelude, prelude, 0);
    string_value_t value = format_string(L"g_t%zu", context->temporary_count++);
    add_source(prelude, 0, L"%s %s;", c_type_name(type), value.data);
    add_source(prelude,
               0,
               parts[0].type == C_VALUE_STRING ? L"if ((%s).length != 0) {" : L"if ((%s) != 0) {",
               parts[0].value.data);
    c_emit_prelude(parts[1].prelude, prelude, 1);
    add_source(prelude, 1, L"%s = %s;", value.data, parts[1].value.data);
    add_static_source(prelude, 0, L"} else {");
    c_emit_prelude(parts[2].prelude, prelude, 1);
    add_source(prelude, 1, L"%s = %s;", value.data, parts[2].value.data);
    add_static_source(prelude, 0, L"}");
    for (size_t i = 0; i < 3; i++)
        destroy_c_expression(&parts[i]);
    return (c_generated_expression_t){.success = true,
                                      .type = type,
                                      .value = value,
                                      .prelude = prelude};
}
