/**
 * @file if_else.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of the if-else statement node.
 * 
 * This file defines the behavior of the `if` statement node in the abstract
 * syntax tree. The statement contains a condition expression, a branch executed
 * when the condition is true, and an optional branch executed when the condition
 * is false.
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
 * @brief AST node representing an if-else statement.
 * 
 * This structure defines a conditional statement in the abstract syntax tree
 * (AST). It stores the condition expression, the statement executed when the
 * condition is true, and an optional statement executed when the condition is
 * false.
 */
typedef struct {
    /**
     * @brief Base statement structure.
     * 
     * Allows the if-else statement to be treated as a generic statement in the
     * AST.
     */
    statement_t base;

    /**
     * @brief Condition expression of the if statement.
     * 
     * This expression is evaluated first. Its boolean value determines which
     * branch is executed.
     */
    expression_t *condition;

    /**
     * @brief Statement executed when the condition is true.
     * 
     * This branch is required and represents the body following the `if`
     * condition.
     */
    statement_t *true_branch;

    /**
     * @brief Statement executed when the condition is false.
     * 
     * This branch is optional and can be `NULL` when the `if` statement has no
     * `else` clause.
     */
    statement_t *false_branch;
} if_else_t;

/**
 * @brief Returns the number of child nodes in an if-else statement.
 * 
 * An if-else statement always has two child nodes: the condition expression and
 * the true branch. If an else branch is present, it has a third child node.
 * 
 * @param node A pointer to the if-else statement node.
 * @return 3 if the statement has an else branch, 2 otherwise.
 */
static size_t get_child_count(const node_t *node) {
    const if_else_t* stmt = (const if_else_t*)node;
    return stmt->false_branch != NULL ? 3 : 2;
}

/**
 * @brief Retrieves a child node of an if-else statement by index.
 * 
 * Index 0 returns the condition expression, index 1 returns the true branch,
 * and index 2 returns the false branch if it exists. If the index is out of
 * range, or if the false branch is absent, `NULL` is returned.
 * 
 * @param node Pointer to the if-else statement node.
 * @param index Zero-based child index.
 * @return Pointer to the requested child node, or NULL if not applicable.
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
 * @brief Returns the tag label for a child of an if-else statement.
 * 
 * The condition child is tagged as `condition`, the true branch is tagged as
 * `true`, and the optional false branch is tagged as `false`.
 * 
 * @param node A pointer to the if-else statement node.
 * @param index Zero-based index of the child node.
 * @return A wide string label for the child node, or NULL if not applicable.
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
 * @brief Executes abstract interpretation for an if-else statement node.
 * 
 * Implements the `execute` virtual method for if-else nodes. The method is
 * reserved for abstract interpretation of conditional control flow. It should
 * eventually evaluate the condition abstractly, execute reachable branches, and
 * merge their resulting abstract states.
 * 
 * The current implementation is a placeholder and returns the input state
 * unchanged.
 * 
 * @param node The if-else statement node to execute.
 * @param state Current abstract state.
 * @param arena Arena used for lattice elements produced during branch analysis.
 * @return The resulting abstract state. Currently this is the same state that
 *         was passed in.
 */
static abstract_state_t *execute(node_t *node, abstract_state_t *state, arena_t *arena) {
    // for now without implementation
    return state;
}

/**
 * @brief Generates compact Goat source code for an if-else statement.
 * 
 * Produces a single-line textual representation of the conditional statement,
 * including the condition, the true branch, and the optional else branch.
 * 
 * @param node A pointer to the if-else statement node.
 * @return A `string_value_t` containing the generated Goat source code.
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
 * @brief Generates indented Goat source code for an if-else statement.
 * 
 * Produces a formatted representation of the conditional statement with the
 * specified indentation. The condition is emitted first, followed by the true
 * branch and, if present, the else branch.
 * 
 * @param node A pointer to the if-else statement node.
 * @param builder A pointer to the `source_builder_t` used to accumulate the
 *        output.
 * @param indent The number of tab characters to prepend for indentation.
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
 * @brief Generates bytecode for an if-else statement node.
 * 
 * The condition expression is emitted first. A `JIF` instruction is then used
 * to skip the true branch when the condition is false. If an else branch exists,
 * a trailing `JUMP` instruction skips over that branch after the true branch has
 * finished executing. Jump targets are patched after the corresponding target
 * instruction indices become known.
 * 
 * @param node A pointer to the if-else statement node.
 * @param code A pointer to the bytecode builder.
 * @param data A pointer to the static data builder.
 * @return The instruction index of the first emitted instruction.
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
        generate_bytecode_from_statement(stmt->false_branch, code, data);
        get_instruction(code, jump_index)->arg1 = get_next_instruction_index(code);
    } else {
        get_instruction(code, jif_index)->arg1 = get_next_instruction_index(code);
    }
    return first;
}

/**
 * @brief Virtual table for if-else nodes.
 * 
 * This virtual table provides the implementation of operations specific to
 * if-else statement nodes.
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
    stmt->false_branch = false_branch;
    return &stmt->base.base;
}
