/**
 * @file function_call.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of function call expressions.
 */

#include "analysis/function_call.h"

#include "analysis/abstract_state.h"
#include "analysis/lattice.h"
#include "analysis/reachability.h"
#include "codegen/code_builder.h"
#include "codegen/source_builder.h"
#include "common_methods.h"
#include "expression.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "model/builtin_function.h"

#include <assert.h>

/** @brief A function call expression node. */
typedef struct {
    /** @brief Base expression structure from which function_call_t inherits. */
    expression_t base;

    /** @brief The function object being called. */
    expression_t *func_object;

    /** @brief The arguments passed to the function. */
    expression_t **args;

    /** @brief The number of arguments in the function call. */
    size_t args_count;
} function_call_t;

/** @brief Implements @ref node_vtbl_t::get_child_count. */
static size_t get_child_count(const node_t *node) {
    const function_call_t *expr = (const function_call_t *)node;
    return 1 + expr->args_count;
}

/** @brief Implements @ref node_vtbl_t::get_child. */
static node_t *get_child(const node_t *node, size_t index) {
    const function_call_t *expr = (const function_call_t *)node;
    if (index < 0) {
        return NULL;
    }
    if (index == 0) {
        return &expr->func_object->base;
    }
    if (index > expr->args_count) {
        return NULL;
    }
    return &expr->args[index - 1]->base;
}

/** @brief Implements @ref node_vtbl_t::get_child_tag. */
static const wchar_t *get_child_tag(const node_t *node, size_t index) {
    if (index == 0) {
        return L"object";
    }
    return NULL;
}

/** @brief Implements @ref node_vtbl_t::calculate. */
static const lattice_element_t *calculate(node_t *node, abstract_state_t *state, arena_t *arena) {
    const function_call_t *expr = (const function_call_t *)node;
    const lattice_element_t **args =
        alloc_from_arena(arena, (expr->args_count + 1) * sizeof(*args));
    for (size_t i = expr->args_count; i > 0 && state->control_flow == FLOW_NORMAL; i--)
        args[i - 1] = calculate_expression(expr->args[i - 1], state, arena);
    if (state->control_flow != FLOW_NORMAL)
        return make_bottom_element();
    const lattice_element_t *function = calculate_expression(expr->func_object, state, arena);
    if (state->control_flow != FLOW_NORMAL)
        return make_bottom_element();
    return interpret_function_call(function, args, expr->args_count, state);
}

/** @brief Implements @ref node_vtbl_t::generate_goat_code. */
static string_value_t generate_goat_code(const node_t *node) {
    const function_call_t *expr = (const function_call_t *)node;
    string_builder_t builder;
    init_string_builder(&builder, 0);

    string_value_t func_object_as_string = generate_goat_code_from_expression(expr->func_object);
    append_string_value(&builder, func_object_as_string);
    FREE_STRING(func_object_as_string);

    append_char(&builder, L'(');
    for (size_t index = 0; index < expr->args_count; index++) {
        if (index > 0) {
            append_static_string(&builder, L", ");
        }
        expression_t *arg = expr->args[index];
        string_value_t arg_as_string = generate_goat_code_from_expression(arg);
        append_string_value(&builder, arg_as_string);
        FREE_STRING(arg_as_string);
    }

    return append_char(&builder, L')');
}

/** @brief Implements @ref node_vtbl_t::generate_indented_goat_code. */
static void
generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    const function_call_t *expr = (const function_call_t *)node;
    generate_indented_goat_code_from_expression(expr->func_object, builder, indent);
    append_static_source(builder, L"(");
    for (size_t index = 0; index < expr->args_count; index++) {
        if (index > 0) {
            append_static_source(builder, L", ");
        }
        expression_t *arg = expr->args[index];
        generate_indented_goat_code_from_expression(arg, builder, indent);
    }
    append_static_source(builder, L")");
}

