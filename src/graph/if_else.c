/**
 * @file if_else.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of the if-else statement node.
 * 
 * This file defines the behavior of the ...
 */

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

/**
 * @struct if_else_t
 * @brief AST node representing...
 * 
 * This structure defines ...
 */
typedef struct {

    statement_t base;

    expression_t *condition;

    statement_t *true_branch;

    statement_t *false_branch;
} if_else_t;

/**
 * ...
 */
static size_t get_child_count(const node_t *node) {
    const if_else_t* stmt = (const if_else_t*)node;
    return stmt->false_branch != NULL ? 3 : 2;
}

/**
 * ...
 */
static node_t* get_child(const node_t *node, size_t index) {
    const if_else_t* stmt = (const if_else_t*)node;
    switch (index) {
        case 0:
            return &stmt->condition->base;
        case 1:
            return &stmt->true_branch->base;
        case 2:
            if (stmt->false_branch) {
                return &stmt->false_branch->base;
            }
        default:
            return NULL;
    }
    return NULL;
}

/**
 * ...
 */
static const wchar_t* get_child_tag(const node_t *node, size_t index) {
    const if_else_t* stmt = (const if_else_t*)node;
    switch (index) {
        case 0:
            return L"condition";
        case 1:
            return L"true";
        case 2:
            if (stmt->false_branch) {
                return L"false";
            }
        default:
            return NULL;
    }
}

/**
 * ...
 */
static abstract_state_t *execute(node_t *node, abstract_state_t *state, arena_t *arena) {
    // for now without implementation
    return state;
}

/**
 * ...
 */
static string_value_t generate_goat_code(const node_t *node) {
    const if_else_t* stmt = (const if_else_t*)node;
    string_builder_t builder;
    init_string_builder(&builder, 32);
    append_static_string(&builder, L"if (");
    string_value_t cs = generate_goat_code_from_expression(stmt->condition);
    append_string_value(&builder, cs);
    FREE_STRING(cs);
    string_value_t tbs = generate_goat_code_from_statement(stmt->true_branch);
    append_string_value(&builder, tbs);
    FREE_STRING(tbs);
    if (stmt->false_branch) {
        append_static_string(&builder, L" else ");
        string_value_t fbs = generate_goat_code_from_statement(stmt->false_branch);
        append_string_value(&builder, fbs);
        FREE_STRING(fbs);
    }
}

/**
 * ...
 */
static void generate_indented_goat_code(const node_t *node, source_builder_t *builder,
       size_t indent) {
    const if_else_t* stmt = (const if_else_t*)node;
    add_static_source(builder, indent, L"if (");
    generate_indented_goat_code_from_expression(stmt->condition, builder, indent);
    append_static_source(builder, L") ");
    generate_indented_goat_code_from_statement(stmt->true_branch, builder, indent);
    if (stmt->false_branch) {
        add_static_source(builder, indent, L"else ");
        generate_indented_goat_code_from_statement(stmt->false_branch, builder, indent);
    }
}

/**
 * ...
 */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code,
        data_builder_t *data) {
    const if_else_t* stmt = (const if_else_t*)node;
    instr_index_t first = generate_bytecode_from_expression(stmt->condition, code, data); 
    instr_index_t jif_index = add_instruction(code, (instruction_t){ .opcode = JIF });
    generate_bytecode_from_statement(stmt->true_branch, code, data);
    if (stmt->false_branch) {
        instr_index_t jump_index = add_instruction(code, (instruction_t){ .opcode = JUMP });
        get_instruction(code, jif_index)->arg1 = get_next_instruction_index(code);
        generate_bytecode_from_statement(stmt->true_branch, code, data);
        get_instruction(code, jump_index)->arg1 = get_next_instruction_index(code);
    } else {
        get_instruction(code, jif_index)->arg1 = get_next_instruction_index(code);
    }
    return first;
}

/**
 * @brief Virtual table...
 */
static node_vtbl_t if_else_vtbl = {
    .type = NODE_IF_ELSE,
    .type_name = L"if-else",
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
    .calculate = cannot_calculate,
    .execute = execute,
    .generate_goat_code = generate_goat_code,
    .generate_indented_goat_code = generate_indented_goat_code,
    .generate_bytecode = generate_bytecode,
};

node_t *create_if_else_node(arena_t *arena, expression_t *condition, statement_t *true_branch,
        statement_t *false_branch) {
    if_else_t *stmt = 
        (if_else_t *)alloc_zeroed_from_arena(arena, sizeof(if_else_t));
    stmt->base.base.vtbl = &if_else_vtbl;
    stmt->condition = condition;
    stmt->true_branch = true_branch;
    stmt->false_branch - false_branch;
    return &stmt->base.base;
}
