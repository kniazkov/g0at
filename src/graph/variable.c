/**
 * @file variable.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of the variable expression node.
 * 
 * This file defines the behavior of the variable expression node, which represents
 * a variable by its name in the syntax tree. 
 */

#include "assignable_expression.h"
#include "common_methods.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"

/**
 * @struct variable_t
 * @brief Represents a variable expression node.
 * 
 * This structure defines a variable expression in the syntax tree. A variable refers
 * to a named entity, such as a variable or constant in the source code. The structure
 * extends `assignable_expression_t` and includes a field for storing the variable's name.
 */
typedef struct {
    /**
     * @brief Base expression structure from which variable_t inherits.
     */
    assignable_expression_t base;

    /**
     * @brief Pointer to the wide-character string representing the variable's name.
     */
    wchar_t *name;

    /**
     * @brief Length of the variable's name in characters (not including the null terminator).
     */
    size_t name_length;
} variable_t;

/**
 * @brief Gets the variable name as string data
 * @param node Pointer to the node.
 * @return `string_value_t` containing variable name
 */
static string_value_t get_data(const node_t *node) {
    const variable_t *expr = (const variable_t *)node;
    return (string_value_t){ expr->name, expr->name_length, false };
}

/**
 * @brief Converts a variable expression to its string representation.
 * 
 * This function converts the given variable expression to its representation as it would
 * appear in the source code (e.g., the variable's name). The resulting string is suitable
 * for embedding in other contexts, such as code generation.
 * 
 * @param node A pointer to the variable expression node.
 * @return A `string_value_t` containing the formatted string representation.
 */
static string_value_t generate_goat_code(const node_t *node) {
    const variable_t *expr = (const variable_t *)node;
    return (string_value_t){ expr->name, expr->name_length, false };
}

/**
 * @brief Generates bytecode for a variable node.
 * 
 * This function generates bytecode for a variable node by first adding the variable's name
 * to the data segment, and then generating a `VLOAD` instruction with the index of the variable's
 * name in the data segment.
 * 
 * @param node A pointer to the node representing a variable.
 * @param code A pointer to the `code_builder_t` structure used for generating instructions.
 * @param data A pointer to the `data_builder_t` structure used for managing the data segment.
 */
static void generate_bytecode(const node_t *node, code_builder_t *code,
        data_builder_t *data) {
    const variable_t *expr = (const variable_t *)node;
    uint32_t index = add_string_to_data_segment_ex(data, expr->name, expr->name_length);
    add_instruction(code, (instruction_t){ .opcode = VLOAD, .arg1 = index });
}

/**
 * @brief Generates bytecode for storing a value into a variable.
 * 
 * This function implements the assignment operation for variable nodes by:
 * 1. Adding the variable's name to the data segment (if not already present)
 * 2. Generating a STORE instruction with the variable's data segment index
 * 
 * @param node A pointer to the variable node (must be of variable type)
 * @param code Code builder for instruction generation
 * @param data Data builder for string management
 */
static void generate_bytecode_assign(const node_t *node, code_builder_t *code,
        data_builder_t *data) {
    const variable_t *expr = (const variable_t *)node;
    uint32_t index = add_string_to_data_segment_ex(data, expr->name, expr->name_length);
    add_instruction(code, (instruction_t){ .opcode = STORE, .arg1 = index });
}

/**
 * @brief Virtual table for variable expressions.
 * 
 * This virtual table provides the implementation of operations specific to variable expressions.
 * It includes function pointers for operations such as converting the variable expression to
 * a string representation and generating the corresponding bytecode.
 */
static node_vtbl_t variable_vtbl = {
    .type = NODE_VARIABLE,
    .type_name = L"variable",
    .is_assignable_expression = true,
    .get_data = get_data,
    .get_child_count = no_children,
    .get_child = no_child,
    .get_child_tag = no_tags,
    .generate_goat_code = generate_goat_code,
    .generate_indented_goat_code = stub_indented_goat_code_generator,
    .generate_bytecode = generate_bytecode,
    .generate_bytecode_assign = generate_bytecode_assign
};

expression_t *create_variable_node(arena_t *arena, const wchar_t *name, size_t name_length) {
    variable_t *expr = (variable_t *)alloc_from_arena(arena, sizeof(variable_t));
    expr->base.base.base.vtbl = &variable_vtbl;
    expr->name = copy_string_to_arena(arena, name, name_length);
    expr->name_length = name_length;
    return &expr->base.base;
}
