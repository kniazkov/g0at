/**
 * @file declarations.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of variable and constant declaration nodes.
 * 
 * This file defines the behavior of declaration nodes in the abstract syntax tree (AST):
 * - Variable declarations (var x, y = 1, z = 2 + 3)
 * - Constant declarations (const pi = 3.14)
 * 
 * Declaration nodes are container statements that hold:
 * 1. For variables: One or more variable declarators (identifier + optional initializer)
 * 2. For constants: One or more constant declarators (identifier + required initializer)
 * 
 * The implementation handles both the syntactic structure and semantic rules:
 * - Multiple declarations in single statement
 * - Optional vs required initializers
 * - Scope management for declared identifiers
 */

#include <assert.h>

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
 * @struct variable_declarator_t
 * @brief Represents a single variable declarator in a declaration statement.
 *
 * This structure defines a node that represents one variable declarator within
 * a variable declaration statement (e.g., "x" or "y = 42" in "var x, y = 42").
 * It contains the variable name and an optional initializer expression.
 */
typedef struct {
    /**
     * @brief Base node structure.
     */
    node_t base;

    /**
     * @brief The name of the variable being declared.
     */
    wchar_t *name;

    /**
     * @brief The length of the variable name in characters.
     */
    size_t name_length;

    /**
     * @brief Optional initializer expression for the variable.
     * 
     * If not NULL, this expression will be evaluated and its result will
     * become the initial value of the variable.
     */
    expression_t *initial;
} variable_declarator_t;

/**
 * @brief Gets the variable name as string data.
 * 
 * This function provides access to the variable name stored in the declarator node.
 * 
 * @param node Pointer to the variable declarator node.
 * @return `string_value_t` containing the variable name.
 */
static string_value_t vdeclr_get_data(const node_t *node) {
    const variable_declarator_t *decl = (const variable_declarator_t *)node;
    return (string_value_t){ decl->name, decl->name_length, false };
}

/**
 * @brief Gets the child count for variable declarator node.
 * 
 * Returns 1 if the declarator has an initializer expression, 0 otherwise.
 * 
 * @param node Pointer to the variable declarator node.
 * @return 1 if initializer exists, 0 otherwise.
 */
static size_t vdeclr_get_child_count(const node_t *node) {
    variable_declarator_t *decl = (variable_declarator_t*)node;
    return decl->initial == NULL ? 0 : 1;
}

/**
 * @brief Retrieves the initializer expression node.
 * 
 * Provides access to the initializer expression if it exists.
 * 
 * @param node Pointer to the variable declarator node.
 * @param index Must be 0 to get the initializer expression.
 * @return Pointer to the initializer expression node or NULL.
 */
static const node_t* vdeclr_get_child(const node_t *node, size_t index) {
    const variable_declarator_t* decl = (const variable_declarator_t*)node;
    if (index == 0 && decl->initial) {
        return &decl->initial->base;
    }
    return NULL;
}

/**
 * @brief Gets child tag for variable declarator.
 * 
 * Returns "initial" for the initializer expression if it exists.
 * 
 * @param node Pointer to the node (unused).
 * @param index Must be 0 to get tag.
 * @return Static wide string "initial" or NULL if index != 0 or no initializer.
 */
static const wchar_t* vdeclr_get_child_tag(const node_t *node, size_t index) {
    const variable_declarator_t* decl = (const variable_declarator_t*)node;
    if (index == 0 && decl->initial) {
        return L"initial";
    }
    return NULL;
}

/**
 * @brief Generates Goat source code for a variable declarator.
 * 
 * Produces either just the variable name (if no initializer) or "name = value" format
 * for initialized variables.
 * 
 * @param node Pointer to the variable declarator node.
 * @return `string_value_t` containing the generated code.
 */
static string_value_t vdeclr_generate_goat_code(const node_t *node) {
    const variable_declarator_t* decl = (const variable_declarator_t*)node;
    if (decl->initial) {
        string_builder_t builder;
        string_value_t initial_as_string =
            decl->initial->base.vtbl->generate_goat_code(&decl->initial->base);
        init_string_builder(&builder, decl->name_length + 3 + initial_as_string.length);
        append_substring(&builder, decl->name, decl->name_length);
        append_substring(&builder, L" = ", 3);
        string_value_t value = append_substring(&builder, initial_as_string.data,
               initial_as_string.length);
        if (initial_as_string.should_free) {
            FREE(initial_as_string.data);
        }
        return value;
    } else {
        return (string_value_t){ decl->name, decl->name_length, false };
    }
}

