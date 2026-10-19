/** @file compound_assignment.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Read-modify-write sugar using existing operations and retained address operands.
 */
#include "compound_assignment.h"

#include "analysis/abstract_state.h"
#include "analysis/c_body.h"
#include "analysis/function_effects.h"
#include "analysis/reachability.h"
#include "codegen/c_locals.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/source_builder.h"
#include "common_methods.h"
#include "index_access.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "logic.h"
#include "member_access.h"
#include "variable.h"

#include <assert.h>

/** @brief The operation owns both operands; no graph node is shared. */
typedef struct {
    expression_t base;
    expression_t *operation;
    const wchar_t *symbol;
    opcode_t opcode;
} compound_assignment_t;

node_t *compound_assignment_target(const node_t *node) {
    return get_node_child((node_t *)((const compound_assignment_t *)node)->operation, 0);
}

bool is_compound_assignment_target(const node_t *node) {
    const node_t *operation = node->parent;
    return operation && operation->parent
           && operation->parent->vtbl->type == NODE_COMPOUND_ASSIGNMENT
           && get_node_child(operation, 0) == node;
}

static size_t get_child_count(const node_t *node) {
    return 1;
}

static node_t *get_child(const node_t *node, size_t index) {
    return index ? NULL : (node_t *)((const compound_assignment_t *)node)->operation;
}

/** @brief Keep the target and operation intact while allowing RHS optimization. */
static bool replace_operand(node_t *node, node_t *old, node_t *replacement) {
    binary_operation_t *operation = (binary_operation_t *)node;
    if ((node_t *)operation->right_operand != old || !is_expression(replacement->vtbl->type))
        return false;
    operation->right_operand = (expression_t *)replacement;
    return true;
}

static const lattice_element_t *
store(node_t *node, const lattice_element_t *value, abstract_state_t *state, bool trace) {
    node_t *target = compound_assignment_target(node);
    if (state->control_flow != FLOW_NORMAL)
        return make_bottom_element();
    if (target->vtbl->type != NODE_VARIABLE) {
        forget_abstract_values(state);
        return value;
    }
    const variable_t *variable = (const variable_t *)target;
    const declarator_t *decl = variable->declarator;
    if (variable->dynamic_lookup) {
        forget_abstract_values(state);
    } else if (decl && decl != get_builtin_declarator()) {
        if (decl->base.vtbl->type == NODE_CONSTANT_DECLARATOR) {
            state->control_flow = FLOW_UNREACHABLE;
            return make_bottom_element();
        }
        if (trace)
            set_in_abstract_state_at(state, decl, value, node);
        else
            set_in_abstract_state(state, decl, value);
    }
    return value;
}

static const lattice_element_t *calculate(node_t *node, abstract_state_t *state, arena_t *arena) {
    const lattice_element_t *value =
        calculate_expression(((compound_assignment_t *)node)->operation, state, arena);
    return store(node, value, state, true);
}

static const lattice_element_t *
analyze_reachability(node_t *node, abstract_state_t **state, analysis_collector_t *collector) {
    const lattice_element_t *value = visit_reachable_node(get_child(node, 0), state, collector);
    return store(node, value, *state, false);
}

static void
collect_direct_effects(const node_t *node, function_summary_t *summary, arena_t *arena) {
    collect_node_direct_effects(get_child(node, 0), summary, arena);
    record_function_access(summary,
                           compound_assignment_target(node),
                           FUNCTION_CAPTURE_WRITE,
                           arena);
}

static string_value_t generate_goat_code(const node_t *node) {
    const compound_assignment_t *expr = (const compound_assignment_t *)node;
    string_value_t left = generate_goat_code_from_node(compound_assignment_target(node));
    string_value_t right = generate_goat_code_from_expression(
        (expression_t *)get_node_child((node_t *)expr->operation, 1));
    string_value_t result = format_string(L"%s %s %s", left.data, expr->symbol, right.data);
    FREE_STRING(left);
    FREE_STRING(right);
    return result;
}

static void
generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    append_formatted_source(builder, generate_goat_code(node));
}

