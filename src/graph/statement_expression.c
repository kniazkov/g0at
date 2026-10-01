/**
 * @file statement_expression.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of statement expression node.
 */

#include "statement.h"
#include "expression.h"
#include "common_methods.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "codegen/source_builder.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"

/** @brief A statement expression node. */
typedef struct {
    /** @brief Base statement structure, from which statement_expression_t inherits. */
    statement_t base;

    /** @brief The wrapped expression. */
    expression_t *wrapped;
} statement_expression_t;

/** @brief Implements @ref node_vtbl_t::get_child_count. */
static size_t get_child_count(const node_t *node) {
    return 1;
}

/** @brief Implements @ref node_vtbl_t::get_child. */
static node_t* get_child(const node_t *node, size_t index) {
    const statement_expression_t* expr = (const statement_expression_t*)node;
    if (index == 0) {
        return &expr->wrapped->base;
    }
    return NULL;
}

/** @brief Implements @ref node_vtbl_t::get_child_tag. */
static const wchar_t* get_child_tag(const node_t *node, size_t index) {
    if (index == 0) {
        return L"expression";
    }
    return NULL;
}

/** @brief Implements @ref node_vtbl_t::execute. */
static abstract_state_t *execute(node_t *node, abstract_state_t *state, arena_t *arena) {
    const statement_expression_t *stmt = (const statement_expression_t *)node;
    calculate_expression(stmt->wrapped, state, arena);
    return state;
}

/** @brief Implements @ref node_vtbl_t::generate_goat_code. */
static string_value_t generate_goat_code(const node_t *node) {
    const statement_expression_t *stmt = (const statement_expression_t *)node;
    string_value_t expr_as_string = generate_goat_code_from_expression(stmt->wrapped);
    if (stmt->wrapped->base.vtbl->type != NODE_STATEMENT_LIST) {
        string_builder_t builder;
        init_string_builder(&builder, expr_as_string.length + 1);  // +1 for the semicolon
        append_string_value(&builder, expr_as_string);
        FREE_STRING(expr_as_string);
        return append_char(&builder, L';');
    } else {
        return expr_as_string;
    }
}

/** @brief Implements @ref node_vtbl_t::generate_indented_goat_code. */
static void generate_indented_goat_code(const node_t *node, source_builder_t *builder,
       size_t indent) {
    const statement_expression_t *stmt = (const statement_expression_t *)node;
    if (
        stmt->wrapped->base.vtbl->type == NODE_STATEMENT_LIST &&
        stmt->base.base.parent != NULL &&
        is_branch_or_loop(stmt->base.base.parent->vtbl->type)
    ) {
        generate_indented_goat_code_from_expression(stmt->wrapped, builder, indent - 1);
    } else {
        add_static_source(builder, indent, L"");
        generate_indented_goat_code_from_expression(stmt->wrapped, builder, indent);
        append_static_source(builder, L";");
    }
}

/** @brief Implements @ref node_vtbl_t::generate_bytecode. */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code,
        data_builder_t *data) {
    const statement_expression_t *stmt = (const statement_expression_t *)node;
    instr_index_t first = generate_bytecode_from_expression(stmt->wrapped, code, data);
    add_instruction(code, (instruction_t){ .opcode = POP });
    return first;
}

/** @brief Virtual table for statement expression operations. */
static node_vtbl_t statement_expression_vtbl = {
    .type = NODE_STATEMENT_EXPRESSION,
    .type_name = L"statement expression",
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

statement_t *create_statement_expression_node(arena_t *arena, expression_t *wrapped) {
    statement_expression_t *expr =
        (statement_expression_t *)alloc_zeroed_from_arena(arena, sizeof(statement_expression_t));
    expr->base.base.vtbl = &statement_expression_vtbl;
    expr->wrapped = wrapped;
    return &expr->base;
}
