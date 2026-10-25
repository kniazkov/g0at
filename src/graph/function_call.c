/**
 * @file function_call.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of function call expressions.
 */

#include "analysis/function_call.h"

#include "analysis/abstract_state.h"
#include "analysis/c_body.h"
#include "analysis/function_call_graph.h"
#include "analysis/function_effects.h"
#include "analysis/function_return.h"
#include "analysis/lattice.h"
#include "analysis/module_interface.h"
#include "analysis/native_builtin.h"
#include "analysis/reachability.h"
#include "codegen/c_call.h"
#include "codegen/c_string_methods.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/source_builder.h"
#include "common_methods.h"
#include "expression.h"
#include "index_access.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "member_access.h"
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
    bool module_noncallable;
    bool module_unknown;
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
static const gchar_t *get_child_tag(const node_t *node, size_t index) {
    if (index == 0) {
        return U"object";
    }
    return NULL;
}

/** @brief Implements @ref node_vtbl_t::calculate. */
static const lattice_element_t *calculate(node_t *node, abstract_state_t *state, arena_t *arena) {
    const function_call_t *expr = (const function_call_t *)node;
    const lattice_element_t **args =
        alloc_from_arena(arena, (expr->args_count + 1) * sizeof(*args));
    for (size_t i = 0; i < expr->args_count && state->control_flow == FLOW_NORMAL; i++)
        args[i] = calculate_expression(expr->args[i], state, arena);
    if (state->control_flow != FLOW_NORMAL)
        return make_bottom_element();
    const lattice_element_t *function;
    if (expr->func_object->base.vtbl->type == NODE_MEMBER_ACCESS) {
        const member_access_t *member = (const member_access_t *)expr->func_object;
        const lattice_element_t *receiver = calculate_expression(member->object, state, arena);
        if (state->control_flow != FLOW_NORMAL)
            return make_bottom_element();
        const builtin_function_t *method = find_native_string_method(member->name);
        if (method
            && (receiver->type == LATTICE_STRING || receiver->type == LATTICE_STRING_CONSTANT)) {
            for (size_t i = expr->args_count; i > 0; i--)
                args[i] = args[i - 1];
            args[0] = receiver;
            return method->interpret(state, args, expr->args_count + 1);
        }
        function = module_interface_member_at(receiver, member->name, arena, node);
    } else {
        function = calculate_expression(expr->func_object, state, arena);
    }
    if (state->control_flow != FLOW_NORMAL)
        return make_bottom_element();
    if (state->module_constants && !state->call_frame) {
        bool top_level = true;
        for (const node_t *owner = node->parent; owner; owner = owner->parent)
            top_level &= owner->vtbl->type != NODE_FUNCTION_OBJECT;
        if (top_level) {
            bool scalar = function->type == LATTICE_NULL || is_numeric_lattice_element(function)
                          || is_string_lattice_element(function)
                          || is_character_lattice_element(function)
                          || is_boolean_lattice_element(function);
            ((function_call_t *)node)->module_noncallable |= scalar;
            ((function_call_t *)node)->module_unknown |= !scalar;
        }
    }
    if (state->c_expressions && state->c_expressions->graph)
        return interpret_c_call(node, args, expr->args_count, state);
    const builtin_function_t *builtin = resolve_native_builtin(get_node_child(node, 0));
    if (builtin && expr->args_count >= builtin->min_args)
        return builtin->interpret(state, args, expr->args_count);
    if (function->type == LATTICE_KNOWN_FUNCTION
        && ((const known_function_element_t *)function)->imported) {
        observe_function_call(state->call_graph_node, node, function, args, expr->args_count);
        return interpret_function_call(function, args, expr->args_count, state);
    }
    if (state->recursive_group)
        return interpret_recursive_call(node, function, args, expr->args_count, state);
    observe_function_call(state->call_graph_node, node, function, args, expr->args_count);
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

    append_char(&builder, U'(');
    for (size_t index = 0; index < expr->args_count; index++) {
        if (index > 0) {
            append_static_string(&builder, U", ");
        }
        expression_t *arg = expr->args[index];
        string_value_t arg_as_string = generate_goat_code_from_expression(arg);
        append_string_value(&builder, arg_as_string);
        FREE_STRING(arg_as_string);
    }

    return append_char(&builder, U')');
}

/** @brief Implements @ref node_vtbl_t::generate_indented_goat_code. */
static void
generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    const function_call_t *expr = (const function_call_t *)node;
    generate_indented_goat_code_from_expression(expr->func_object, builder, indent);
    append_static_source(builder, U"(");
    for (size_t index = 0; index < expr->args_count; index++) {
        if (index > 0) {
            append_static_source(builder, U", ");
        }
        expression_t *arg = expr->args[index];
        generate_indented_goat_code_from_expression(arg, builder, indent);
    }
    append_static_source(builder, U")");
}