/**
 * @brief Generates bytecode for a variable declarator.
 * 
 * Produces bytecode that:
 * 1. Evaluates initializer (or pushes NIL if none)
 * 2. Pops the value (handled by declaration statement)
 * 3. Declares the variable in current scope
 * 
 * @param node Pointer to the variable declarator node.
 * @param code Pointer to the code builder for bytecode generation.
 * @param data Pointer to the data builder for string storage.
 */
static void vdeclr_generate_bytecode(const node_t *node, code_builder_t *code,
        data_builder_t *data) {
    const variable_declarator_t* decl = (const variable_declarator_t*)node;
    if (decl->initial) {
        decl->initial->base.vtbl->generate_bytecode(&decl->initial->base, code, data);
    } else {
        add_instruction(code, (instruction_t){ .opcode = NIL });
    }
    add_instruction(code, (instruction_t){ .opcode = POP });
    uint32_t index = add_string_to_data_segment_ex(data, decl->name, decl->name_length);
    add_instruction(code, (instruction_t){ .opcode = VAR, .arg1 = index });
}

/**
 * @brief Virtual table for variable declarator operations.
 * 
 * Provides implementations of operations specific to variable declarator nodes.
 */
static node_vtbl_t vdeclr_vtbl = {
    .type = NODE_VARIABLE_DECLARATOR,
    .type_name = L"variable declarator",
    .get_data = vdeclr_get_data,
    .get_child_count = vdeclr_get_child_count,
    .get_child = vdeclr_get_child,
    .get_child_tag = vdeclr_get_child_tag,
    .generate_goat_code = vdeclr_generate_goat_code,
    .generate_indented_goat_code = stub_indented_goat_code_generator,
    .generate_bytecode = vdeclr_generate_bytecode,
};

/**
 * @brief Creates a new variable declarator node.
 * 
 * Allocates and initializes a new variable declarator node with the given name
 * and optional initializer expression.
 * 
 * @param arena Arena allocator to use for node allocation.
 * @param spec Declarator specification.
 * @return Pointer to the newly created variable declarator node.
 */
variable_declarator_t *create_variable_declarator_node(arena_t *arena,
        const declarator_t *spec) {
    variable_declarator_t *decl = 
        (variable_declarator_t *)alloc_from_arena(arena, sizeof(variable_declarator_t));
    decl->base.vtbl = &vdeclr_vtbl;
    decl->name = copy_string_to_arena(arena, spec->name, spec->name_length);
    decl->name_length = spec->name_length;
    decl->initial = spec->initial;
    return decl;
}

/**
 * @struct variable_declaration_t
 * @brief Represents a variable declaration statement containing multiple declarators.
 *
 * This structure defines a node that represents a complete variable declaration statement
 * (e.g., "var x, y = 1, z = 2 + 3"). It serves as a container for one or more variable
 * declarators (variable_declarator_t) and handles their collective behavior.
 */
typedef struct {
    /**
     * @brief Base statement structure.
     * 
     * Provides common statement functionality and allows this structure to be treated
     * as a statement node in the abstract syntax tree.
     */
    statement_t base;

    /**
     * @brief Array of variable declarators.
     * 
     * A dynamically allocated array of pointers to `variable_declarator_t` nodes,
     * each representing a single variable declaration within this statement.
     * The memory is allocated from the arena.
     */
    variable_declarator_t **decl_list;

    /**
     * @brief Count of variable declarators.
     * 
     * Specifies the number of variable declarators in the decl_list array.
     * Must be at least 1 (empty declarations are not valid).
     */
    size_t decl_count;
} variable_declaration_t;

/**
 * @brief Gets the child count for a variable declaration node.
 * 
 * Returns the number of variable declarators contained within this declaration statement.
 * 
 * @param node Pointer to the variable declaration node.
 * @return The count of child declarator nodes (always >= 1).
 */
static size_t vdecln_get_child_count(const node_t *node) {
    const variable_declaration_t* root = (const variable_declaration_t*)node;
    return root->decl_count;
}

/**
 * @brief Retrieves a specific declarator child node.
 * 
 * Provides access to individual variable declarators within this declaration statement.
 * 
 * @param node Pointer to the variable declaration node.
 * @param index Zero-based index of the declarator to retrieve.
 * @return Pointer to the requested declarator node, or NULL if index is out of bounds.
 */
static const node_t* vdecln_get_child(const node_t *node, size_t index) {
    const variable_declaration_t* root = (const variable_declaration_t*)node;
    if (index >= root->decl_count) {
        return NULL;
    }
    return &root->decl_list[index]->base;
}

