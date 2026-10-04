/** @file test_c_generation.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Type isolation, explicit rejection and transactional output tests.
 */
#include "test_c_generation.h"

#include "analysis/analysis.h"
#include "analysis_test_support.h"
#include "cli/options.h"
#include "codegen/c_generation.h"
#include "graph/expression.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"
#include "test_macro.h"

#include <stdio.h>
#include <wchar.h>

bool test_c_generation_context(void) {
    arena_t *arena = create_arena(32);
    node_t *function = create_function_object_node(arena, NULL, 0);
    node_t *expression = create_integer_node(arena, 1);
    function_summary_t *integer = create_function_summary(arena, function, 1);
    function_summary_t *real = create_function_summary(arena, function, 1);
    integer->parameter_types[0] = integer->return_type = make_integer_element();
    real->parameter_types[0] = real->return_type = make_real_element();
    c_expression_proof_t int_proof = {.node = expression, .type = C_VALUE_INT64};
    c_expression_proof_t real_proof = {.node = expression, .type = C_VALUE_DOUBLE};
    integer->c_expressions = &int_proof;
    real->c_expressions = &real_proof;
    c_generation_context_t a = {.summary = integer}, b = {.summary = real};
    expression->flags = NODE_FLAG_C_COMPATIBLE;
    ASSERT(c_generation_parameter_type(&a, 0) == C_VALUE_INT64);
    ASSERT(c_generation_parameter_type(&b, 0) == C_VALUE_DOUBLE);
    ASSERT(c_generation_return_type(&a) == C_VALUE_INT64);
    ASSERT(c_generation_return_type(&b) == C_VALUE_DOUBLE);
    ASSERT(c_generation_expression_type(&a, expression) == C_VALUE_INT64);
    ASSERT(c_generation_expression_type(&b, expression) == C_VALUE_DOUBLE);
    ASSERT(c_generation_expression_type(&a, function) == C_VALUE_UNKNOWN);
    ASSERT(c_generation_parameter_type(&a, 1) == C_VALUE_UNKNOWN);
    ASSERT(c_generation_parameter_type(NULL, 0) == C_VALUE_UNKNOWN);
    ASSERT(c_generation_return_type(NULL) == C_VALUE_UNKNOWN);
    ASSERT(c_generation_expression_type(NULL, expression) == C_VALUE_UNKNOWN);
    fail_c_generation(&a, expression, C_GENERATION_UNSUPPORTED);
    fail_c_generation(&a, function, C_GENERATION_NOT_PROVEN);
    ASSERT(a.failed_node == expression && a.status == C_GENERATION_UNSUPPORTED);
    ASSERT(b.status == C_GENERATION_OK);
    destroy_arena(arena);
    return true;
}

/** @brief Test emitter that fails after producing partial source. */
static bool partial_function(const node_t *node,
                             c_generation_context_t *context,
                             source_builder_t *builder,
                             size_t indent) {
    add_source(builder, indent, L"incomplete_%zu", context->summary->parameter_count);
    return fail_c_generation(context, get_node_child(node, 1), C_GENERATION_UNSUPPORTED);
}

/** @brief Test emitter exercising context transport and successful ownership transfer. */
static bool complete_function(const node_t *node,
                              c_generation_context_t *context,
                              source_builder_t *builder,
                              size_t indent) {
    if (!context->bindings || !context->callees
        || context->bindings->type != c_generation_parameter_type(context, 0)
        || context->callees->summary != context->summary)
        return false;
    string_builder_t line;
    init_string_builder(&line, 0);
    append_string(&line, L"int64_t ");
    append_substring(&line, context->function_name.data, context->function_name.length);
    add_formatted_source(builder, indent, append_string(&line, L"(int64_t n) { return n; }"));
    return true;
}

/** @brief Simulates a successful emitter that forgot to write a function. */
static bool empty_function(const node_t *node,
                           c_generation_context_t *context,
                           source_builder_t *builder,
                           size_t indent) {
    return true;
}

