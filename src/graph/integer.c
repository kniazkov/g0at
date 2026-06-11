/**
 * @file integer.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of the integer literal expression.
 * 
 * This file defines the behavior of the integer literal expression, which represents
 * a signed 64-bit integer value in the syntax tree. Integer literals are immutable
 * and directly correspond to numeric constants in the source code.
 */

#include "expression.h"
#include "common_methods.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "lib/split64.h"
#include "analysis/lattice.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/source_builder.h"

/**
 * @struct integer_t
 * @brief Represents an integer literal expression node.
 * 
 * This structure defines an integer literal expression in the syntax tree.
 * The structure extends `expression_t` and includes a field for storing
 * the 64-bit signed integer value.
 */
typedef struct {
    /**
     * @brief Base expression structure from which integer_t inherits.
     */
    expression_t base;

    /**
     * @brief Lattice element containing 64-bit signed integer value.
     */
    integer_constant_element_t element;
} integer_t;

/**
 * @brief Calculates the lattice value for an integer literal node.
 *
 * The node is expected to be an integer expression. The result is an exact
 * integer constant lattice element containing the literal value stored in the node.
 *
 * @param node A pointer to the integer literal node.
 * @param state Current abstract state.
 * @param arena Memory arena used to allocate the resulting lattice element.
 * @return Constant pointer to the calculated integer constant lattice element.
 */
static const lattice_element_t *calculate(node_t *node, abstract_state_t *state, arena_t *arena) {
    const integer_t *expr = (const integer_t *)node;
    return &expr->element.base;
}

/**
 * @brief Converts an integer expression to its Goat source representation.
 *
 * This function converts the given integer expression to its decimal string
 * representation as it would appear in the source code.
 *
 * @param node A pointer to the integer expression node.
 * @return A `string_value_t` containing the formatted decimal string.
 */
static string_value_t generate_goat_code(const node_t *node) {
    const integer_t *expr = (const integer_t *)node;
    return format_string(L"%ld", expr->element.value);
}

/**
 * @brief Gets the integer literal as display data.
 *
 * Returns the integer literal text together with the default display
 * classification.
 *
 * @param node Pointer to the integer literal node.
 * @return Display value containing the integer literal text.
 */
static node_display_value_t get_data(const node_t *node) {
    return (node_display_value_t){
        .text = generate_goat_code(node),
        .kind = NODE_DISPLAY_VALUE_PLAIN
    };
}

/**
 * @brief Generates indented Goat source code for an integer literal expression.
 *
 * This function implements the virtual method for generating Goat source code that represents
 * an integer literal. It converts the integer value to its string representation and outputs
 * it directly without any additional formatting or decorations.

 * @param node Pointer to the AST node representing the integer literal.
 * @param builder Pointer to the source builder where generated code will be stored.
 * @param indent The current indentation level (in tabs) for code generation.
 */
static void generate_indented_goat_code(const node_t *node, source_builder_t *builder,
            size_t indent) {
    const integer_t *expr = (const integer_t *)node;
    append_formatted_source(builder, format_string(L"%ld", expr->element.value));
}

/**
 * @brief Generates bytecode for an integer node.
 * 
 * This function generates bytecode for an integer node by emitting either:
 * - An `ILOAD32` instruction for values fitting in 32 bits (range [-2^31, 2^31-1])
 * - An `ILOAD64` instruction with `ARG` prefix for 64-bit values
 * 
 * @param node A pointer to the node representing an integer literal.
 * @param code A pointer to the `code_builder_t` structure for instruction generation.
 * @param data Unused parameter (kept for signature compatibility with other generators).
 * @return The instruction index of the first emitted instruction.
 */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code,
        data_builder_t *data) {
    const integer_t *expr = (const integer_t *)node;
    int64_t value = expr->element.value;
    instr_index_t first;
    if (value > INT32_MAX || value < INT32_MIN) {
        split64_t s;
        s.int_value = value;
        first = add_instruction(code, (instruction_t){ .opcode = ARG, .arg1 = s.parts[0] });
        add_instruction(code, (instruction_t){ .opcode = ILOAD64, .arg1 = s.parts[1] });
    } else {
        first = add_instruction(code, (instruction_t){ .opcode = ILOAD32, .arg1 = value });
    }
    return first;
}

/**
 * @brief Virtual table for integer expressions.
 * 
 * This virtual table provides the implementation of operations specific to integer
 * literal expressions, including code generation and string representation.
 */
static node_vtbl_t integer_vtbl = {
    .type = NODE_INTEGER,
    .type_name = L"integer",
    .get_data = get_data,
    .get_property_count = no_properties,
    .get_property = no_property,
    .get_child_count = no_children,
    .get_child = no_child,
    .get_child_tag = no_tags,
    .insert_child_before = no_child_insertion,
    .replace_child = no_child_replacement,
    .get_related_count = no_related_nodes,
    .get_related = no_related_node,
    .get_relation_type = no_relation_type,
    .calculate = calculate,
    .execute = execute_nothing,
    .generate_goat_code = generate_goat_code,
    .generate_indented_goat_code = generate_indented_goat_code,
    .generate_bytecode = generate_bytecode,
};

/**
 * @brief Built-in data type
 */
static data_type_t data_type = BUILT_IN_DATA_TYPE(L"int");

node_t *create_integer_node(arena_t *arena, int64_t value) {
    integer_t *expr = (integer_t *)alloc_zeroed_from_arena(arena, sizeof(integer_t));
    expr->base.base.vtbl = &integer_vtbl;
    expr->base.data_type = &data_type;
    expr->element.base.type = LATTICE_INTEGER_CONSTANT;
    expr->element.value = value;
    return &expr->base.base;
}