static instr_index_t generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    const compound_assignment_t *expr = (const compound_assignment_t *)node;
    node_t *target = compound_assignment_target(node);
    instr_index_t first;
    uint32_t retained = 0, property = 0;
    if (target->vtbl->type == NODE_MEMBER_ACCESS) {
        const member_access_t *member = (const member_access_t *)target;
        first = generate_bytecode_from_expression(member->object, code, data);
        retained = 1;
        property = add_string_to_data_segment_ex(data, member->name);
        add_instruction(code, (instruction_t){.opcode = DUP});
        add_instruction(code, (instruction_t){.opcode = GETPROP, .arg1 = property});
    } else if (target->vtbl->type == NODE_INDEX_ACCESS) {
        const index_access_t *index = (const index_access_t *)target;
        first = generate_bytecode_from_expression(index->object, code, data);
        for (size_t i = 0; i < index->count; i++)
            generate_bytecode_from_expression(index->indices[i], code, data);
        retained = (uint32_t)index->count + 1;
        add_instruction(code, (instruction_t){.opcode = DUPN, .arg1 = retained});
        add_instruction(code, (instruction_t){.opcode = GETINDEX, .arg1 = index->count});
    } else {
        assert(target->vtbl->type == NODE_VARIABLE);
        first = generate_bytecode_from_expression((expression_t *)target, code, data);
    }
    generate_bytecode_from_expression((expression_t *)get_node_child((node_t *)expr->operation, 1),
                                      code,
                                      data);
    add_instruction(code, (instruction_t){.opcode = expr->opcode});
    if (retained) {
        add_instruction(code, (instruction_t){.opcode = ROTATE, .arg1 = retained});
        add_instruction(code,
                        target->vtbl->type == NODE_MEMBER_ACCESS
                            ? (instruction_t){.opcode = SETPROP, .arg1 = property}
                            : (instruction_t){.opcode = SETINDEX, .arg1 = retained - 1});
    } else {
        generate_bytecode_assign_from_node(target, code, data);
    }
    return first;
}

static bool can_generate_c_code(const node_t *node,
                                const lattice_element_t *value,
                                const c_expression_context_t *context) {
    const node_t *target = compound_assignment_target(node);
    if (!context || !context->graph || target->vtbl->type != NODE_VARIABLE)
        return false;
    const variable_t *variable = (const variable_t *)target;
    const declarator_t *decl = variable->declarator;
    c_value_type_t type = c_expression_type(context, get_child(node, 0));
    return !variable->dynamic_lookup && decl && decl->base.vtbl->type != NODE_CONSTANT_DECLARATOR
           && type != C_VALUE_UNKNOWN && c_local_binding(context, decl, type);
}

static c_generated_expression_t generate_c_code(const node_t *node,
                                                c_generation_context_t *context) {
    return c_store_local(node,
                         compound_assignment_target(node),
                         generate_c_code_from_node(get_child(node, 0), context),
                         context);
}

static node_vtbl_t vtbl = {.type = NODE_COMPOUND_ASSIGNMENT,
                           .type_name = L"compound assignment",
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
                           .replace_child = no_child_replacement,
                           .get_related_count = no_related_nodes,
                           .get_related = no_related_node,
                           .get_relation_type = no_relation_type,
                           .calculate = calculate,
                           .execute = execute_nothing,
                           .generate_goat_code = generate_goat_code,
                           .generate_indented_goat_code = generate_indented_goat_code,
                           .generate_bytecode = generate_bytecode,
                           .can_generate_c_code = can_generate_c_code,
                           .generate_c_code = generate_c_code,
                           .generate_indented_c_code = no_indented_c_code,
                           .generate_bytecode_assign = no_bytecode_assignment,
                           .generate_bytecode_deferred = no_deferred_bytecode};

expression_t *create_compound_assignment_node(arena_t *arena,
                                              assignable_expression_t *left,
                                              expression_t *right,
                                              const wchar_t *symbol) {
    typedef struct {
        const wchar_t *symbol;
        opcode_t opcode;
        expression_t *(*create)(arena_t *, expression_t *, expression_t *);
    } operation_t;

    static const operation_t operations[] = {{L"+=", ADD, create_addition_node},
                                             {L"-=", SUB, create_subtraction_node},
                                             {L"*=", MUL, create_multiplication_node},
                                             {L"/=", DIVIDE, create_division_node},
                                             {L"%=", MODULO, create_modulo_node},
                                             {L"**=", POWER, create_power_node},
                                             {L"&=", BAND, create_bitwise_and_node},
                                             {L"|=", BOR, create_bitwise_or_node},
                                             {L"^=", BXOR, create_bitwise_xor_node},
                                             {L"<<=", SHL, create_shift_left_node},
                                             {L">>=", SHR, create_shift_right_node}};
    for (size_t i = 0; i < sizeof(operations) / sizeof(*operations); i++) {
        if (wcscmp(symbol, operations[i].symbol))
            continue;
        compound_assignment_t *expr = alloc_zeroed_from_arena(arena, sizeof(*expr));
        expr->base.base.vtbl = &vtbl;
        expr->symbol = operations[i].symbol;
        expr->opcode = operations[i].opcode;
        expr->operation = operations[i].create(arena, &left->base, right);
        /* The operation's left child is also an address: never fold it into a literal. */
        node_vtbl_t *operation_vtbl = alloc_from_arena(arena, sizeof(*operation_vtbl));
        *operation_vtbl = *expr->operation->base.vtbl;
        operation_vtbl->simplify = no_simplification;
        operation_vtbl->replace_child = replace_operand;
        expr->operation->base.vtbl = operation_vtbl;
        return &expr->base;
    }
    assert(false);
    return NULL;
}