/** @brief Implements @ref node_vtbl_t::generate_bytecode. */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    const function_call_t *expr = (const function_call_t *)node;
    assert(expr->args_count < UINT16_MAX);
    instr_index_t first = code->size;
    if (expr->args_count > 0) {
        size_t index = expr->args_count;
        do {
            index--;
            expression_t *arg = expr->args[index];
            generate_bytecode_from_expression(arg, code, data);
        } while (index > 0);
    }
    generate_bytecode_from_expression(expr->func_object, code, data);
    add_instruction(code, (instruction_t){.opcode = CALL, .arg0 = (uint16_t)expr->args_count});
    return first;
}

/** @brief Implements node_vtbl_t::analyze_reachability. */
static const lattice_element_t *
analyze_reachability(node_t *node, abstract_state_t **state, analysis_collector_t *collector) {
    size_t count = get_node_child_count(node) - 1;
    const lattice_element_t **args = alloc_from_arena((*state)->arena, (count + 1) * sizeof(*args));
    for (size_t i = count; i > 0; i--)
        args[i - 1] = visit_reachable_node(get_node_child(node, i), state, collector);
    const lattice_element_t *callee =
        visit_reachable_node(get_node_child(node, 0), state, collector);
    if ((*state)->control_flow != FLOW_NORMAL)
        return make_bottom_element();
    if (callee->type == LATTICE_KNOWN_FUNCTION
        && ((const known_function_element_t *)callee)->builtin) {
        const builtin_function_t *builtin = ((const known_function_element_t *)callee)->builtin;
        if (builtin->effects == BUILTIN_EFFECT_NONE)
            node->flags |= NODE_FLAG_PURE;
        return interpret_function_call(callee, args, count, *state);
    }
    forget_abstract_values(*state);
    return make_top_element();
}

/** @brief Refines the resolved callee proof with effects of argument/callee evaluation. */
static bool is_pure(const node_t *node) {
    return node_has_flag(node, NODE_FLAG_PURE) && children_are_pure(node);
}

/** @brief Virtual table for function call expressions. */
static node_vtbl_t function_call_vtbl = {
    .type = NODE_FUNCTION_CALL,
    .analyze_reachability = analyze_reachability,
    .is_pure = is_pure,
    .type_name = L"function call",
    .get_data = no_data,
    .get_property_count = no_properties,
    .get_property = no_property,
    .get_child_count = get_child_count,
    .get_child = get_child,
    .get_child_tag = get_child_tag,
    .insert_child_before = no_child_insertion,
    .replace_child = no_child_replacement,
    .get_related_count = no_related_nodes,
    .get_related = no_related_node,
    .get_relation_type = no_relation_type,
    .calculate = calculate,
    .execute = execute_nothing,
    .generate_goat_code = generate_goat_code,
    .generate_indented_goat_code = generate_indented_goat_code,
    .generate_bytecode = generate_bytecode,
    .can_generate_c_code = cannot_generate_c_code,
    .generate_c_code = no_c_code,
    .generate_indented_c_code = no_indented_c_code,
    .generate_bytecode_assign = no_bytecode_assignment,
    .generate_bytecode_deferred = no_deferred_bytecode,
};

node_t *create_function_call_node_without_args(arena_t *arena, expression_t *func_object) {
    function_call_t *expr =
        (function_call_t *)alloc_zeroed_from_arena(arena, sizeof(function_call_t));
    expr->base.base.vtbl = &function_call_vtbl;
    expr->func_object = func_object;
    return &expr->base.base;
}

void set_function_call_arguments(node_t *node,
                                 arena_t *arena,
                                 expression_t **args,
                                 size_t args_count) {
    assert(node->vtbl->type == NODE_FUNCTION_CALL);
    function_call_t *expr = (function_call_t *)node;
    assert(expr->args == NULL);
    expr->args = (expression_t **)alloc_from_arena(arena, args_count * sizeof(expression_t *));
    memcpy(expr->args, args, args_count * sizeof(expression_t *));
    expr->args_count = args_count;
}
