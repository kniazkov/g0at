/**
 * @file simple_assignment.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of the simple assignment expression node.
 * 
 * This file defines the behavior of the simple assignment expression node, which represents
 * the '=' operation in the syntax tree.
 */

#include <assert.h>

#include "assignment.h"
#include "statement.h"
#include "common_methods.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/source_builder.h"

/**
 * @struct simple_assignment_t
 * @brief Represents a simple assignment operation expression node.
 * 
 * This structure defines a simple assignment operation in the syntax tree. The node
 * extends `assignment_t` and includes references to the target (lvalue) and value (rvalue).
 */

typedef struct {
    /**
     * @brief Base binary operation structure from which addition_t inherits.
     */
    assignment_t base;
} simple_assignment_t;

/**
 * @brief Converts an assignment operation to its string representation.
 * @param node A pointer to the assignment expression node.
 * @return A `string_value_t` containing "target = value" representation.
 */
static string_value_t generate_goat_code(const node_t *node) {
    const simple_assignment_t *expr = (const simple_assignment_t *)node;
    string_value_t left =
        expr->base.left_operand->base.base.vtbl->generate_goat_code(
            &expr->base.left_operand->base.base);
    string_value_t right =
        expr->base.right_operand->base.vtbl->generate_goat_code(&expr->base.right_operand->base);
    string_value_t result = format_string(L"%s = %s", left.data, right.data);
    FREE_STRING(left);
    FREE_STRING(right);
    return result;
}

/**
 * @brief Generates indented Goat source code for a simple assignment operation node.
 * 
 * This function implements the virtual method for generating Goat source code for a simple
 * assignment expression (`=` operator). It recursively generates code for both left and right
 * operands, combining them with the assignment operator in between.
 * 
 * @param node Pointer to the assignment node to generate code for.
 * @param builder Pointer to the source builder where generated code will be stored.
 * @param indent The current indentation level (in tabs) for code generation.
 */
static void generate_indented_goat_code(const node_t *node, source_builder_t *builder,
            size_t indent) {
    const simple_assignment_t *expr = (const simple_assignment_t *)node;
    expr->base.left_operand->base.base.vtbl->generate_indented_goat_code(
        &expr->base.left_operand->base.base, builder, indent);
    append_formatted_line_of_source(builder, STATIC_STRING(L" = "));
    expr->base.right_operand->base.vtbl->generate_indented_goat_code(
        &expr->base.right_operand->base, builder, indent);
}

/**
 * @brief Generates bytecode for simple assignment operation.
 * @param node A pointer to the node representing the addition operation.
 * @param code A pointer to the `code_builder_t` structure used for generating instructions.
 * @param data A pointer to the `data_builder_t` structure used for managing the data segment.
 */
static void generate_bytecode(const node_t *node, code_builder_t *code,
        data_builder_t *data) {
    const simple_assignment_t *expr = (const simple_assignment_t *)node;
    expr->base.right_operand->base.vtbl->generate_bytecode(
        &expr->base.right_operand->base, code, data);
    expr->base.left_operand->base.base.vtbl->generate_bytecode_assign(
        &expr->base.left_operand->base.base, code, data);
}

/**
 * @brief Virtual table for simple assignment operations.
 * 
 * This virtual table provides the implementation of operations specific to assignment expressions.
 * It includes function pointers for operations such as converting the simple assignment expression
 * to a string representation and generating the corresponding bytecode.
 */
static node_vtbl_t simple_assignment_vtbl = {
    .type = NODE_SIMPLE_ASSIGNMENT,
    .type_name = L"assignment",
    .get_data = no_data,
    .get_child_count = assignment_get_child_count,
    .get_child = assignment_get_child,
    .get_child_tag = assignment_get_tag,
    .generate_goat_code = generate_goat_code,
    .generate_indented_goat_code = generate_indented_goat_code,
    .generate_bytecode = generate_bytecode
};

expression_t *create_simple_assignment_node(arena_t *arena, assignable_expression_t *left_operand,
        expression_t *right_operand) {
    simple_assignment_t *expr = (simple_assignment_t *)alloc_from_arena(
        arena,
        sizeof(simple_assignment_t)
    );
    expr->base.base.base.vtbl = &simple_assignment_vtbl;
    expr->base.left_operand = left_operand;
    expr->base.right_operand = right_operand;
    return &expr->base.base;
}

declarator_t *create_declarator_from_simple_assignment(const node_t *expr) {
    assert(expr->vtbl->type == NODE_SIMPLE_ASSIGNMENT);
    const simple_assignment_t *assign = (simple_assignment_t *)expr;
    if (assign->base.left_operand->base.base.vtbl->type != NODE_VARIABLE) {
        return NULL;
    }
    declarator_t *decl = create_declarator_from_variable(&assign->base.left_operand->base.base);
    decl->initial = assign->base.right_operand;
    return decl;
}