/**
 * @brief Generates Goat source code for a variable declaration statement.
 * 
 * Produces the textual representation of a variable declaration including all its
 * declarators (e.g., "x, y = 1, z = 2 + 3" for "var x, y = 1, z = 2 + 3").
 * 
 * @param node Pointer to the variable declaration node.
 * @return string_value_t containing the generated code. The caller is responsible
 *         for freeing the memory if should_free is true.
 */
static string_value_t vdecln_generate_goat_code(const node_t *node) {
    const variable_declaration_t* decl = (const variable_declaration_t*)node;
    string_builder_t builder;
    init_string_builder(&builder, 0);
    string_value_t result = { L"", 0, false };
    
    for (size_t index = 0; index < decl->decl_count; index++) {
        if (index > 0) {
            append_substring(&builder, L", ", 2);
        }
        variable_declarator_t *vdr = decl->decl_list[index];
        string_value_t vdr_as_string = vdr->base.vtbl->generate_goat_code(&vdr->base);
        result = append_substring(&builder, vdr_as_string.data, vdr_as_string.length);
        if (vdr_as_string.should_free) {
            FREE(vdr_as_string.data);
        }
    }
    return result;
}

/**
 * @brief Generates indented Goat source code for variable declarations.
 * 
 * Produces properly formatted source code with correct indentation for variable
 * declaration statements, including all declarators.
 * 
 * @param node Pointer to the variable declaration node.
 * @param builder Pointer to the source builder for output.
 * @param indent Number of indentation tabs to apply.
 * @return true if code was successfully generated, false otherwise.
 */
static bool vdecln_generate_indented_goat_code(const node_t *node, 
        source_builder_t *builder, size_t indent) {
    string_value_t line = vdecln_generate_goat_code(node);
    if (line.length == 0) {
        return false;
    }
    add_formatted_line_of_source_code(builder, indent, line);
    return true;
}

/**
 * @brief Generates bytecode for variable declarations.
 * 
 * Emits bytecode that:
 * 1. Evaluates initializers (if any)
 * 2. Declares variables in current scope
 * 3. Handles proper stack management
 * 
 * @param node Pointer to the variable declaration node.
 * @param code Pointer to the code builder for bytecode output.
 * @param data Pointer to the data builder for string storage.
 */
static void vdecln_generate_bytecode(const node_t *node, 
        code_builder_t *code, data_builder_t *data) {
    const variable_declaration_t* decl = (const variable_declaration_t*)node;
    for (size_t index = 0; index < decl->decl_count; index++) {
        variable_declarator_t *vdr = decl->decl_list[index];
        vdr->base.vtbl->generate_bytecode(&vdr->base, code, data);
    }
}

/**
 * @brief Virtual table for variable declaration nodes.
 * 
 * Contains function pointers implementing all operations for variable declaration
 * statements in the abstract syntax tree.
 */
static node_vtbl_t vdecln_vtbl = {
    .type = NODE_VARIABLE_DECLARATION,
    .type_name = L"variable declaration",
    .get_data = no_data,
    .get_child_count = vdecln_get_child_count,
    .get_child = vdecln_get_child,
    .get_child_tag = no_tags,
    .generate_goat_code = vdecln_generate_goat_code,
    .generate_indented_goat_code = vdecln_generate_indented_goat_code,
    .generate_bytecode = vdecln_generate_bytecode,
};

/**
 * @brief Creates a new variable declaration AST node.
 * 
 * Constructs a complete variable declaration statement node containing one or more
 * variable declarators. The node owns the declarator list and its contents.
 * 
 * @param arena Arena allocator for memory management.
 * @param decl_list Array of declarator specifications.
 * @param decl_count Number of declarators (must be > 0).
 * @return Pointer to the newly created variable declaration node.
 * 
 * @note The created node takes ownership of the declarator list and its contents.
 */
node_t *create_variable_declaration_node(arena_t *arena, 
        declarator_t **decl_list, size_t decl_count) {
    assert(decl_count > 0);
    variable_declaration_t *node = (variable_declaration_t *)alloc_from_arena(
            arena, sizeof(variable_declaration_t));
    node->base.base.vtbl = &vdecln_vtbl;
    node->decl_list = (variable_declarator_t **)alloc_from_arena(arena,
            decl_count * sizeof(variable_declarator_t *));
    node->decl_count = decl_count;
    
    for (size_t index = 0; index < decl_count; index++) {
        node->decl_list[index] = create_variable_declarator_node(arena, decl_list[index]);
    }
    
    return &node->base.base;
}
