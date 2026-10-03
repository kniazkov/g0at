/**
 * @file return.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of the return statement node.
 */

#include "analysis/abstract_state.h"
#include "analysis/c_body.h"
#include "analysis/function_call.h"
#include "analysis/lattice.h"
#include "analysis/reachability.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/source_builder.h"
#include "common_methods.h"
#include "expression.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "statement.h"

/** @brief Implements node_vtbl_t::can_generate_c_code for a complete numeric body. */
static bool can_generate_c_code(const node_t *node,
                                const lattice_element_t *value,
                                const c_expression_context_t *context) {
    return context && context->graph && get_node_child_count(node) == 1
           && c_expression_type(context, get_node_child(node, 0))
                  == classify_c_value_type(context->summary->return_type->type);
}

/** @brief AST node representing a return statement. */
typedef struct {
    /** @brief Base statement structure. */
    statement_t base;

    /**
     * @brief The expression representing the return value (optional).
     *
     * This can be `NULL` for `return;` without a value, or point to an expression that produces the
     * return value (e.g., `return x + 1;`).
     */
    expression_t *value;
} return_t;

/** @brief Implements @ref node_vtbl_t::get_child_count. */
static size_t get_child_count(const node_t *node) {
    const return_t *stmt = (const return_t *)node;
    return stmt->value != NULL ? 1 : 0;
}

/** @brief Implements @ref node_vtbl_t::get_child. */
static node_t *get_child(const node_t *node, size_t index) {
    const return_t *stmt = (const return_t *)node;
    if (index == 0 && stmt->value) {
        return &stmt->value->base;
    }
    return NULL;
}

/** @brief Implements @ref node_vtbl_t::get_child_tag. */
static const wchar_t *get_child_tag(const node_t *node, size_t index) {
    const return_t *stmt = (const return_t *)node;
    if (index == 0 && stmt->value) {
        return L"expression";
    }
    return NULL;
}

/** @brief Implements @ref node_vtbl_t::execute. */
static abstract_state_t *execute(node_t *node, abstract_state_t *state, arena_t *arena) {
    const return_t *stmt = (const return_t *)node;
    const lattice_element_t *value =
        stmt->value ? calculate_expression(stmt->value, state, arena) : make_null_element();
    if (state->control_flow != FLOW_NORMAL)
        return state;
    if (value->type == LATTICE_BOTTOM) {
        state->control_flow = FLOW_UNREACHABLE;
        return state;
    }
    if (state->return_value) {
        *state->return_value =
            lattice_join(arena,
                         *state->return_value ? *state->return_value : make_bottom_element(),
                         value);
    }
    collect_abstract_return(state);
    state->control_flow = FLOW_RETURN;
    return state;
}

/** @brief Implements @ref node_vtbl_t::generate_goat_code. */
static string_value_t generate_goat_code(const node_t *node) {
    const return_t *stmt = (const return_t *)node;
    if (stmt->value) {
        string_builder_t builder;
        string_value_t value_as_string = generate_goat_code_from_expression(stmt->value);
        init_string_builder(&builder, value_as_string.length + 8);
        append_static_string(&builder, L"return ");
        append_string_value(&builder, value_as_string);
        FREE_STRING(value_as_string);
        return append_char(&builder, L';');
    } else {
        return STATIC_STRING(L"return;");
    }
}

/** @brief Implements @ref node_vtbl_t::generate_indented_goat_code. */
static void
generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    const return_t *stmt = (const return_t *)node;
    if (stmt->value) {
        add_static_source(builder, indent, L"return ");
        generate_indented_goat_code_from_expression(stmt->value, builder, indent);
        append_static_source(builder, L";");
    } else {
        add_static_source(builder, indent, L"return;");
    }
}

/** @brief Implements @ref node_vtbl_t::generate_bytecode. */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    const return_t *stmt = (const return_t *)node;
    instr_index_t first;
    if (stmt->value) {
        first = generate_bytecode_from_expression(stmt->value, code, data);
    } else {
        first = add_instruction(code, (instruction_t){.opcode = NIL});
    }
    add_instruction(code, (instruction_t){.opcode = RET});
    return first;
}

/** @brief Implements node_vtbl_t::analyze_reachability. */
static const lattice_element_t *
analyze_reachability(node_t *node, abstract_state_t **state, analysis_collector_t *collector) {
    visit_reachable_children(node, state, collector);
    (*state)->control_flow = FLOW_RETURN;
    return make_bottom_element();
}

/** @brief Virtual table for return node. */
static node_vtbl_t return_vtbl = {
    .type = NODE_RETURN,
    .analyze_reachability = analyze_reachability,
    .is_pure = children_are_pure,
    .collect_direct_effects = collect_child_effects,
    .type_name = L"return",
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
    .calculate = no_abstract_value,
    .execute = execute,
    .generate_goat_code = generate_goat_code,
    .generate_indented_goat_code = generate_indented_goat_code,
    .generate_bytecode = generate_bytecode,
    .can_generate_c_code = can_generate_c_code,
    .generate_c_code = no_c_code,
    .generate_indented_c_code = no_indented_c_code,
    .generate_bytecode_assign = no_bytecode_assignment,
    .generate_bytecode_deferred = no_deferred_bytecode,
};

node_t *create_return_node(arena_t *arena, expression_t *value) {
    return_t *stmt = (return_t *)alloc_zeroed_from_arena(arena, sizeof(return_t));
    stmt->base.base.vtbl = &return_vtbl;
    stmt->value = value;
    return &stmt->base.base;
}
