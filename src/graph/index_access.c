/** @file index_access.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Indexed reads and writes; receivers and indices are evaluated once in source order.
 */
#include "index_access.h"

#include "analysis/abstract_state.h"
#include "analysis/function_effects.h"
#include "analysis/reachability.h"
#include "codegen/c_lowering.h"
#include "codegen/code_builder.h"
#include "codegen/source_builder.h"
#include "common_methods.h"
#include "lib/memory/allocate.h"
#include "lib/memory/arena.h"
#include "lib/strings/string_ext.h"
#include "replacement.h"
#include "variable.h"

/** @brief Includes the receiver followed by every index. */
static size_t get_child_count(const node_t *node) {
    return ((const index_access_t *)node)->count + 1;
}

/** @brief Returns one receiver/index child. */
static node_t *get_child(const node_t *node, size_t index) {
    const index_access_t *expr = (const index_access_t *)node;
    return index == 0             ? (node_t *)expr->object
           : index <= expr->count ? (node_t *)expr->indices[index - 1]
                                  : NULL;
}

/** @brief Replaces one receiver/index expression. */
static bool replace_child(node_t *node, node_t *old, node_t *replacement) {
    index_access_t *expr = (index_access_t *)node;
    if (!is_expression(replacement->vtbl->type))
        return false;
    if ((node_t *)expr->object == old) {
        expr->object = (expression_t *)replacement;
        return true;
    }
    for (size_t i = 0; i < expr->count; i++) {
        if ((node_t *)expr->indices[i] == old) {
            expr->indices[i] = (expression_t *)replacement;
            return true;
        }
    }
    return false;
}

/** @brief Generates parseable source. */
static string_value_t generate_goat_code(const node_t *node) {
    const index_access_t *expr = (const index_access_t *)node;
    string_builder_t builder = {0};
    string_value_t object = generate_goat_code_from_expression(expr->object);
    append_string_value(&builder, object);
    FREE_STRING(object);
    append_char(&builder, U'[');
    for (size_t i = 0; i < expr->count; i++) {
        if (i)
            append_static_string(&builder, U", ");
        string_value_t index = generate_goat_code_from_expression(expr->indices[i]);
        append_string_value(&builder, index);
        FREE_STRING(index);
    }
    return append_char(&builder, U']');
}

/** @brief Emits formatted source. */
static void
generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    append_formatted_source(builder, generate_goat_code(node));
}

/** @brief Emits receiver and indices, retaining the receiver only for CALLM. */
static instr_index_t operands(const index_access_t *expr,
                              code_builder_t *code,
                              data_builder_t *data,
                              bool retain_receiver) {
    instr_index_t first = generate_bytecode_from_expression(expr->object, code, data);
    if (retain_receiver)
        add_instruction(code, (instruction_t){.opcode = DUP});
    for (size_t i = 0; i < expr->count; i++)
        generate_bytecode_from_expression(expr->indices[i], code, data);
    return first;
}

instr_index_t generate_index_read(node_t *node,
                                  code_builder_t *code,
                                  data_builder_t *data,
                                  bool retain_receiver) {
    const index_access_t *expr = (const index_access_t *)node;
    instr_index_t first = operands(expr, code, data, retain_receiver);
    add_instruction(code, (instruction_t){.opcode = GETINDEX, .arg1 = expr->count});
    return first;
}

/** @brief Emits a read. */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    return generate_index_read(node, code, data, false);
}

/** @brief RHS is already on the stack, as with named property assignment. */
static instr_index_t
generate_bytecode_assign(const node_t *node, code_builder_t *code, data_builder_t *data) {
    const index_access_t *expr = (const index_access_t *)node;
    instr_index_t first = operands(expr, code, data, false);
    add_instruction(code, (instruction_t){.opcode = SETINDEX, .arg1 = expr->count});
    return first;
}

size_t arguments_literal_index(const node_t *node) {
    if (!node || node->vtbl->type != NODE_INDEX_ACCESS)
        return SIZE_MAX;
    const index_access_t *index = (const index_access_t *)node;
    const node_t *object = replacement_original((const node_t *)index->object);
    while (object->vtbl->type == NODE_EXPRESSION_PARENTHESIZED)
        object = replacement_original(get_node_child(object, 0));
    if (object->vtbl->type != NODE_VARIABLE
        || !is_arguments_name(((const variable_t *)object)->name) || index->count != 1)
        return SIZE_MAX;
    node_t *slot = (node_t *)replacement_original((const node_t *)index->indices[0]);
    while (slot->vtbl->type == NODE_EXPRESSION_PARENTHESIZED)
        slot = (node_t *)replacement_original(get_node_child(slot, 0));
    if (slot->vtbl->type != NODE_INTEGER)
        return SIZE_MAX;
    const integer_constant_element_t *value =
        (const integer_constant_element_t *)slot->vtbl->calculate(slot, NULL, NULL);
    return value->value >= 0 && (uint64_t)value->value < SIZE_MAX ? (size_t)value->value : SIZE_MAX;
}

