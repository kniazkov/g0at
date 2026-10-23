/** @file inheritance.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Evaluates prototypes, enters a derived object and runs its constructor.
 */
#include "inheritance.h"

#include "analysis/reachability.h"
#include "array.h"
#include "codegen/code_builder.h"
#include "codegen/source_builder.h"
#include "common_methods.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/linked_list.h"
#include "lib/string_ext.h"
#include "statement.h"

/** @brief Constructor has its own object scope; prototypes evaluate in the enclosing scope. */
typedef struct {
    expression_t base;
    expression_t *prototype;
    statement_list_t *constructor;
} inheritance_t;

static size_t get_child_count(const node_t *node) {
    return 2;
}

static node_t *get_child(const node_t *node, size_t index) {
    const inheritance_t *expr = (const inheritance_t *)node;
    return index == 0 ? (node_t *)expr->prototype : index == 1 ? (node_t *)expr->constructor : NULL;
}

static bool replace_child(node_t *node, node_t *old, node_t *replacement) {
    inheritance_t *expr = (inheritance_t *)node;
    if (old == (node_t *)expr->prototype && is_expression(replacement->vtbl->type)) {
        expr->prototype = (expression_t *)replacement;
        return true;
    }
    if (old == (node_t *)expr->constructor && replacement->vtbl->type == NODE_STATEMENT_LIST) {
        expr->constructor = (statement_list_t *)replacement;
        return true;
    }
    return false;
}

static string_value_t generate_goat_code(const node_t *node) {
    const inheritance_t *expr = (const inheritance_t *)node;
    string_builder_t builder = {0};
    bool list = expr->prototype->base.vtbl->type == NODE_PROTOTYPE_LIST;
    if (!list)
        append_char(&builder, U'(');
    string_value_t text = generate_goat_code_from_expression(expr->prototype);
    append_string_value(&builder, text);
    FREE_STRING(text);
    if (!list)
        append_char(&builder, U')');
    append_static_string(&builder, U" -> ");
    text = generate_goat_code_from_expression(&expr->constructor->base);
    string_value_t result = append_string_value(&builder, text);
    FREE_STRING(text);
    return result;
}

static void
generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    append_formatted_source(builder, generate_goat_code(node));
}

static instr_index_t generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    inheritance_t *expr = (inheritance_t *)node;
    instr_index_t first = get_next_instruction_index(code);
    bool list = expr->prototype->base.vtbl->type == NODE_PROTOTYPE_LIST;
    size_t count = list ? ((const array_literal_t *)expr->prototype)->count : 1;
    generate_bytecode_from_expression(expr->prototype, code, data);
    add_instruction(code, (instruction_t){.opcode = INHERIT, .arg0 = list, .arg1 = count});
    for (list_item_t *item = expr->constructor->statements->head; item; item = item->next)
        generate_bytecode_from_statement((statement_t *)item->value.ptr, code, data);
    add_instruction(code, (instruction_t){.opcode = LEAVE});
    return first;
}

static const lattice_element_t *calculate(node_t *node, abstract_state_t *state, arena_t *arena) {
    inheritance_t *expr = (inheritance_t *)node;
    calculate_expression(expr->prototype, state, arena);
    forget_abstract_values(state);
    calculate_expression(&expr->constructor->base, state, arena);
    forget_abstract_values(state);
    return make_user_defined_object_element();
}

static const lattice_element_t *
analyze_reachability(node_t *node, abstract_state_t **state, analysis_collector_t *collector) {
    forget_abstract_values(*state);
    visit_reachable_children(node, state, collector);
    forget_abstract_values(*state);
    return make_user_defined_object_element();
}

static node_vtbl_t vtbl = {.type = NODE_INHERITANCE,
                           .type_name = U"inheritance",
                           .is_assignable_expression = false,
                           .analyze_reachability = analyze_reachability,
                           .is_pure = not_pure,
                           .simplify = no_simplification,
                           .collect_direct_effects = unknown_direct_effects,
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
                           .generate_bytecode_assign = no_bytecode_assignment,
                           .can_generate_c_code = cannot_generate_c_code,
                           .generate_c_code = no_c_code,
                           .generate_indented_c_code = no_indented_c_code,
                           .generate_bytecode_deferred = no_deferred_bytecode};

node_t *
create_inheritance_node(arena_t *arena, expression_t *prototype, statement_list_t *constructor) {
    inheritance_t *expr = alloc_zeroed_from_arena(arena, sizeof(*expr));
    expr->base.base.vtbl = &vtbl;
    expr->prototype = prototype;
    expr->constructor = constructor;
    return &expr->base.base;
}
