/**
 * @file variable.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of the variable expression node.
 * 
 * This file defines the behavior of the variable expression node, which represents
 * a variable by its name in the syntax tree. 
 */

#include <assert.h>

#include "assignable_expression.h"
#include "common_methods.h"
#include "statement.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/source_builder.h"

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
     * @brief String representing the variable's name.
     */
    string_view_t name;
} variable_t;

/**
 * @brief Gets the variable name as string data
 * @param node Pointer to the node.
 * @return `string_value_t` containing variable name
 */
static string_value_t get_data(const node_t *node) {
    const variable_t *expr = (const variable_t *)node;
    return VIEW_TO_VALUE(expr->name);
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
    return VIEW_TO_VALUE(expr->name);
}

/**
 * @brief Generates indented Goat source code for a variable reference expression.
 *
 * This function implements the virtual method for generating Goat source code that represents
 * a variable reference. It outputs the variable's name directly without any additional
 * formatting or operators, as a variable reference is a terminal expression in the syntax tree.
 *
 * @param node Pointer to the AST node representing the variable reference.
 * @param builder Pointer to the source builder where generated code will be stored.
 * @param indent The current indentation level (in tabs) for code generation (unused).
 */
static void generate_indented_goat_code(const node_t *node, source_builder_t *builder,
            size_t indent) {
    const variable_t *expr = (const variable_t *)node;
    append_formatted_source(builder, VIEW_TO_VALUE(expr->name));
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
 * @return The instruction index of the first emitted instruction.
 */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code,
        data_builder_t *data) {
    const variable_t *expr = (const variable_t *)node;
    uint32_t index = add_string_to_data_segment_ex(data, expr->name);
    return add_instruction(code, (instruction_t){ .opcode = VLOAD, .arg1 = index });
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
 * @return The instruction index of the first emitted instruction.
 */
static instr_index_t generate_bytecode_assign(const node_t *node, code_builder_t *code,
        data_builder_t *data) {
    const variable_t *expr = (const variable_t *)node;
    uint32_t index = add_string_to_data_segment_ex(data, expr->name);
    return add_instruction(code, (instruction_t){ .opcode = STORE, .arg1 = index });
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
    .generate_indented_goat_code = generate_indented_goat_code,
    .generate_bytecode = generate_bytecode,
    .generate_bytecode_assign = generate_bytecode_assign
};

expression_t *create_variable_node(arena_t *arena, string_view_t name) {
    variable_t *expr = (variable_t *)alloc_from_arena(arena, sizeof(variable_t));
    expr->base.base.base.vtbl = &variable_vtbl;
    expr->name = copy_string_to_arena(arena, name.data, name.length);
    return &expr->base.base;
}

declarator_t *create_declarator_from_variable(const node_t *expr) {
    assert(expr->vtbl->type == NODE_VARIABLE);
    const variable_t *var = (variable_t *)expr;
    declarator_t *decl = (declarator_t*)ALLOC(sizeof(declarator_t));
    decl->name = var->name;
    decl->initial = NULL;
    return decl;
}