static bool can_generate_c_code(const node_t *node,
                                const lattice_element_t *value,
                                const c_expression_context_t *context) {
    size_t slot = arguments_literal_index(node);
    if (!context || !context->summary || slot >= context->summary->parameter_count)
        return false;
    c_value_type_t type = classify_c_value_type(context->summary->parameter_types[slot]->type);
    return type == C_VALUE_INT64 || type == C_VALUE_DOUBLE || type == C_VALUE_BOOL
           || type == C_VALUE_CHAR || type == C_VALUE_STRING;
}

static c_generated_expression_t generate_c_code(const node_t *node,
                                                c_generation_context_t *context) {
    c_expression_context_t proof = {.summary = context->summary};
    size_t slot = arguments_literal_index(node);
    if (!context->module_definition || !can_generate_c_code(node, NULL, &proof)) {
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    c_value_type_t type = c_generation_parameter_type(context, slot);
    const gchar_t *field = type == C_VALUE_INT64    ? U"integer"
                           : type == C_VALUE_DOUBLE ? U"real"
                           : type == C_VALUE_BOOL   ? U"boolean"
                           : type == C_VALUE_CHAR   ? U"character"
                                                    : U"string";
    return (c_generated_expression_t){.success = true,
                                      .type = type,
                                      .value = format_string(U"%sg_arguments[%zu].value.%s",
                                                             type == C_VALUE_STRING ? U"*" : U"",
                                                             slot,
                                                             field)};
}

static void
collect_direct_effects(const node_t *node, function_summary_t *summary, arena_t *arena) {
    c_expression_context_t proof = {.summary = summary};
    if (can_generate_c_code(node, NULL, &proof))
        collect_child_effects(node, summary, arena);
    else
        unknown_direct_effects(node, summary, arena);
}

/** @brief Indexing can invoke arbitrary type behavior, so invalidate mutable facts. */
static const lattice_element_t *calculate(node_t *node, abstract_state_t *state, arena_t *arena) {
    const index_access_t *index = (const index_access_t *)node;
    size_t slot = arguments_literal_index(node);
    if (can_generate_c_code(node, NULL, state->c_expressions))
        return state->c_expressions->summary->parameter_types[slot];
    calculate_expression(index->object, state, arena);
    for (size_t i = 0; i < index->count; i++)
        calculate_expression(index->indices[i], state, arena);
    /* Module/file scopes are user objects too: even constant-key indexing can
     * call a handler, mutate captured values and return an unrelated value. */
    forget_abstract_values(state);
    return make_top_element();
}

/** @brief Visits receiver/indices in evaluation order and invalidates mutable facts. */
static const lattice_element_t *
analyze_reachability(node_t *node, abstract_state_t **state, analysis_collector_t *collector) {
    const index_access_t *index = (const index_access_t *)node;
    visit_reachable_node((node_t *)index->object, state, collector);
    for (size_t i = 0; i < index->count; i++)
        visit_reachable_node((node_t *)index->indices[i], state, collector);
    forget_abstract_values(*state);
    return make_top_element();
}

/** @brief Virtual table for index expressions. */
static node_vtbl_t vtbl = {.type = NODE_INDEX_ACCESS,
                           .type_name = U"index access",
                           .is_assignable_expression = true,
                           .analyze_reachability = analyze_reachability,
                           .is_pure = not_pure,
                           .simplify = no_simplification,
                           .collect_direct_effects = collect_direct_effects,
                           .get_data = no_data,
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
                           .generate_c_code = generate_c_code,
                           .generate_indented_c_code = no_indented_c_code,
                           .generate_bytecode_deferred = no_deferred_bytecode};

node_t *create_index_access_node(arena_t *arena, expression_t *object) {
    index_access_t *expr = alloc_zeroed_from_arena(arena, sizeof(*expr));
    expr->base.base.base.vtbl = &vtbl;
    expr->object = object;
    return &expr->base.base.base;
}

void set_index_arguments(node_t *node, arena_t *arena, expression_t **indices, size_t count) {
    index_access_t *expr = (index_access_t *)node;
    expr->count = count;
    if (count) {
        expr->indices = alloc_from_arena(arena, count * sizeof(*indices));
        memcpy(expr->indices, indices, count * sizeof(*indices));
    }
}
