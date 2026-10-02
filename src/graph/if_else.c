/**
 * @file if_else.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of the if-else statement node.
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

/** @brief AST node representing an if-else statement. */
typedef struct {
    /** @brief Base statement structure. */
    statement_t base;

    /** @brief Condition expression of the if statement. */
    expression_t *condition;

    /** @brief EITHER until the reachability pass proves otherwise. */
    abstract_truth_t condition_truth;

    /** @brief Statement executed when the condition is true. */
    statement_t *true_branch;

    /**
     * @brief Statement executed when the condition is false.
     *
     * This branch is optional and can be `NULL` when the `if` statement has no `else` clause.
     */
    statement_t *false_branch;
} if_else_t;

/** @brief Implements @ref node_vtbl_t::get_child_count. */
static size_t get_child_count(const node_t *node) {
    const if_else_t* stmt = (const if_else_t*)node;
    return stmt->false_branch != NULL ? 3 : 2;
}

/** @brief Implements @ref node_vtbl_t::get_child. */
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

/** @brief Implements @ref node_vtbl_t::get_child_tag. */
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

/** @brief Implements @ref node_vtbl_t::execute. */
static abstract_state_t *execute(node_t *node, abstract_state_t *state, arena_t *arena) {
    const if_else_t *stmt = (const if_else_t *)node;
    if (state->control_flow != FLOW_NORMAL) return state;
    const lattice_element_t *condition = calculate_expression(stmt->condition, state, arena);
    if (state->control_flow != FLOW_NORMAL) return state;
    abstract_truth_t truth = lattice_truth(condition);
    if (truth == ABSTRACT_NEVER) {
        state->control_flow = FLOW_UNREACHABLE;
    } else if (truth == ABSTRACT_TRUE) {
        execute_statement(stmt->true_branch, state, arena);
    } else if (truth == ABSTRACT_FALSE) {
        if (stmt->false_branch) execute_statement(stmt->false_branch, state, arena);
    } else {
        abstract_state_t *left = clone_abstract_state(state);
        abstract_state_t *right = clone_abstract_state(state);
        execute_statement(stmt->true_branch, left, arena);
        if (stmt->false_branch) execute_statement(stmt->false_branch, right, arena);
        abstract_state_t *merged = join_abstract_states(left, right);
        /* Keep the address held by expression/block callers alive. */
        abstract_state_t previous = *state;
        *state = *merged;
        *merged = previous;
        destroy_abstract_state(merged);
        destroy_abstract_state(left);
        destroy_abstract_state(right);
        if (state->control_flow == FLOW_NORMAL) collect_joined_abstract_state(state, node);
    }
    return state;
}

/** @brief Implements @ref node_vtbl_t::generate_goat_code. */
static string_value_t generate_goat_code(const node_t *node) {
    const if_else_t* stmt = (const if_else_t*)node;
    string_builder_t builder;
    init_string_builder(&builder, 32);
    append_static_string(&builder, L"if (");
    string_value_t cs = generate_goat_code_from_expression(stmt->condition);
    append_string_value(&builder, cs);
    FREE_STRING(cs);
    append_static_string(&builder, L") ");
    string_value_t tbs = generate_goat_code_from_statement(stmt->true_branch);
    append_string_value(&builder, tbs);
    FREE_STRING(tbs);
    if (stmt->false_branch) {
        append_static_string(&builder, L" else ");
        string_value_t fbs = generate_goat_code_from_statement(stmt->false_branch);
        append_string_value(&builder, fbs);
        FREE_STRING(fbs);
    }
    return append_static_string(&builder, L"");
}

/** @brief Implements @ref node_vtbl_t::generate_indented_goat_code. */
static void generate_indented_goat_code(const node_t *node, source_builder_t *builder,
       size_t indent) {
    const if_else_t* stmt = (const if_else_t*)node;
    if (node->parent && node->parent->vtbl->type == NODE_IF_ELSE) {
        append_static_source(builder, L"if ("); // `else if` combination
        indent--;
    } else {
        add_static_source(builder, indent, L"if (");
    }
    generate_indented_goat_code_from_expression(stmt->condition, builder, indent);
    append_static_source(builder, L") ");
    generate_indented_goat_code_from_statement(stmt->true_branch, builder, indent + 1);
    if (stmt->false_branch) {
        add_static_source(builder, indent, L"else ");
        generate_indented_goat_code_from_statement(stmt->false_branch, builder, indent + 1);
    }
}

/** @brief Literal conditions have no effects and need not be evaluated. */
static bool is_literal_condition(const node_t *node) {
    switch (node->vtbl->type) {
        case NODE_NULL:
        case NODE_TRUE:
        case NODE_FALSE:
        case NODE_INTEGER:
        case NODE_REAL:
        case NODE_STATIC_STRING:
            return true;
        case NODE_EXPRESSION_PARENTHESIZED:
            return is_literal_condition(get_node_child(node, 0));
        default:
            return false;
    }
}

/** @brief Implements @ref node_vtbl_t::generate_bytecode. */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code,
        data_builder_t *data) {
    const if_else_t* stmt = (const if_else_t*)node;
    instr_index_t first = get_next_instruction_index(code);
    if (stmt->condition_truth != ABSTRACT_EITHER) {
        if (stmt->condition_truth == ABSTRACT_NEVER) {
            return generate_bytecode_from_expression(stmt->condition, code, data);
        }
        if (!is_literal_condition(&stmt->condition->base)) {
            generate_bytecode_from_expression(stmt->condition, code, data);
            add_instruction(code, (instruction_t){ .opcode = POP });
        }
        if (stmt->condition_truth == ABSTRACT_TRUE) {
            generate_bytecode_from_statement(stmt->true_branch, code, data);
        } else if (stmt->false_branch) {
            generate_bytecode_from_statement(stmt->false_branch, code, data);
        }
        return first;
    }
    generate_bytecode_from_expression(stmt->condition, code, data);
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

/** @brief Virtual table for if-else nodes. */
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

node_t *create_if_else_node(arena_t *arena, expression_t *condition, statement_t *true_branch,
        statement_t *false_branch) {
    if_else_t *stmt =
        (if_else_t *)alloc_zeroed_from_arena(arena, sizeof(if_else_t));
    stmt->base.base.vtbl = &if_else_vtbl;
    stmt->condition = condition;
    stmt->condition_truth = ABSTRACT_EITHER;
    stmt->true_branch = true_branch;
    stmt->false_branch = false_branch;
    return &stmt->base.base;
}

void set_if_else_condition_truth(node_t *node, abstract_truth_t truth) {
    ((if_else_t *)node)->condition_truth = truth;
}
