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
    string_view_t name;

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
    return VIEW_TO_VALUE(decl->name);
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
        init_string_builder(&builder, decl->name.length + 3 + initial_as_string.length);
        append_substring(&builder, decl->name.data, decl->name.length);
        append_substring(&builder, L" = ", 3);
        string_value_t value = append_substring(&builder, initial_as_string.data,
               initial_as_string.length);
        FREE_STRING(initial_as_string);
        return value;
    } else {
        return VIEW_TO_VALUE(decl->name);
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
    uint32_t index = add_string_to_data_segment_ex(data, decl->name.data, decl->name.length);
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
static variable_declarator_t *create_variable_declarator_node(arena_t *arena,
        const declarator_t *spec) {
    variable_declarator_t *decl = 
        (variable_declarator_t *)alloc_from_arena(arena, sizeof(variable_declarator_t));
    decl->base.vtbl = &vdeclr_vtbl;
    decl->name = spec->name;
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
    const variable_declaration_t* decl = (const variable_declaration_t*)node;
    if (index >= decl->decl_count) {
        return NULL;
    }
    return &decl->decl_list[index]->base;
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
    append_substring(&builder, L"var ", 4);
    for (size_t index = 0; index < decl->decl_count; index++) {
        if (index > 0) {
            append_substring(&builder, L", ", 2);
        }
        variable_declarator_t *vdr = decl->decl_list[index];
        string_value_t vdr_as_string = vdeclr_generate_goat_code(&vdr->base);
        append_substring(&builder, vdr_as_string.data, vdr_as_string.length);
        FREE_STRING(vdr_as_string);
    }
    return append_char(&builder, L';');
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
        vdeclr_generate_bytecode(&vdr->base, code, data);
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

node_t *create_variable_declaration_node(arena_t *arena, declarator_t **decl_list,
        size_t decl_count) {
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

/**
 * @struct constant_declarator_t
 * @brief Represents a constant declaration in the abstract syntax tree.
 *
 * This structure defines a node for constant declarations (e.g., "const pi = 3.14").
 * Unlike variables, constants must be initialized at declaration time and cannot be
 * modified afterward. The node stores both the constant name and its initializer
 * expression.
 */
typedef struct {
    /**
     * @brief Base node structure.
     * 
     * Provides common node functionality and allows this structure to be treated
     * as a node in the abstract syntax tree.
     */
    node_t base;

    /**
     * @brief The name of the constant being declared.
     */
    string_view_t name;

    /**
     * @brief Initializer expression for the constant.
     * 
     * Must be non-NULL as constants require initialization. The expression is
     * evaluated once at declaration time and its result becomes the immutable
     * value of the constant.
     */
    expression_t *initial;
} constant_declarator_t;

/**
 * @brief Gets the constant name as string data.
 * 
 * Provides access to the constant name stored in the declarator node.
 * 
 * @param node Pointer to the constant declarator node.
 * @return string_value_t containing the constant name.
 */
static string_value_t cdeclr_get_data(const node_t *node) {
    const constant_declarator_t *decl = (const constant_declarator_t *)node;
    return VIEW_TO_VALUE(decl->name);
}

/**
 * @brief Gets the child count for constant declarator node.
 * 
 * Returns 1 as constants always have an initializer expression.
 * 
 * @param node Pointer to the constant declarator node.
 * @return Always returns 1 (constant declarations require initializers).
 */
static size_t cdeclr_get_child_count(const node_t *node) {
    return 1;
}

/**
 * @brief Retrieves the initializer expression node.
 * 
 * Provides access to the constant's initializer expression.
 * 
 * @param node Pointer to the constant declarator node.
 * @param index Must be 0 to get the initializer expression.
 * @return Pointer to the initializer expression node or NULL if index != 0.
 */
static const node_t* cdeclr_get_child(const node_t *node, size_t index) {
    const constant_declarator_t* decl = (const constant_declarator_t*)node;
    if (index == 0) {
        return &decl->initial->base;
    }
    return NULL;
}

/**
 * @brief Gets child tag for constant declarator.
 * 
 * Returns "initial" for the initializer expression.
 * 
 * @param node Pointer to the node (unused).
 * @param index Must be 0 to get tag.
 * @return Static wide string "initial" or NULL if index != 0.
 */
static const wchar_t* cdeclr_get_child_tag(const node_t *node, size_t index) {
    if (index == 0) {
        return L"initial";
    }
    return NULL;
}

/**
 * @brief Generates Goat source code for a constant declarator.
 * 
 * Produces the textual representation of a constant declaration in the format
 * "name = value" (e.g., "pi = 3.14").
 * 
 * @param node Pointer to the constant declarator node.
 * @return string_value_t containing the generated code. The caller is responsible
 *         for freeing the memory if `should_free` is true.
 */
static string_value_t cdeclr_generate_goat_code(const node_t *node) {
    const constant_declarator_t* decl = (const constant_declarator_t*)node;
    string_builder_t builder;
    string_value_t initial_as_string =
        decl->initial->base.vtbl->generate_goat_code(&decl->initial->base);
    init_string_builder(&builder, decl->name.length + 3 + initial_as_string.length);
    append_substring(&builder, decl->name.data, decl->name.length);
    append_substring(&builder, L" = ", 3);
    string_value_t value = append_substring(&builder, initial_as_string.data,
            initial_as_string.length);
    FREE_STRING(initial_as_string);
    return value;
}

/**
 * @brief Generates bytecode for a constant declarator.
 * 
 * Produces bytecode that:
 * 1. Evaluates the initializer expression
 * 2. Pops the value from stack (handled by declaration statement)
 * 3. Declares the constant in current scope
 * 
 * @param node Pointer to the constant declarator node.
 * @param code Pointer to the code builder for bytecode output.
 * @param data Pointer to the data builder for string storage.
 */
static void cdeclr_generate_bytecode(const node_t *node, code_builder_t *code,
        data_builder_t *data) {
    const constant_declarator_t* decl = (const constant_declarator_t*)node;
    decl->initial->base.vtbl->generate_bytecode(&decl->initial->base, code, data);
    uint32_t index = add_string_to_data_segment_ex(data, decl->name.data, decl->name.length);
    add_instruction(code, (instruction_t){ .opcode = CONST, .arg1 = index });
}

/**
 * @brief Virtual table for constant declarator operations.
 * 
 * Provides implementations of operations specific to constant declarator nodes.
 */
static node_vtbl_t cdeclr_vtbl = {
    .type = NODE_CONSTANT_DECLARATOR,
    .type_name = L"constant declarator",
    .get_data = cdeclr_get_data,
    .get_child_count = cdeclr_get_child_count,
    .get_child = cdeclr_get_child,
    .get_child_tag = cdeclr_get_child_tag,
    .generate_goat_code = cdeclr_generate_goat_code,
    .generate_indented_goat_code = stub_indented_goat_code_generator,
    .generate_bytecode = cdeclr_generate_bytecode,
};

/**
 * @brief Creates a new constant declarator AST node.
 * 
 * Constructs a complete constant declarator node from a declarator specification.
 * 
 * @param arena Arena allocator for node allocation.
 * @param spec Pointer to declarator specification containing:
 *             - Name and length
 *             - Mandatory initializer expression
 * @return Pointer to newly created constant_declarator_t node.
 */
static constant_declarator_t *create_constant_declarator_node(arena_t *arena,
        const declarator_t *spec) {
    assert(spec->initial != NULL);
    
    constant_declarator_t *decl = 
        (constant_declarator_t *)alloc_from_arena(arena, sizeof(constant_declarator_t));
    decl->base.vtbl = &cdeclr_vtbl;
    decl->name = spec->name;
    decl->initial = spec->initial;
    return decl;
}

/**
 * @struct constant_declaration_t
 * @brief Represents a constant declaration statement in the abstract syntax tree.
 *
 * This structure defines a node that represents a complete constant declaration
 * statement (e.g., "const pi = 3.14, tau = 6.28"). It serves as a container for
 * one or more constant declarators and handles their collective behavior.
 * Unlike variables, all constants must be initialized at declaration time.
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
     * @brief Array of constant declarators.
     * 
     * An arena-allocated array of pointers to constant_declarator_t nodes,
     * each representing a single constant declaration within this statement.
     * The array always contains at least one element.
     */
    constant_declarator_t **decl_list;

    /**
     * @brief Count of constant declarators.
     * 
     * Specifies the number of constant declarators in the `decl_list` array.
     * The count is always positive (empty declarations are syntactically invalid).
     */
    size_t decl_count;
} constant_declaration_t;

/**
 * @brief Gets the child count for a constant declaration node.
 * 
 * Returns the number of constant declarators contained in this declaration.
 * 
 * @param node Pointer to the constant declaration node.
 * @return The count of child declarator nodes (always >= 1).
 */
static size_t cdecln_get_child_count(const node_t *node) {
    const constant_declaration_t* root = (const constant_declaration_t*)node;
    return root->decl_count;
}

/**
 * @brief Retrieves a specific constant declarator child node.
 * 
 * Provides access to individual constant declarators within this declaration.
 * 
 * @param node Pointer to the constant declaration node.
 * @param index Zero-based index of the declarator to retrieve.
 * @return Pointer to the constant declarator node, or NULL if index is invalid.
 */
static const node_t* cdecln_get_child(const node_t *node, size_t index) {
    const constant_declaration_t* decl = (const constant_declaration_t*)node;
    if (index >= decl->decl_count) {
        return NULL;
    }
    return &decl->decl_list[index]->base;
}

/**
 * @brief Generates Goat source code for a constant declaration.
 * 
 * Produces the textual representation of a constant declaration including all
 * its declarators (e.g., "const pi = 3.14, tau = 6.28").
 * 
 * @param node Pointer to the constant declaration node.
 * @return string_value_t containing the generated code. The caller is responsible
 *         for freeing the memory if should_free is true.
 */
static string_value_t cdecln_generate_goat_code(const node_t *node) {
    const constant_declaration_t* decl = (const constant_declaration_t*)node;
    string_builder_t builder;
    init_string_builder(&builder, 0);
    append_substring(&builder, L"const ", 6);
    for (size_t index = 0; index < decl->decl_count; index++) {
        if (index > 0) {
            append_substring(&builder, L", ", 2);
        }
        constant_declarator_t *cdr = decl->decl_list[index];
        string_value_t cdr_as_string = cdr->base.vtbl->generate_goat_code(&cdr->base);
        append_substring(&builder, cdr_as_string.data, cdr_as_string.length);
        FREE_STRING(cdr_as_string);
    }
    return append_char(&builder, L';');
}

/**
 * @brief Generates indented Goat source code for constant declarations.
 * 
 * Produces properly formatted source code with correct indentation for constant
 * declaration statements, including all declarators.
 * 
 * @param node Pointer to the constant declaration node.
 * @param builder Pointer to the source builder for output.
 * @param indent Number of indentation tabs to apply.
 * @return true if code was successfully generated, false on error.
 */
static bool cdecln_generate_indented_goat_code(const node_t *node, source_builder_t *builder,
        size_t indent) {
    string_value_t line = cdecln_generate_goat_code(node);
    if (line.length == 0) {
        return false;
    }
    add_formatted_line_of_source_code(builder, indent, line);
    return true;
}

/**
 * @brief Generates bytecode for constant declarations.
 * 
 * Emits bytecode that:
 * 1. Evaluates initializer expressions
 * 2. Declares constants in current scope
 * 3. Ensures immutability of declared values
 * 
 * @param node Pointer to the constant declaration node.
 * @param code Pointer to the code builder for bytecode output.
 * @param data Pointer to the data builder for string storage.
 */
static void cdecln_generate_bytecode(const node_t *node, 
        code_builder_t *code, data_builder_t *data) {
    const constant_declaration_t* decl = (const constant_declaration_t*)node;
    for (size_t index = 0; index < decl->decl_count; index++) {
        constant_declarator_t *cdr = decl->decl_list[index];
        cdr->base.vtbl->generate_bytecode(&cdr->base, code, data);
    }
}

/**
 * @brief Virtual table for constant declaration nodes.
 * 
 * Contains function pointers implementing all operations for constant declaration
 * statements in the abstract syntax tree.
 */
static node_vtbl_t cdecln_vtbl = {
    .type = NODE_CONSTANT_DECLARATION,
    .type_name = L"constant declaration",
    .get_data = no_data,
    .get_child_count = cdecln_get_child_count,
    .get_child = cdecln_get_child,
    .get_child_tag = no_tags,
    .generate_goat_code = cdecln_generate_goat_code,
    .generate_indented_goat_code = cdecln_generate_indented_goat_code,
    .generate_bytecode = cdecln_generate_bytecode,
};

node_t *create_constant_declaration_node(arena_t *arena, declarator_t **decl_list,
        size_t decl_count) {
    assert(decl_count > 0);
    constant_declaration_t *node = (constant_declaration_t *)alloc_from_arena(
            arena, sizeof(constant_declaration_t));
    node->base.base.vtbl = &cdecln_vtbl;
    node->decl_list = (constant_declarator_t **)alloc_from_arena(arena,
            decl_count * sizeof(constant_declarator_t *));
    node->decl_count = decl_count;
    
    for (size_t index = 0; index < decl_count; index++) {
        node->decl_list[index] = create_constant_declarator_node(arena, decl_list[index]);
    }
    
    return &node->base.base;
}
