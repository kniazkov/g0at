/**
 * @file return.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of the return statement node.
 * 
 * This file defines the behavior of the `return` statement node in the abstract syntax tree.
 * The return statement is used to exit from a function and optionally provide a return value.
 * This implementation handles semantic representation and bytecode generation for return logic.
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

/**
 * @struct return_t
 * @brief AST node representing a return statement.
 * 
 * This structure defines the `return` statement in the abstract syntax tree (AST).
 * A return statement optionally holds a value expression that specifies what value
 * should be returned from a function.
 */
typedef struct {
    /**
     * @brief Base statement structure.
     * 
     * Allows the return statement to be treated as a generic statement in the AST.
     */
    statement_t base;

    /**
     * @brief The expression representing the return value (optional).
     * 
     * This can be `NULL` for `return;` without a value, or point to an expression
     * that produces the return value (e.g., `return x + 1;`).
     */
    expression_t *value;
} return_t;

/**
 * @brief Returns the number of child nodes in a return statement.
 * 
 * A return statement has one child if it returns a value expression,
 * or zero if it is a bare `return;` without a value.
 * 
 * @param node A pointer to the return statement node.
 * @return 1 if the return statement has a value expression, 0 otherwise.
 */
static size_t get_child_count(const node_t *node) {
    const return_t* stmt = (const return_t*)node;
    return stmt->value != NULL ? 1 : 0;
}

/**
 * @brief Retrieves the child node of a return statement by index.
 * 
 * If the return statement has a value expression, it is returned for index 0.
 * Otherwise, or if the index is out of range, `NULL` is returned.
 * 
 * @param node Pointer to the return statement node.
 * @param index Must be 0 to retrieve the return value expression.
 * @return Pointer to the return value expression node, or NULL.
 */
static const node_t* get_child(const node_t *node, size_t index) {
    const return_t* stmt = (const return_t*)node;
    if (index == 0 && stmt->value) {
        return &stmt->value->base;
    }
    return NULL;
}

/**
 * @brief Returns the tag label for a child of a return statement.
 * 
 * If the return statement has a value expression and the index is 0,
 * this function returns the label "expression". Otherwise, it returns NULL.
 * 
 * @param node A pointer to the return statement node.
 * @param index Zero-based index of the child node.
 * @return A wide string label for the child node, or NULL if not applicable.
 */
static const wchar_t* get_child_tag(const node_t *node, size_t index) {
    const return_t* stmt = (const return_t*)node;
    if (index == 0 && stmt->value) {
        return L"expression";
    }
    return NULL;
}

/**
 * @brief Generates Goat source code for a return statement.
 * 
 * Produces a single-line textual representation of the return statement.
 * If the return includes a value expression, it is printed after `return`.
 * Otherwise, just `return;` is emitted.
 * 
 * @param node A pointer to the return statement node.
 * @return A `string_value_t` containing the generated Goat source code.
 */
static string_value_t generate_goat_code(const node_t *node) {
    const return_t* stmt = (const return_t*)node;
    if (stmt->value) {
        string_builder_t builder;
        string_value_t value_as_string =
            stmt->value->base.vtbl->generate_goat_code(&stmt->value->base);
        init_string_builder(&builder, value_as_string.length + 8);
        append_static_string(&builder, L"return ");
        append_string_value(&builder, value_as_string);
        FREE_STRING(value_as_string);
        return append_char(&builder, L';');
    } else {
        return STATIC_STRING(L"return;");
    }
}

/**
 * @brief Generates indented Goat source code for a return statement.
 * 
 * Produces a formatted representation of the return statement with the specified indentation.
 * If the return includes a value expression, it is emitted after `return`, followed by a semicolon.
 * Otherwise, just `return;` is generated.
 * 
 * @param node A pointer to the return statement node.
 * @param builder A pointer to the `source_builder_t` used to accumulate the output.
 * @param indent The number of tab characters to prepend for indentation.
 */
static void generate_indented_goat_code(const node_t *node, source_builder_t *builder,
       size_t indent) {
    const return_t* stmt = (const return_t*)node;
    if (stmt->value) {
        add_static_source(builder, indent, L"return ");
        stmt->value->base.vtbl->generate_indented_goat_code(&stmt->value->base, builder, indent);
        append_static_source(builder, L";");
    } else {
        add_static_source(builder, indent, L"return;");
    }
}

/**
 * @brief Generates bytecode for a return statement node.
 * 
 * If the return statement includes a value, bytecode is generated for the value expression
 * first. Otherwise, a `NIL` instruction is emitted to represent an empty return.
 * In both cases, a `RET` instruction is added at the end to complete the return operation.
 * 
 * @param node A pointer to the return statement node.
 * @param code A pointer to the bytecode builder.
 * @param data A pointer to the static data builder.
 * @return The instruction index of the first emitted instruction.
 */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code,
        data_builder_t *data) {
    const return_t* stmt = (const return_t*)node;
    instr_index_t first;
    if (stmt->value) {
        first = stmt->value->base.vtbl->generate_bytecode(&stmt->value->base, code, data);
    } else {
        first = add_instruction(code, (instruction_t){ .opcode = NIL });
    }
    add_instruction(code, (instruction_t){ .opcode = RET });
    return first;
}

/**
 * @brief Virtual table for return node.
 * 
 * This virtual table provides the implementation of operations specific to return nodes.
 */
static node_vtbl_t return_vtbl = {
    .type = NODE_RETURN,
    .type_name = L"return",
    .get_data = no_data,
    .get_child_count = get_child_count,
    .get_child = get_child,
    .get_child_tag = get_child_tag,
    .generate_goat_code = generate_goat_code,
    .generate_indented_goat_code = generate_indented_goat_code,
    .generate_bytecode = generate_bytecode,
};

statement_t *create_return_node(arena_t *arena, expression_t *value) {
    return_t *stmt = 
        (return_t *)alloc_from_arena(arena, sizeof(return_t));
    stmt->base.base.vtbl = &return_vtbl;
    stmt->value = value;
    return &stmt->base;
}
