/**
 * @file null.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of the null expression.
 * 
 * This file defines the behavior of the null expression, which represents a literal
 * null value in the syntax tree. The null value is a singleton that indicates the
 * absence of a meaningful value and can be used in variable declarations,
 * assignments, and function returns.
 */

#include <memory.h>

#include "expression.h"
#include "common_methods.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/source_builder.h"

/**
 * @struct null_t
 * @brief Represents a null expression node.
 * 
 * This structure defines a null expression in the syntax tree. The null value
 * is a singleton that indicates the absence of a meaningful value. The structure
 * extends `expression_t` but contains no additional fields since null requires
 * no additional data storage.
 */
typedef struct {
    /**
     * @brief Base expression structure from which null_t inherits.
     */
    expression_t base;
} null_t;

/**
 * @brief Converts a null expression to its string representation.
 * 
 * This function returns the string "null" as the representation of the null
 * expression, matching how it would appear in source code. The returned
 * string is a constant value and doesn't require memory management.
 * 
 * @param node A pointer to the null expression node (unused).
 * @return A `string_value_t` containing the literal string "null".
 */
static string_value_t generate_goat_code(const node_t *node) {
    return STATIC_STRING(L"null"); 
}

/**
 * @brief Generates indented Goat source code for a null expression.
 *
 * @param node Pointer to the AST node representing the null expression.
 * @param builder Pointer to the source builder where generated code will be stored.
 * @param indent The current indentation level (in tabs) for code generation (unused).
 */
static void generate_indented_goat_code(const node_t *node, source_builder_t *builder,
            size_t indent) {
    append_static_source(builder, L"null");
}

/**
 * @brief Generates bytecode for a null expression.
 * 
 * This function generates bytecode for a null expression by emitting a `NIL`
 * opcode. The NIL instruction pushes the null value onto the virtual machine's
 * stack when executed.
 * 
 * @param node A pointer to the node representing the null expression (unused).
 * @param code A pointer to the `code_builder_t` structure used for generating instructions.
 * @param data A pointer to the `data_builder_t` structure (unused in this case).
 */
static void generate_bytecode(const node_t *node, code_builder_t *code,
        data_builder_t *data) {
    add_instruction(code, (instruction_t){ .opcode = NIL });
}

/**
 * @brief Virtual table for null expressions.
 * 
 * This virtual table provides the implementation of operations specific to null
 * expressions. It contains function pointers for operations such as converting
 * the null value to a string representation and generating the corresponding
 * bytecode.
 */
static node_vtbl_t null_vtbl = {
    .type = NODE_NULL,
    .type_name = L"null",
    .get_data = no_data,
    .get_child_count = no_children,
    .get_child = no_child,
    .get_child_tag = no_tags,
    .generate_goat_code = generate_goat_code,
    .generate_indented_goat_code = generate_indented_goat_code,
    .generate_bytecode = generate_bytecode,
};

/**
 * @brief Singleton instance of the null expression node.
 * 
 * Since null is a constant value with no state, we use a single shared instance
 * rather than creating new nodes for each occurrence in the syntax tree.
 */
static null_t instance = {
    .base = {
        .base = {
            .vtbl = &null_vtbl
        }
    }
};

node_t *get_null_node_instance() {
    return &instance.base.base;
}
