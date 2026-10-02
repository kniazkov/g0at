/**
 * @file throw.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of the throw statement node.
 */

#include <assert.h>
#include "statement.h"
#include "expression.h"
#include "common_methods.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "analysis/abstract_state.h"
#include "analysis/lattice.h"
#include "codegen/source_builder.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"

/** @brief AST node representing an explicit throw. */
typedef struct {
    /** @brief Base statement structure. */
    statement_t base;

    /** @brief Required thrown expression. */
    expression_t *value;
} throw_t;

/** @brief Implements @ref node_vtbl_t::get_child_count. */
static size_t get_child_count(const node_t *node) {
    const throw_t* stmt = (const throw_t*)node;
    return stmt->value != NULL ? 1 : 0;
}

/** @brief Implements @ref node_vtbl_t::get_child. */
static node_t* get_child(const node_t *node, size_t index) {
    const throw_t* stmt = (const throw_t*)node;
    if (index == 0 && stmt->value) {
        return &stmt->value->base;
    }
    return NULL;
}

/** @brief Implements @ref node_vtbl_t::get_child_tag. */
static const wchar_t* get_child_tag(const node_t *node, size_t index) {
    const throw_t* stmt = (const throw_t*)node;
    if (index == 0 && stmt->value) {
        return L"expression";
    }
    return NULL;
}

/** @brief Implements @ref node_vtbl_t::execute. */
static abstract_state_t *execute(node_t *node, abstract_state_t *state, arena_t *arena) {
    const throw_t* stmt = (const throw_t*)node;
    calculate_expression(stmt->value, state, arena);
    if (state->control_flow == FLOW_NORMAL) state->control_flow = FLOW_UNREACHABLE;
    return state;
}

/** @brief Implements @ref node_vtbl_t::generate_goat_code. */
static string_value_t generate_goat_code(const node_t *node) {
    const throw_t* stmt = (const throw_t*)node;
    string_value_t value = generate_goat_code_from_expression(stmt->value);
    string_value_t result = format_string(L"throw %s;", value.data);
    FREE_STRING(value);
    return result;
}

/** @brief Implements @ref node_vtbl_t::generate_indented_goat_code. */
static void generate_indented_goat_code(const node_t *node, source_builder_t *builder,
       size_t indent) {
    const throw_t* stmt = (const throw_t*)node;
    add_static_source(builder, indent, L"throw ");
    generate_indented_goat_code_from_expression(stmt->value, builder, indent);
    append_static_source(builder, L";");
}

/** @brief Implements @ref node_vtbl_t::generate_bytecode. */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code,
        data_builder_t *data) {
    const throw_t* stmt = (const throw_t*)node;
    instr_index_t first = generate_bytecode_from_expression(stmt->value, code, data);
    add_instruction(code, (instruction_t){ .opcode = THROW });
    return first;
}

/** @brief Virtual table for throw nodes. */
static node_vtbl_t throw_vtbl = {
    .type = NODE_THROW,
    .type_name = L"throw",
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
    .can_generate_c_code = cannot_generate_c_code,
    .generate_c_code = no_c_code,
    .generate_indented_c_code = no_indented_c_code,
    .generate_bytecode_assign = no_bytecode_assignment,
    .generate_bytecode_deferred = no_deferred_bytecode,
};

node_t *create_throw_node(arena_t *arena, expression_t *value) {
    assert(value);
    throw_t *stmt =
        (throw_t *)alloc_zeroed_from_arena(arena, sizeof(throw_t));
    stmt->base.base.vtbl = &throw_vtbl;
    stmt->value = value;
    return &stmt->base.base;
}
