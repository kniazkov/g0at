/** @file try_catch.c
 * @copyright 2026 Ivan Kniazkov
 * @brief A try statement and its named catch block.
 */
#include "analysis/abstract_state.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/source_builder.h"
#include "common_methods.h"
#include "declarations.h"
#include "expression.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"
#include "statement.h"
#include "statement_list.h"

#include <assert.h>

/** @brief Catch names belong to the graph arena; both children are borrowed AST nodes. */
typedef struct {
    statement_t base;
    statement_t *body;
    string_view_t exception_name;
    statement_list_t *handler;
    declarator_t *exception_declarator;
} try_catch_t;

/** @brief Implements @ref node_vtbl_t::get_data. */
static node_display_value_t get_data(const node_t *node) {
    const try_catch_t *stmt = (const try_catch_t *)node;
    return (node_display_value_t){VIEW_TO_VALUE(stmt->exception_name), NODE_DISPLAY_VALUE_PLAIN};
}

/** @brief Implements @ref node_vtbl_t::get_child_count. */
static size_t get_child_count(const node_t *node) {
    return 2;
}

/** @brief Implements @ref node_vtbl_t::get_child. */
static node_t *get_child(const node_t *node, size_t index) {
    const try_catch_t *stmt = (const try_catch_t *)node;
    return index == 0 ? &stmt->body->base : index == 1 ? &stmt->handler->base.base : NULL;
}

/** @brief Implements @ref node_vtbl_t::get_child_tag. */
static const wchar_t *get_child_tag(const node_t *node, size_t index) {
    return index == 0 ? L"try" : index == 1 ? L"catch" : NULL;
}

/** @brief Conservative placeholder until exceptional abstract states are implemented. */
static abstract_state_t *execute(node_t *node, abstract_state_t *state, arena_t *arena) {
    if (state->control_flow == FLOW_NORMAL)
        forget_abstract_values(state);
    return state;
}

/** @brief Implements @ref node_vtbl_t::generate_goat_code. */
static string_value_t generate_goat_code(const node_t *node) {
    const try_catch_t *stmt = (const try_catch_t *)node;
    string_value_t body = generate_goat_code_from_statement(stmt->body);
    string_value_t handler = generate_goat_code_from_expression(&stmt->handler->base);
    string_value_t result =
        format_string(L"try %s catch (%s) %s", body.data, stmt->exception_name.data, handler.data);
    FREE_STRING(body);
    FREE_STRING(handler);
    return result;
}

/** @brief Implements @ref node_vtbl_t::generate_indented_goat_code. */
static void
generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    const try_catch_t *stmt = (const try_catch_t *)node;
    add_static_source(builder, indent, L"try");
    const node_t *body = &stmt->body->base;
    if (body->vtbl->type == NODE_STATEMENT_EXPRESSION
        && get_node_child(body, 0)->vtbl->type == NODE_STATEMENT_LIST) {
        append_static_source(builder, L" ");
        generate_indented_goat_code_from_node(get_node_child(body, 0), builder, indent);
    } else {
        generate_indented_goat_code_from_statement(stmt->body, builder, indent + 1);
    }
    add_source(builder, indent, L"catch (%s) ", stmt->exception_name.data);
    generate_indented_goat_code_from_expression(&stmt->handler->base, builder, indent);
}

/** @brief Emits separate try/catch contexts, binding the thrown stack value in catch. */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    const try_catch_t *stmt = (const try_catch_t *)node;
    instr_index_t first = add_instruction(code, (instruction_t){.opcode = TRY});
    generate_bytecode_from_statement(stmt->body, code, data);
    add_instruction(code, (instruction_t){.opcode = RESTORE});
    instr_index_t skip = add_instruction(code, (instruction_t){.opcode = JUMP});
    get_instruction(code, first)->arg1 = get_next_instruction_index(code);
    add_instruction(code, (instruction_t){.opcode = ENTER});
    add_instruction(
        code,
        (instruction_t){.opcode = VAR,
                        .arg1 = add_string_to_data_segment(data, stmt->exception_name.data)});
    /* The catch block shares the context that contains the exception binding. */
    for (size_t i = 0; i < get_node_child_count(&stmt->handler->base.base); i++)
        generate_bytecode_from_node(get_node_child(&stmt->handler->base.base, i), code, data);
    add_instruction(code, (instruction_t){.opcode = RESTORE});
    get_instruction(code, skip)->arg1 = get_next_instruction_index(code);
    return first;
}

static node_vtbl_t vtbl = {
    .type = NODE_TRY_CATCH,
    .analyze_reachability = reachability_unknown,
    .is_pure = not_pure,
    .collect_direct_effects = collect_child_effects,
    .type_name = L"try-catch",
    .get_data = get_data,
    .get_property_count = no_properties,
    .get_property = no_property,
    .get_child_count = get_child_count,
    .get_child = get_child,
    .get_child_tag = get_child_tag,
    .insert_child_before = no_child_insertion,
    .replace_child = no_child_replacement,
    .get_related_count = no_related_nodes,
    .get_related = no_related_node,
    .get_relation_type = no_relation_type,
    .calculate = no_abstract_value,
    .execute = execute,
    .generate_goat_code = generate_goat_code,
    .generate_indented_goat_code = generate_indented_goat_code,
    .generate_bytecode = generate_bytecode,
    .can_generate_c_code = cannot_generate_c_code,
    .generate_c_code = no_c_code,
    .generate_indented_c_code = no_indented_c_code,
    .generate_bytecode_assign = no_bytecode_assignment,
    .generate_bytecode_deferred = no_deferred_bytecode,
};

node_t *create_try_catch_node(arena_t *arena,
                              statement_t *body,
                              string_view_t exception_name,
                              statement_list_t *handler) {
    assert(body
           && (is_statement(body->base.vtbl->type) || is_branch_or_loop(body->base.vtbl->type)));
    assert(exception_name.data && exception_name.length);
    assert(handler && handler->base.base.vtbl->type == NODE_STATEMENT_LIST);
    try_catch_t *stmt = alloc_zeroed_from_arena(arena, sizeof(try_catch_t));
    stmt->base.base.vtbl = &vtbl;
    stmt->body = body;
    stmt->exception_name = copy_string_to_arena(arena, exception_name.data, exception_name.length);
    stmt->handler = handler;
    stmt->exception_declarator =
        create_synthetic_variable_declaration_node(arena, stmt->exception_name).declarator;
    return &stmt->base.base;
}

declarator_t *get_catch_declarator(node_t *node) {
    assert(node->vtbl->type == NODE_TRY_CATCH);
    return ((try_catch_t *)node)->exception_declarator;
}
