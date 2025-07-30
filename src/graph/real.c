/**
 * @file real.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of the real number literal expression.
 * 
 * This file defines the behavior of real (floating-point) literal expressions,
 * which represent 64-bit IEEE double-precision values in the abstract syntax tree (AST).
 * Real literals are immutable and directly correspond to numeric constants in the source code.
 */

#include "expression.h"
#include "common_methods.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "lib/split64.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/source_builder.h"

/**
 * @struct real_t
 * @brief Represents a real number literal expression node.
 * 
 * This structure defines a real number (floating-point) literal expression
 * in the syntax tree. It extends `expression_t` and stores a 64-bit `double` value.
 */
typedef struct {
    /**
     * @brief Base expression structure from which integer_t inherits.
     */
    expression_t base;

    /**
     * @brief The 64-bit signed integer value.
     */
    double value;
} real_t;

/**
 * @brief Converts a real number expression to its string representation.
 * 
 * Converts the stored floating-point value to a string, as it would appear
 * in the Goat source code.
 * 
 * @param node A pointer to the real number expression node.
 * @return A `string_value_t` containing the formatted floating-point string.
 */
static string_value_t get_data_and_generate_goat_code(const node_t *node) {
    const real_t *expr = (const real_t *)node;
    return format_string(L"%f", expr->value);
}

/**
 * @brief Generates indented Goat source code for a real number literal.
 *
 * Converts the real value to a string and appends it to the output
 * with proper formatting and indentation.
 * 
 * @param node Pointer to the real number literal node.
 * @param builder Pointer to the source builder used for code generation.
 * @param indent Current indentation level (in tabs).
 */
static void generate_indented_goat_code(const node_t *node, source_builder_t *builder,
            size_t indent) {
    const real_t *expr = (const real_t *)node;
    append_formatted_source(builder, format_string(L"%f", expr->value));
}

/**
 * @brief Generates bytecode for a real number literal node.
 * 
 * Emits:
 * - An `ARG` instruction for the lower 32 bits of the `double` value.
 * - An `RLOAD` instruction for the upper 32 bits.
 * Together, they form the full 64-bit representation of the real number.
 * 
 * @param node A pointer to the real number literal node.
 * @param code A pointer to the code builder for instruction emission.
 * @param data Unused (present for compatibility).
 * @return Index of the first emitted instruction.
 */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code,
        data_builder_t *data) {
    const real_t *expr = (const real_t *)node;
    split64_t s;
    s.real_value = expr->value;
    instr_index_t first = add_instruction(
        code,
        (instruction_t){ .opcode = ARG, .arg1 = s.parts[0] }
    );
    //add_instruction(code, (instruction_t){ .opcode = RLOAD, .arg1 = s.parts[1] });
    return first;
}

/**
 * @brief Virtual table for real number literal expressions.
 * 
 * Implements core operations for real literals: string conversion,
 * bytecode generation, and source code regeneration.
 */
static node_vtbl_t real_vtbl = {
    .type = NODE_REAL,
    .type_name = L"real number",
    .get_data = get_data_and_generate_goat_code,
    .get_child_count = no_children,
    .get_child = no_child,
    .get_child_tag = no_tags,
    .generate_goat_code = get_data_and_generate_goat_code,
    .generate_indented_goat_code = generate_indented_goat_code,
    .generate_bytecode = generate_bytecode,
};

node_t *create_real_number_node(arena_t *arena, double value) {
    real_t *expr = (real_t *)alloc_from_arena(arena, sizeof(real_t));
    expr->base.base.vtbl = &real_vtbl;
    expr->value = value;
    return &expr->base.base;
}
