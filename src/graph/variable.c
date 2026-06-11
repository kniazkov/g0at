/**
 * @file variable.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of the variable expression node.
 * 
 * This file defines the behavior of the variable expression node, which represents
 * a variable by its name in the syntax tree. 
 */

#include <assert.h>

#include "variable.h"
#include "common_methods.h"
#include "statement.h"
#include "declarations.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "analysis/abstract_state.h"
#include "analysis/lattice.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/source_builder.h"

/**
 * @brief Gets the variable name as display data.
 *
 * Returns the variable name together with a display classification. Variables
 * resolved to the built-in declarator are marked as predefined names.
 *
 * @param node Pointer to the variable node.
 * @return Display value containing the variable name.
 */
static node_display_value_t get_data(const node_t *node) {
    const variable_t *expr = (const variable_t *)node;
    node_display_value_kind_t kind = NODE_DISPLAY_VALUE_PLAIN;
    if (expr->declarator &&
            expr->declarator->name.length > 0 &&
            expr->declarator->name.data[0] == L'*') {
        kind = NODE_DISPLAY_VALUE_PREDEFINED;
    }
    return (node_display_value_t){
        .text = VIEW_TO_VALUE(expr->name),
        .kind = kind
    };
}

/**
 * @brief Gets the number of nodes related to a variable node.
 *
 * A variable node may be related to the declarator that introduced the
 * referenced name. If semantic analysis has resolved the variable, this returns
 * 1; otherwise it returns 0.
 *
 * @param node Pointer to the variable node.
 * @return 1 if the variable has a resolved declarator, otherwise 0.
 */
static size_t get_related_count(const node_t *node) {
    const variable_t *expr = (const variable_t *)node;
    return expr->declarator ? 1 : 0;
}

/**
 * @brief Retrieves a related node from a variable node.
 *
 * For a resolved variable, index 0 returns the declarator node that introduced
 * the referenced name.
 *
 * @param node Pointer to the variable node.
 * @param index Zero-based related-node index.
 * @return Pointer to the related declarator node, or NULL if index is out of bounds
 *         or the variable has not been resolved.
 */
static const node_t *get_related(const node_t *node, size_t index) {
    const variable_t *expr = (const variable_t *)node;
    if (index == 0) {
        return &expr->declarator->base;
    }
    return NULL;
}

/**
 * @brief Gets the relation type for a related variable node.
 *
 * For index 0, returns RELATION_DECLARATION when the variable has been resolved
 * to a declarator. Otherwise returns RELATION_NONE.
 *
 * @param node Pointer to the variable node.
 * @param index Zero-based related-node index.
 * @return RELATION_DECLARATION for the resolved declarator relation, otherwise
 *         RELATION_NONE.
 */
static relation_type_t get_relation_type(const node_t *node, size_t index) {
    const variable_t *expr = (const variable_t *)node;
    if (index == 0 && expr->declarator) {
        return RELATION_DECLARATION;
    }
    return RELATION_NONE;
}

/**
 * @brief Calculates the abstract value of a variable expression.
 *
 * Looks up the variable's bound declarator in the current abstract state and
 * returns the current lattice value associated with it.
 *
 * If the declarator has no entry in the state yet, the variable is treated as
 * `null`: a null lattice element is created, stored in the state, and returned.
 * This gives unresolved-or-not-yet-written variables a concrete abstract fact
 * instead of leaving a delightful little NULL pointer trap for the next pass.
 *
 * @param node A pointer to the variable expression node.
 * @param state Current abstract state.
 * @param arena Memory arena used by the calculation interface. This
 *        implementation does not allocate from it directly.
 * @return Current abstract value of the variable.
 */
static const lattice_element_t *calculate(node_t *node, abstract_state_t *state, arena_t *arena) {
    const variable_t *expr = (const variable_t *)node;
    const lattice_element_t *value = get_from_abstract_state(state, expr->declarator);
    if (!value) {
        value = make_null_element();
        set_in_abstract_state(state, expr->declarator, value);
    }
    return value;
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
    .get_property_count = no_properties,
    .get_property = no_property,
    .get_child_count = no_children,
    .get_child = no_child,
    .get_child_tag = no_tags,
    .insert_child_before = no_child_insertion,
    .replace_child = no_child_replacement,
    .get_related_count = get_related_count,
    .get_related = get_related,
    .get_relation_type = get_relation_type,
    .calculate = calculate,
    .execute = execute_nothing,
    .generate_goat_code = generate_goat_code,
    .generate_indented_goat_code = generate_indented_goat_code,
    .generate_bytecode = generate_bytecode,
    .generate_bytecode_assign = generate_bytecode_assign
};

expression_t *create_variable_node(arena_t *arena, string_view_t name) {
    variable_t *expr = (variable_t *)alloc_zeroed_from_arena(arena, sizeof(variable_t));
    expr->base.base.base.vtbl = &variable_vtbl;
    expr->name = copy_string_to_arena(arena, name.data, name.length);
    return &expr->base.base;
}

declarator_spec_t *create_declarator_from_variable(const node_t *expr) {
    assert(expr->vtbl->type == NODE_VARIABLE);
    const variable_t *var = (variable_t *)expr;
    declarator_spec_t *decl = (declarator_spec_t*)ALLOC(sizeof(declarator_spec_t));
    decl->name = var->name;
    decl->initial = NULL;
    return decl;
}
