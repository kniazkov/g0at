/** @file test_c_replacement.c
 * @copyright 2026 Ivan Kniazkov
 * @brief C replacement proofs, conservative fallbacks and specialization isolation.
 */
#include "test_c_replacement.h"

#include "analysis/analysis.h"
#include "analysis_test_support.h"
#include "cli/options.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "graph/binary_operation.h"
#include "graph/common_methods.h"
#include "graph/replacement.h"
#include "lib/allocate.h"
#include "test_macro.h"

#include <math.h>
#include <stdio.h>
#include <wchar.h>

/** @brief Test emitter distinguishes original arithmetic from a replacement literal. */
static c_generated_expression_t emit_expression(const node_t *node,
                                                c_generation_context_t *context) {
    c_value_type_t type = c_generation_expression_type(context, node);
    if (type != C_VALUE_INT64 && type != C_VALUE_DOUBLE) {
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    source_builder_t *prelude = create_source_builder();
    add_static_source(prelude, 0, L"/* original evaluation */");
    return (c_generated_expression_t){.success = true,
                                      .type = type,
                                      .value = node->vtbl->type == NODE_ADDITION
                                                   ? (type == C_VALUE_INT64
                                                          ? STATIC_STRING(L"integer original")
                                                          : STATIC_STRING(L"real original"))
                                                   : STATIC_STRING(L"replacement literal"),
                                      .prelude = prelude};
}

bool test_c_replacement_expression(void) {
    arena_t *arena = create_arena(32);
    expression_t *original = create_addition_node(arena,
                                                  (expression_t *)create_integer_node(arena, 1),
                                                  (expression_t *)create_integer_node(arena, 2));
    expression_t *literal = (expression_t *)create_integer_node(arena, 11);
    expression_t *inner = create_expression_replacement(arena, original, literal);
    node_t *outer =
        (node_t *)create_expression_replacement(arena,
                                                inner,
                                                (expression_t *)create_integer_node(arena, 22));
    node_vtbl_t emitter = *original->base.vtbl;
    emitter.generate_c_code = emit_expression;
    original->base.vtbl = &emitter;
    node_vtbl_t literal_emitter = *literal->base.vtbl;
    literal_emitter.generate_c_code = emit_expression;
    literal->base.vtbl = &literal_emitter;
    c_expression_proof_t misleading = {.node = (node_t *)literal, .type = C_VALUE_DOUBLE};
    c_expression_proof_t proof = {.next = &misleading,
                                  .node = (node_t *)original,
                                  .type = C_VALUE_INT64};
    function_summary_t summary = {.c_expressions = &proof};
    c_generation_context_t context = {.summary = &summary};
    original->base.flags = NODE_FLAG_UNREACHABLE;
    outer->flags = NODE_FLAG_C_COMPATIBLE;
    node_t *parent = original->base.parent;
    ASSERT(replacement_original(outer) == (node_t *)original);
    ASSERT(replacement_result(outer) == get_node_child(outer, 1));
    ASSERT(c_generation_expression_type(&context, outer) == C_VALUE_INT64);
    c_generated_expression_t result = generate_c_code_from_node(outer, &context);
    ASSERT(result.success && result.type == C_VALUE_INT64);
    ASSERT(!wcscmp(result.value.data, L"integer original"));
    ASSERT(result.prelude && result.prelude->count == 1);
    destroy_c_expression(&result);
    proof.type = C_VALUE_DOUBLE;
    result = generate_c_code_from_node(outer, &context);
    ASSERT(result.success && result.type == C_VALUE_DOUBLE);
    destroy_c_expression(&result);
    summary.c_expressions = &misleading;
    ASSERT(c_generation_expression_type(&context, outer) == C_VALUE_UNKNOWN);
    result = generate_c_code_from_node(outer, &context);
    ASSERT(!result.success && !result.prelude && !result.value.data);
    ASSERT(context.status == C_GENERATION_NOT_PROVEN && context.failed_node == (node_t *)original);
    ASSERT(original->base.parent == parent && original->base.flags == NODE_FLAG_UNREACHABLE);
    ASSERT(outer->flags == NODE_FLAG_C_COMPATIBLE && proof.node == (node_t *)original);
    code_builder_t *code = create_code_builder();
    data_builder_t *data = create_data_builder();
    generate_bytecode_from_node(outer, code, data);
    ASSERT(code->size == 1 && code->instructions[0].opcode == ILOAD32
           && code->instructions[0].arg1 == 22);
    destroy_data_builder(data);
    destroy_code_builder(code);
    destroy_arena(arena);
    return true;
}

/** @brief Test emitter retains both arms of an original conditional. */
static bool emit_statement(const node_t *node,
                           c_generation_context_t *context,
                           source_builder_t *builder,
                           size_t indent) {
    if (node->vtbl->type != NODE_IF_ELSE || !get_node_child(node, 2))
        return fail_c_generation(context, node, C_GENERATION_UNSUPPORTED);
    add_static_source(builder, indent, L"if (condition) { left(); } else { right(); }");
    return true;
}

bool test_c_replacement_statement(void) {
    arena_t *arena = create_arena(32);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root =
        parse_analysis_test_program(&memory,
                                    STATIC_STRING(L"if (true) {var x=1;} else {var x=2;}"));
    ASSERT(root);
    options_t *options = create_options();
    ASSERT(!analyze(root, &memory, options, NULL));
    node_t *wrapper = get_node_child(root, 0);
    ASSERT(wrapper->vtbl->type == NODE_STATEMENT_REPLACEMENT);
    node_t *original = get_node_child(wrapper, 0);
    node_t *selected = get_node_child(wrapper, 1);
    node_t *selected_parent = selected->parent;
    node_t *other = get_node_child(original, 2);
    uint32_t other_flags = other->flags;
    node_vtbl_t emitter = *original->vtbl;
    emitter.generate_indented_c_code = emit_statement;
    original->vtbl = &emitter;
    statement_t *nested =
        create_statement_replacement(arena,
                                     (statement_t *)wrapper,
                                     create_statement_expression_node(arena, NULL));
    function_summary_t summary = {0};
    c_generation_context_t context = {.summary = &summary};
    source_builder_t *source = create_source_builder();
    ASSERT(generate_indented_c_code_from_node((node_t *)nested, &context, source, 3));
    ASSERT(source->count == 1 && source->lines[0].indent == 3);
    ASSERT(wcsstr(source->lines[0].text.data, L"else { right(); }"));
    ASSERT(selected->parent == selected_parent && other->flags == other_flags);
    emitter.generate_indented_c_code = no_indented_c_code;
    ASSERT(!generate_indented_c_code_from_node((node_t *)nested, &context, source, 3));
    ASSERT(context.failed_node == original && context.status == C_GENERATION_UNSUPPORTED);
    ASSERT(source->count == 1);
    code_builder_t *code = create_code_builder();
    data_builder_t *data = create_data_builder();
    generate_bytecode_from_node((node_t *)nested, code, data);
    ASSERT(code->size == 0);
    destroy_data_builder(data);
    destroy_code_builder(code);
    destroy_source_builder(source);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}

/** @brief Finds a node including archived branches; test trees have no cycles. */
static node_t *find_type(node_t *node, node_type_t type) {
    if (node->vtbl->type == type)
        return node;
    for (size_t i = 0; i < get_node_child_count(node); i++) {
        node_t *found = find_type(get_node_child(node, i), type);
        if (found)
            return found;
    }
    return NULL;
}

bool test_c_replacement_analysis(void) {
    arena_t *arena = create_arena(32);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(
        &memory,
        STATIC_STRING(L"const f=func(n){return n+(2+3);}; f(10); f(20); f(1.5);"));
    ASSERT(root);
    options_t *options = create_options();
    ASSERT(!analyze(root, &memory, options, NULL));
    node_t *function = find_type(root, NODE_FUNCTION_OBJECT);
    ASSERT(function);
    function_summary_set_t *summaries = get_function_summaries(function);
    ASSERT(summaries && summaries->head && summaries->head->next);
    node_t *original = find_type(get_node_child(function, 1), NODE_ADDITION);
    ASSERT(original);
    node_t *closed_replacement = find_type(original, NODE_EXPRESSION_REPLACEMENT);
    ASSERT(closed_replacement);
    node_t *parent = original->parent;
    /* Simulate a value-specific rewrite: it must never become generic native code. */
    node_t *wrapper =
        (node_t *)create_expression_replacement(arena,
                                                (expression_t *)original,
                                                (expression_t *)create_integer_node(arena, 15));
    ASSERT(replace_child_node(parent, original, wrapper));
    node_vtbl_t emitter = *original->vtbl;
    emitter.generate_c_code = emit_expression;
    original->vtbl = &emitter;
    size_t integers = 0, reals = 0;
    for (function_summary_t *summary = summaries->head; summary; summary = summary->next) {
        ASSERT(summary->c_support == FUNCTION_C_SUPPORTED);
        c_generation_context_t context = {.summary = summary};
        ASSERT(c_generation_expression_type(&context, closed_replacement) == C_VALUE_INT64);
        c_generated_expression_t result = generate_c_code_from_node(wrapper, &context);
        ASSERT(result.success && !wcsstr(result.value.data, L"replacement literal"));
        integers += result.type == C_VALUE_INT64;
        reals += result.type == C_VALUE_DOUBLE;
        destroy_c_expression(&result);
    }
    ASSERT(integers == 1 && reals == 1);
    ASSERT(get_node_child(wrapper, 0) == original);
    ASSERT(replacement_result(wrapper)->vtbl->type == NODE_INTEGER);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}

bool test_c_replacement_proofs(void) {
    arena_t *arena = create_arena(32);
    abstract_state_t *state = create_abstract_state(arena);
    c_expression_context_t proofs = {.arena = arena};
    state->c_expressions = &proofs;
    expression_t *sum = create_addition_node(arena,
                                             (expression_t *)create_integer_node(arena, 2),
                                             (expression_t *)create_integer_node(arena, 3));
    const lattice_element_t *value = calculate_expression(sum, state, arena);
    function_summary_t summary = {.c_expressions = proofs.head};
    c_generation_context_t context = {.summary = &summary};
    node_t *five = create_integer_node(arena, 5);
    node_t *wrapper = (node_t *)create_expression_replacement(arena, sum, (expression_t *)five);
    ASSERT(c_generation_replacement(&context, wrapper) == five);
    c_generated_expression_t code = generate_c_code_from_node(wrapper, &context);
    ASSERT(code.success && code.type == C_VALUE_INT64 && !code.prelude);
    ASSERT(!wcscmp(code.value.data, L"INT64_C(5)"));
    ASSERT(!context.replacement_proof);
    destroy_c_expression(&code);
    node_t *wrong_type = (node_t *)create_expression_replacement(
        arena,
        sum,
        (expression_t *)create_real_number_node(arena, 5.0));
    ASSERT(c_generation_replacement(&context, wrong_type) == (node_t *)sum);
    node_t *wrong_value =
        (node_t *)create_expression_replacement(arena,
                                                sum,
                                                (expression_t *)create_integer_node(arena, 6));
    ASSERT(c_generation_replacement(&context, wrong_value) == (node_t *)sum);
    node_t *nested = (node_t *)create_expression_replacement(arena,
                                                             (expression_t *)wrong_value,
                                                             (expression_t *)five);
    ASSERT(c_generation_replacement(&context, nested) == five);
    nested = (node_t *)create_expression_replacement(arena,
                                                     (expression_t *)wrapper,
                                                     (expression_t *)create_integer_node(arena, 6));
    ASSERT(c_generation_replacement(&context, nested) == (node_t *)sum);
    /* Repeated visits can only lose a constant, even if the representation stays fixed. */
    record_c_expression(&proofs, (node_t *)sum, make_integer_constant_element(arena, 6));
    ASSERT(!c_expression_constant(&proofs, (node_t *)sum));
    record_c_expression(&proofs, (node_t *)sum, value);
    ASSERT(!c_expression_constant(&proofs, (node_t *)sum));
    ASSERT(c_generation_replacement(&context, wrapper) == (node_t *)sum);
    destroy_abstract_state(state);
    destroy_arena(arena);
    return true;
}

bool test_c_replacement_real_edges(void) {
    arena_t *arena = create_arena(32);
    const double values[] = {-0.0, 0.0, INFINITY, -INFINITY, NAN, 0x1p-1074, 0x1p63};
    for (size_t i = 0; i < sizeof(values) / sizeof(*values); i++) {
        abstract_state_t *state = create_abstract_state(arena);
        c_expression_context_t proofs = {.arena = arena};
        state->c_expressions = &proofs;
        expression_t *original = (expression_t *)create_real_number_node(arena, values[i]);
        calculate_expression(original, state, arena);
        function_summary_t summary = {.c_expressions = proofs.head};
        c_generation_context_t context = {.summary = &summary};
        node_t *literal = create_real_number_node(arena, values[i]);
        node_t *wrapper =
            (node_t *)create_expression_replacement(arena, original, (expression_t *)literal);
        ASSERT(c_generation_replacement(&context, wrapper)
               == (isnan(values[i]) ? (node_t *)original : literal));
        if (values[i] == 0.0) {
            node_t *opposite = create_real_number_node(arena, -values[i]);
            wrapper =
                (node_t *)create_expression_replacement(arena, original, (expression_t *)opposite);
            ASSERT(c_generation_replacement(&context, wrapper) == (node_t *)original);
        }
        destroy_abstract_state(state);
    }
    destroy_arena(arena);
    return true;
}

bool test_c_replacement_specializations(void) {
    arena_t *arena = create_arena(32);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(
        &memory,
        STATIC_STRING(L"const f=func(n){if(n<=9223372036854775807){return n+(2+3);}else{return "
                      L"n-1;}};f(1);f(1.5);"));
    options_t *options = create_options();
    ASSERT(root && !analyze(root, &memory, options, NULL));
    node_t *function = find_type(root, NODE_FUNCTION_OBJECT);
    node_t *branch = find_type(function, NODE_IF_ELSE);
    node_t *parent = branch->parent;
    uint32_t flags = branch->flags;
    size_t count = 0;
    for (function_summary_t *s = get_function_summaries(function)->head; s; s = s->next) {
        ASSERT(s->c_support == FUNCTION_C_SUPPORTED);
        c_generation_result_t code =
            generate_c_function(s, (string_view_t){L"goat_test", 9}, NULL, NULL);
        ASSERT(code.status == C_GENERATION_OK);
        bool integer = s->parameter_types[0]->type == LATTICE_INTEGER;
        ASSERT((wcsstr(code.source.data, L"if (") == NULL) == integer);
        ASSERT(wcsstr(code.source.data, L"INT64_C(5)"));
        ASSERT(!wcsstr(code.source.data, L"INT64_C(2)"));
        FREE_STRING(code.source);
        count++;
    }
    ASSERT(count == 2 && branch->parent == parent && branch->flags == flags);
    ASSERT(get_node_child(parent, 0) == branch);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}

bool test_c_replacement_branches(void) {
    arena_t *arena = create_arena(32);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(
        &memory,
        STATIC_STRING(L"const f=func(n){if(2<3){return n;}else{return n/2;}};f(1);"));
    options_t *options = create_options();
    ASSERT(root && !analyze(root, &memory, options, NULL));
    node_t *function = find_type(root, NODE_FUNCTION_OBJECT);
    function_summary_t *s = get_function_summaries(function)->head;
    ASSERT(s->c_support == FUNCTION_C_SUPPORTED);
    node_t *wrapper = find_type(function, NODE_STATEMENT_REPLACEMENT);
    ASSERT(wrapper);
    node_t *original = get_node_child(wrapper, 0);
    node_t *chosen = get_node_child(wrapper, 1);
    c_generation_context_t context = {.summary = s};
    ASSERT(c_generation_replacement(&context, wrapper) == chosen);
    node_t *bad =
        (node_t *)create_statement_replacement(arena,
                                               (statement_t *)original,
                                               (statement_t *)get_node_child(original, 2));
    ASSERT(c_generation_replacement(&context, bad) == original);
    bad = (node_t *)create_statement_replacement(arena,
                                                 (statement_t *)wrapper,
                                                 create_statement_expression_node(arena, NULL));
    ASSERT(c_generation_replacement(&context, bad) == original);
    c_generation_result_t code =
        generate_c_function(s, (string_view_t){L"goat_test", 9}, NULL, NULL);
    ASSERT(code.status == C_GENERATION_OK && !wcsstr(code.source.data, L"if ("));
    ASSERT(wcsstr(code.source.data, L"return g_p0;"));
    FREE_STRING(code.source);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}

bool test_c_replacement_effects(void) {
    arena_t *arena = create_arena(32);
    parser_memory_t memory = {arena, arena, arena, arena};
    const wchar_t *sources[] = {L"const f=func(n){var x=0;return (x=1)+x;};f(1);",
                                L"const f=func(n){if(n<1)return 1;return f(n-1);};f(2);"};
    for (size_t i = 0; i < 2; i++) {
        node_t *root = parse_analysis_test_program(
            &memory,
            (string_value_t){.data = sources[i], .length = wcslen(sources[i])});
        options_t *options = create_options();
        ASSERT(root && !analyze(root, &memory, options, NULL));
        node_t *function = find_type(root, NODE_FUNCTION_OBJECT);
        function_summary_t *s = get_function_summaries(function)->head;
        ASSERT(s && function_summary_is_pure(s));
        node_t *original = find_type(function, i ? NODE_FUNCTION_CALL : NODE_ADDITION);
        ASSERT(original);
        node_t *wrapper = (node_t *)create_expression_replacement(
            arena,
            (expression_t *)original,
            (expression_t *)create_integer_node(arena, i ? 1 : 2));
        c_generation_context_t context = {.summary = s};
        ASSERT(c_generation_replacement(&context, wrapper) == original);
        c_expression_context_t proofs = {.head = s->c_expressions};
        ASSERT(!c_expression_constant(&proofs, original));
        destroy_options(options);
    }
    destroy_arena(arena);
    return true;
}