/** @brief Implements @ref node_vtbl_t::generate_bytecode. */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    const function_call_t *expr = (const function_call_t *)node;
    assert(expr->args_count < UINT16_MAX);
    instr_index_t first = code->size;
    for (size_t index = 0; index < expr->args_count; index++)
        generate_bytecode_from_expression(expr->args[index], code, data);
    if (expr->func_object->base.vtbl->type == NODE_MEMBER_ACCESS) {
        /* Method call `object.member(...)`: keep the receiver on the stack below the function so
         * CALLM can pass it as the method context. */
        const member_access_t *member = (const member_access_t *)expr->func_object;
        generate_bytecode_from_expression(member->object, code, data);
        add_instruction(code, (instruction_t){.opcode = DUP});
        uint32_t index = add_string_to_data_segment_ex(data, member->name);
        add_instruction(code, (instruction_t){.opcode = GETPROP, .arg1 = index});
        add_instruction(code, (instruction_t){.opcode = CALLM, .arg0 = (uint16_t)expr->args_count});
    } else if (expr->func_object->base.vtbl->type == NODE_INDEX_ACCESS) {
        generate_index_read(&expr->func_object->base, code, data, true);
        add_instruction(code, (instruction_t){.opcode = CALLM, .arg0 = (uint16_t)expr->args_count});
    } else {
        generate_bytecode_from_expression(expr->func_object, code, data);
        add_instruction(code, (instruction_t){.opcode = CALL, .arg0 = (uint16_t)expr->args_count});
    }
    return first;
}

/** @brief Implements node_vtbl_t::analyze_reachability. */
static const lattice_element_t *
analyze_reachability(node_t *node, abstract_state_t **state, analysis_collector_t *collector) {
    size_t count = get_node_child_count(node) - 1;
    const lattice_element_t **args = alloc_from_arena((*state)->arena, (count + 1) * sizeof(*args));
    for (size_t i = 0; i < count; i++)
        args[i] = visit_reachable_node(get_node_child(node, i + 1), state, collector);
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
    if (callee->type == LATTICE_KNOWN_FUNCTION
        && ((const known_function_element_t *)callee)->imported)
        return interpret_imported_call(((const known_function_element_t *)callee)->imported,
                                       args,
                                       count,
                                       *state,
                                       NULL);
    forget_abstract_values(*state);
    return make_top_element();
}

/** @brief Refines the resolved callee proof with effects of argument/callee evaluation. */
static bool is_pure(const node_t *node) {
    return node_has_flag(node, NODE_FLAG_PURE) && children_are_pure(node);
}

/** @brief Implements node_vtbl_t::collect_direct_effects. */
static void
collect_direct_effects(const node_t *node, function_summary_t *summary, arena_t *arena) {
    record_function_effect_call(summary, node, arena);
    collect_child_effects(node, summary, arena);
}

/** @brief A static numeric call needs a supported target and all evaluated arguments. */
static bool can_generate_c_code(const node_t *node,
                                const lattice_element_t *value,
                                const c_expression_context_t *context) {
    return c_call_supported(node, context);
}

/** @brief Implements node_vtbl_t::replace_child for the callee or an argument. */
static bool replace_child(node_t *node, node_t *old_child, node_t *new_child) {
    function_call_t *call = (function_call_t *)node;
    if (!is_expression(new_child->vtbl->type))
        return false;
    if ((node_t *)call->func_object == old_child) {
        call->func_object = (expression_t *)new_child;
        return true;
    }
    for (size_t i = 0; i < call->args_count; i++) {
        if ((node_t *)call->args[i] == old_child) {
            call->args[i] = (expression_t *)new_child;
            return true;
        }
    }
    return false;
}

/** @brief Virtual table for function call expressions. */
static node_vtbl_t function_call_vtbl = {
    .type = NODE_FUNCTION_CALL,
    .analyze_reachability = analyze_reachability,
    .is_pure = is_pure,
    .simplify = no_simplification,
    .collect_direct_effects = collect_direct_effects,
    .type_name = U"function call",
    .get_data = no_data,
    .get_property_count = no_properties,
    .get_property = no_property,
    .get_child_count = get_child_count,
    .get_child = get_child,
    .get_child_tag = get_child_tag,
    .insert_child_before = no_child_insertion,
    .replace_child = replace_child,
    .get_related_count = no_related_nodes,
    .get_related = no_related_node,
    .get_relation_type = no_relation_type,
    .calculate = calculate,
    .execute = execute_nothing,
    .generate_goat_code = generate_goat_code,
    .generate_indented_goat_code = generate_indented_goat_code,
    .generate_bytecode = generate_bytecode,
    .can_generate_c_code = can_generate_c_code,
    .generate_c_code = c_call,
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

bool module_call_is_noncallable(const node_t *node) {
    return node && node->vtbl->type == NODE_FUNCTION_CALL
           && ((const function_call_t *)node)->module_noncallable
           && !((const function_call_t *)node)->module_unknown;
}