bool test_c_generation_transaction(void) {
    arena_t *arena = create_arena(32);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(
        &memory,
        STATIC_STRING(L"const f=func(n){if(n<1)return 0;return n;};f(1);"));
    ASSERT(root);
    options_t *options = create_options();
    analysis_collector_t *collector = create_analysis_collector(arena);
    ASSERT(!analyze(root, &memory, options, collector));
    analysis_event_query_t query = {.kind = ANALYSIS_FUNCTION_SUMMARY};
    const analysis_event_t *event = find_analysis_event(collector, NULL, &query);
    ASSERT(event && event->function_summary->c_support == FUNCTION_C_SUPPORTED);
    function_summary_t summary = *event->function_summary;
    node_t *function = (node_t *)summary.function;
    string_view_t name = {L"goat_f_integer", 14};
    c_generation_result_t result = generate_c_function(&summary, name, NULL, NULL);
    ASSERT(result.status == C_GENERATION_UNSUPPORTED && !result.source.data);
    ASSERT(summary.c_support == FUNCTION_C_SUPPORTED);
    ASSERT(summary.c_blockers == 0);
    node_vtbl_t overridden = *function->vtbl;
    node_vtbl_t *original = function->vtbl;
    function->vtbl = &overridden;
    overridden.generate_indented_c_code = partial_function;
    result = generate_c_function(&summary, name, NULL, NULL);
    ASSERT(result.status == C_GENERATION_UNSUPPORTED && !result.source.data);
    ASSERT(result.failed_node == get_node_child(function, 1));
    overridden.generate_indented_c_code = empty_function;
    result = generate_c_function(&summary, name, NULL, NULL);
    ASSERT(result.status == C_GENERATION_UNSUPPORTED && !result.source.data);
    overridden.generate_indented_c_code = complete_function;
    result = generate_c_function(&summary, name, NULL, NULL);
    ASSERT(result.status == C_GENERATION_UNSUPPORTED && !result.source.data);
    c_generation_binding_t binding = {.declaration = function,
                                      .name = {L"n", 1},
                                      .type = C_VALUE_INT64};
    c_generation_callee_t callee = {.summary = &summary, .name = name};
    result = generate_c_function(&summary, name, &binding, &callee);
    ASSERT(result.status == C_GENERATION_OK && !result.failed_node);
    ASSERT(result.source.data && wcsstr(result.source.data, L"goat_f_integer"));
    FREE_STRING(result.source);
    ASSERT(generate_c_function(&summary, (string_view_t){0}, NULL, NULL).status
           == C_GENERATION_INVALID_REQUEST);
    const lattice_element_t *unknown_parameter = make_top_element();
    const lattice_element_t **parameters = summary.parameter_types;
    summary.parameter_types = &unknown_parameter;
    ASSERT(generate_c_function(&summary, name, NULL, NULL).status == C_GENERATION_NOT_PROVEN);
    summary.parameter_types = parameters;
    summary.c_support = FUNCTION_C_UNKNOWN;
    result = generate_c_function(&summary, name, &binding, &callee);
    ASSERT(result.status == C_GENERATION_NOT_PROVEN && !result.source.data);
    summary.c_support = FUNCTION_C_SUPPORTED;
    summary.status = FUNCTION_INCONCLUSIVE;
    ASSERT(generate_c_function(&summary, name, NULL, NULL).status == C_GENERATION_NOT_PROVEN);
    summary.status = FUNCTION_ANALYZED;
    summary.effects = FUNCTION_EFFECT_OUTPUT;
    ASSERT(generate_c_function(&summary, name, NULL, NULL).status == C_GENERATION_NOT_PROVEN);
    summary.effects = FUNCTION_EFFECT_NONE;
    summary.return_type = make_top_element();
    ASSERT(generate_c_function(&summary, name, NULL, NULL).status == C_GENERATION_NOT_PROVEN);
    ASSERT(generate_c_function(NULL, name, NULL, NULL).status == C_GENERATION_INVALID_REQUEST);
    summary.function = get_node_child(function, 1);
    ASSERT(generate_c_function(&summary, name, NULL, NULL).status == C_GENERATION_INVALID_REQUEST);
    function->vtbl = original;
    destroy_options(options);
    destroy_arena(arena);
    return true;
}

/** @brief Test expression emitter with explicitly sequenced preparatory statements. */
static c_generated_expression_t sequenced_expression(const node_t *node,
                                                     c_generation_context_t *context) {
    source_builder_t *prelude = create_source_builder();
    add_static_source(prelude, 0, L"int64_t t0 = first();");
    add_static_source(prelude, 0, L"int64_t t1 = second();");
    return (c_generated_expression_t){.success = true,
                                      .type = c_generation_expression_type(context, node),
                                      .value = format_string(L"%s", L"t0 + t1"),
                                      .prelude = prelude};
}

/** @brief Fails after allocating expression output; dispatch must discard it. */
static c_generated_expression_t failed_expression(const node_t *node,
                                                  c_generation_context_t *context) {
    c_generated_expression_t result = sequenced_expression(node, context);
    result.success = false;
    return result;
}

bool test_c_generation_expression(void) {
    arena_t *arena = create_arena(32);
    node_t *node = create_integer_node(arena, 1);
    node_vtbl_t overridden = *node->vtbl;
    overridden.generate_c_code = sequenced_expression;
    node->vtbl = &overridden;
    c_expression_proof_t proof = {.node = node, .type = C_VALUE_INT64};
    function_summary_t summary = {.c_expressions = &proof};
    c_generation_context_t context = {.summary = &summary};
    c_generated_expression_t expression = generate_c_code_from_node(node, &context);
    ASSERT(expression.success && expression.type == C_VALUE_INT64);
    ASSERT(!wcscmp(expression.value.data, L"t0 + t1"));
    ASSERT(expression.prelude->count == 2);
    ASSERT(wcsstr(expression.prelude->lines[0].text.data, L"first()"));
    ASSERT(wcsstr(expression.prelude->lines[1].text.data, L"second()"));
    destroy_c_expression(&expression);
    ASSERT(!expression.value.data && !expression.prelude && !expression.success);
    destroy_c_expression(&expression);
    ASSERT(!generate_c_code_from_node(node, NULL).success);
    overridden.generate_c_code = failed_expression;
    expression = generate_c_code_from_node(node, &context);
    ASSERT(!expression.success && !expression.value.data && !expression.prelude);
    ASSERT(context.status == C_GENERATION_UNSUPPORTED && context.failed_node == node);
    fail_c_generation(&context, node, C_GENERATION_UNSUPPORTED);
    ASSERT(!generate_c_code_from_node(node, &context).success);
    destroy_arena(arena);
    return true;
}
