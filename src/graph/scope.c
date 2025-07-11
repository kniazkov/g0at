/**
 * @file scope.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of the scope node in the abstract syntax tree (AST).
 * 
 * This file defines the behavior of the scope node, which represents a block of statements
 * enclosed in curly braces. The scope node creates a new lexical environment where variables
 * can be declared and used without affecting the outer scope. It is fundamental for implementing
 * block scoping, control structures, and functions in the language.
 */

#include "common_methods.h"
#include "expression.h"
#include "statement.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/source_builder.h"

/**
 * @struct scope_t
 * @brief Represents a scope node in the abstract syntax tree.
 * 
 * This structure defines a scope node that contains a list of statements enclosed within
 * curly braces. The scope node creates a new lexical environment when executed, allowing
 * for local variable declarations that don't pollute the outer scope.
 */
typedef struct {
    /**
     * @brief Base expression structure from which scope_t inherits.
     * 
     * This allows the scope node to be treated as an expression in the AST while
     * providing the necessary functionality for tree traversal and manipulation.
     */
    expression_t base;

    /**
     * @brief List of statements contained within the scope.
     * 
     * This is an array of pointers to statement nodes that make up the body
     * of the scope block.
     */
    statement_t **stmt_list;

    /**
     * @brief Number of statements in the scope.
     * 
     * This field stores the count of statements in the scope's statement list.
     */
    size_t stmt_count;
} scope_t;

/**
 * @brief Gets the number of child statements in scope.
 * @param node Pointer to the scope.
 * @return Number of child statements (0 for empty scopes).
 */
static size_t get_child_count(const node_t *node) {
    const scope_t* scope = (const scope_t*)node;
    return scope->stmt_count;
}

/**
 * @brief Retrieves a specific child statement from scope.
 * 
 * Safely accesses the statement list with bounds checking.
 * Valid indices range from 0 to stmt_count-1.
 * 
 * @param node Pointer to the scope.
 * @param index Zero-based statement position.
 * @return Pointer to statement node or NULL.
 */
static const node_t* get_child(const node_t *node, size_t index) {
    const scope_t* scope = (const scope_t*)node;
    if (index >= scope->stmt_count) {
        return NULL;
    }
    return &scope->stmt_list[index]->base;
}

/**
 * @brief Converts the scope node to its string representation in Goat language syntax.
 * 
 * This function generates the canonical string representation of a scope block by:
 * 1. Opening with a curly brace '{'
 * 2. Concatenating all contained statements' string representations
 * 3. Closing with a curly brace '}'
 * 
 * Statements are space-separated when multiple exist in the scope. Empty scopes
 * are represented as "{}".
 * 
 * @param node Pointer to the scope node to stringify.
 * @return string_value_t containing the formatted scope representation.
 */
static string_value_t generate_goat_code(const node_t *node) {
    const scope_t* scope = (const scope_t*)node;
    string_builder_t builder;
    init_string_builder(&builder, 2); // for empty scope: {}
    append_char(&builder, L'{');
    for (size_t index = 0; index < scope->stmt_count; index++) {
        if (index > 0) {
            append_char(&builder, L' ');
        }
        statement_t *stmt = scope->stmt_list[index];
        string_value_t stmt_as_string = stmt->base.vtbl->generate_goat_code(&stmt->base);
        append_string_value(&builder, stmt_as_string);
        FREE_STRING(stmt_as_string);
    }
    return append_char(&builder, L'}');
}

/**
 * @brief Generates properly indented Goat source code for the scope node.
 * 
 * Produces human-readable source code with correct block indentation by:
 * 1. Writing opening brace on its own line at current indent level
 * 2. Generating all statements at indent + 1 level
 * 3. Writing closing brace at original indent level
 * 
 * The formatting follows these conventions:
 * - Braces always appear on separate lines
 * - Nested blocks increase indentation by 1 tab
 * - Statements maintain their own indentation rules
 * 
 * @param node Pointer to the scope node to format..
 * @param builder Pointer to source builder accumulating the output.
 * @param indent Base indentation level (number of tabs) for this scope.
 * @return bool True if all statements generated successfully, false if any
 *  statement generation failed.
 */
static bool generate_indented_goat_code(const node_t *node, source_builder_t *builder,
        size_t indent) {
    const scope_t* scope = (const scope_t*)node;
    bool result = true;
    add_line_of_source_code(builder, indent, L"{");
    for (size_t index = 0; result && index < scope->stmt_count; index++) {
        statement_t *stmt = scope->stmt_list[index];
        result = stmt->base.vtbl->generate_indented_goat_code(&stmt->base, builder, indent + 1);
    }
    add_line_of_source_code(builder, indent, L"}");
    return result;
}

/**
 * @brief Generates bytecode sequence for a scope block.
 * 
 * Emits VM instructions that implement scope semantics by:
 * 1. ENTER - Creates new lexical context
 * 2. [statements] - Generates all contained statements' bytecode
 * 3. LEAVE - Restores previous context while keeping scope result
 * 
 * Memory and execution context handling:
 * - ENTER/LEAVE instructions manage context chaining
 * - All statement bytecode executes within the new context
 * - The final LEAVE pushes the scope's result object
 * 
 * @param node Pointer to the scope node to compile..
 * @param code Code builder receiving generated instructions.
 * @param data Data builder for constant pool management.
 */
static void generate_bytecode(const node_t *node, code_builder_t *code,
        data_builder_t *data) {
    const scope_t* scope = (const scope_t*)node;
    add_instruction(code, (instruction_t){ .opcode = ENTER });
    for (size_t index = 0; index < scope->stmt_count; index++) {
        statement_t *stmt = scope->stmt_list[index];
        stmt->base.vtbl->generate_bytecode(&stmt->base, code, data);
    }
    add_instruction(code, (instruction_t){ .opcode = LEAVE });
}

/**
 * @brief Virtual table for scope node operations.
 * 
 * This virtual table provides the implementation of operations specific to scope nodes in the
 * abstract syntax tree (AST).
 */
static node_vtbl_t scope_vtbl = {
    .type = NODE_SCOPE,
    .type_name = L"scope",
    .get_data = no_data,
    .get_child_count = get_child_count,
    .get_child = get_child,
    .get_child_tag = no_tags,
    .generate_goat_code = generate_goat_code,
    .generate_indented_goat_code = stub_indented_goat_code_generator,
    .generate_bytecode = generate_bytecode
};

node_t *create_scope_node(arena_t *arena, statement_t **stmt_list, size_t stmt_count) {
    scope_t *scope = (scope_t *)alloc_from_arena(arena, sizeof(scope_t));
    scope->base.base.vtbl = &scope_vtbl;
    size_t data_size = stmt_count * sizeof(statement_t *);
    scope->stmt_list = (statement_t **)alloc_from_arena(arena, data_size);
    memcpy(scope->stmt_list, stmt_list, data_size);
    scope->stmt_count = stmt_count;
    return &scope->base.base;
}
