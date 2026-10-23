/**
 * @file member_access.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of the member access expression node.
 */

#include "member_access.h"

#include "analysis/abstract_state.h"
#include "codegen/c_string_methods.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/source_builder.h"
#include "common_methods.h"
#include "goat/unicode.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "replacement.h"

/** @brief Implements @ref node_vtbl_t::get_data. */
static node_display_value_t get_data(const node_t *node) {
    const member_access_t *expr = (const member_access_t *)node;
    return (node_display_value_t){.text = VIEW_TO_VALUE(expr->name),
                                  .kind = NODE_DISPLAY_VALUE_PLAIN};
}

/** @brief Implements @ref node_vtbl_t::get_child_count. */
static size_t get_child_count(const node_t *node) {
    return 1;
}

/** @brief Implements @ref node_vtbl_t::get_child. */
static node_t *get_child(const node_t *node, size_t index) {
    const member_access_t *expr = (const member_access_t *)node;
    return index == 0 ? (node_t *)expr->object : NULL;
}

/** @brief Implements @ref node_vtbl_t::replace_child. */
static bool replace_child(node_t *node, node_t *old_child, node_t *new_child) {
    member_access_t *owner = (member_access_t *)node;
    if ((node_t *)owner->object != old_child || !is_expression(new_child->vtbl->type))
        return false;
    owner->object = (expression_t *)new_child;
    return true;
}

/** @brief Keep a numeric receiver separate from the member-access dot when reparsing. */
static bool numeric_receiver(const member_access_t *expr) {
    node_type_t type = replacement_result(&expr->object->base)->vtbl->type;
    return type == NODE_INTEGER || type == NODE_REAL;
}

/** @brief Implements @ref node_vtbl_t::generate_goat_code. */
static string_value_t generate_goat_code(const node_t *node) {
    const member_access_t *expr = (const member_access_t *)node;
    string_value_t object = generate_goat_code_from_expression(expr->object);
    string_value_t result =
        format_string(numeric_receiver(expr) ? U"(%s).%s" : U"%s.%s", object.data, expr->name.data);
    FREE_STRING(object);
    return result;
}

/** @brief Implements @ref node_vtbl_t::generate_indented_goat_code. */
static void
generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    const member_access_t *expr = (const member_access_t *)node;
    if (numeric_receiver(expr))
        append_static_source(builder, U"(");
    generate_indented_goat_code_from_expression(expr->object, builder, indent);
    if (numeric_receiver(expr))
        append_static_source(builder, U")");
    append_static_source(builder, U".");
    append_formatted_source(builder, VIEW_TO_VALUE(expr->name));
}

/** @brief Implements @ref node_vtbl_t::generate_bytecode. */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    const member_access_t *expr = (const member_access_t *)node;
    instr_index_t first = generate_bytecode_from_expression(expr->object, code, data);
    uint32_t index = add_string_to_data_segment_ex(data, expr->name);
    add_instruction(code, (instruction_t){.opcode = GETPROP, .arg1 = index});
    return first;
}

/**
 * @brief Generates bytecode for storing a value into a named property.
 *
 * The value is already on the stack; this emits the object expression followed by `SETPROP`.
 */
static instr_index_t
generate_bytecode_assign(const node_t *node, code_builder_t *code, data_builder_t *data) {
    const member_access_t *expr = (const member_access_t *)node;
    instr_index_t first = generate_bytecode_from_expression(expr->object, code, data);
    uint32_t index = add_string_to_data_segment_ex(data, expr->name);
    add_instruction(code, (instruction_t){.opcode = SETPROP, .arg1 = index});
    return first;
}

/** @brief The immutable string length property, in host gchar_t units. */
static const lattice_element_t *calculate(node_t *node, abstract_state_t *state, arena_t *arena) {
    const member_access_t *member = (const member_access_t *)node;
    const lattice_element_t *value = calculate_expression(member->object, state, arena);
    if (!STRCMP(member->name.data, U"length")) {
        if (value->type == LATTICE_STRING_CONSTANT)
            return make_integer_constant_element(
                arena,
                ((const string_constant_element_t *)value)->value.length);
        if (value->type == LATTICE_STRING)
            return make_integer_element();
    }
    return make_top_element();
}

static bool can_generate_c_code(const node_t *node,
                                const lattice_element_t *value,
                                const c_expression_context_t *context) {
    const member_access_t *member = (const member_access_t *)node;
    return !STRCMP(member->name.data, U"length")
           && c_expression_type(context, (const node_t *)member->object) == C_VALUE_STRING;
}

/** @brief Virtual table for member access expressions. */
static node_vtbl_t member_access_vtbl = {
    .type = NODE_MEMBER_ACCESS,
    .analyze_reachability = visit_reachable_children,
    .is_pure = children_are_pure,
    .simplify = no_simplification,
    .collect_direct_effects = collect_child_effects,
    .type_name = U"member access",
    .is_assignable_expression = true,
    .get_data = get_data,
    .get_property_count = no_properties,
    .get_property = no_property,
    .get_child_count = get_child_count,
    .get_child = get_child,
    .get_child_tag = no_tags,
    .insert_child_before = no_child_insertion,
    .replace_child = replace_child,
    .get_related_count = no_related_nodes,
    .get_related = no_related_node,
    .get_relation_type = no_relation_type,
    .calculate = calculate,
    .execute = execute_nothing,
    .generate_goat_code = generate_goat_code,
    .generate_indented_goat_code = generate_indented_goat_code,
    .generate_bytecode = generate_bytecode,
    .generate_bytecode_assign = generate_bytecode_assign,
    .can_generate_c_code = can_generate_c_code,
    .generate_c_code = c_string_length,
    .generate_indented_c_code = no_indented_c_code,
    .generate_bytecode_deferred = no_deferred_bytecode,
};

expression_t *create_member_access_node(arena_t *arena, expression_t *object, string_view_t name) {
    member_access_t *expr =
        (member_access_t *)alloc_zeroed_from_arena(arena, sizeof(member_access_t));
    expr->base.base.base.vtbl = &member_access_vtbl;
    expr->object = object;
    expr->name = copy_string_to_arena(arena, name.data, name.length);
    return &expr->base.base;
}
