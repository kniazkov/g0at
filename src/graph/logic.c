/** @file logic.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Logical short-circuit and bitwise expressions.
 */
#include "logic.h"

#include "analysis/bitwise.h"
#include "analysis/reachability.h"
#include "codegen/code_builder.h"
#include "codegen/source_builder.h"
#include "common_methods.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"

#include <assert.h>

bitwise_kind_t node_bitwise_kind(node_type_t type) {
    switch (type) {
        case NODE_BITWISE_AND:
            return BIT_AND;
        case NODE_BITWISE_OR:
            return BIT_OR;
        case NODE_BITWISE_XOR:
            return BIT_XOR;
        case NODE_SHIFT_LEFT:
            return BIT_SHIFT_LEFT;
        case NODE_SHIFT_RIGHT:
            return BIT_SHIFT_RIGHT;
        default:
            assert(false);
            return BIT_AND;
    }
}

static size_t child_count(const node_t *node) {
    return ((const binary_operation_t *)node)->right_operand ? 2 : 1;
}

static node_t *child(const node_t *node, size_t index) {
    return index < child_count(node) ? binop_get_child(node, index) : NULL;
}

static const wchar_t *tag(const node_t *node, size_t index) {
    return index < child_count(node) ? (index ? L"right" : L"left") : NULL;
}

/** @brief Evaluates only feasible paths and merges their effects. */
static const lattice_element_t *
calculate_short(node_t *node, abstract_state_t *state, arena_t *arena) {
    binary_operation_t *expr = (binary_operation_t *)node;
    const lattice_element_t *left = calculate_expression(expr->left_operand, state, arena);
    abstract_truth_t truth = lattice_truth(left);
    if (state->control_flow != FLOW_NORMAL || truth == ABSTRACT_NEVER)
        return make_bottom_element();
    bool is_or = node->vtbl->type == NODE_LOGICAL_OR;
    const lattice_element_t *skipped = is_or ? make_true_element() : make_false_element();
    if (truth == (is_or ? ABSTRACT_TRUE : ABSTRACT_FALSE))
        return skipped;
    if (truth != ABSTRACT_EITHER)
        return lattice_boolean(calculate_expression(expr->right_operand, state, arena), false);
    abstract_state_t *right = clone_abstract_state(state);
    const lattice_element_t *value =
        lattice_boolean(calculate_expression(expr->right_operand, right, arena), false);
    if (right->control_flow != FLOW_NORMAL)
        value = make_bottom_element();
    abstract_state_t *merged = join_abstract_states(state, right);
    abstract_state_t old = *state;
    *state = *merged;
    *merged = old;
    destroy_abstract_state(merged);
    destroy_abstract_state(right);
    collect_joined_abstract_state(state, node);
    return lattice_join(arena, skipped, value);
}

/** @brief Implements node_vtbl_t::calculate. */
static const lattice_element_t *calculate(node_t *node, abstract_state_t *state, arena_t *arena) {
    node_type_t type = node->vtbl->type;
    if (type == NODE_LOGICAL_AND || type == NODE_LOGICAL_OR)
        return calculate_short(node, state, arena);
    binary_operation_t *expr = (binary_operation_t *)node;
    const lattice_element_t *left = calculate_expression(expr->left_operand, state, arena);
    if (type == NODE_LOGICAL_NOT || type == NODE_BOOLEAN_CONVERSION)
        return lattice_boolean(left, type == NODE_LOGICAL_NOT);
    if (type == NODE_BITWISE_NOT)
        return lattice_bitwise_not(arena, left);
    const lattice_element_t *right = calculate_expression(expr->right_operand, state, arena);
    return lattice_bitwise(arena, left, right, node_bitwise_kind(type));
}

static const wchar_t *symbol(node_type_t type) {
    switch (type) {
        case NODE_LOGICAL_NOT:
            return L"!";
        case NODE_BOOLEAN_CONVERSION:
            return L"!!";
        case NODE_BITWISE_NOT:
            return L"~";
        case NODE_LOGICAL_AND:
            return L"&&";
        case NODE_LOGICAL_OR:
            return L"||";
        case NODE_BITWISE_AND:
            return L"&";
        case NODE_BITWISE_OR:
            return L"|";
        case NODE_BITWISE_XOR:
            return L"^";
        case NODE_SHIFT_LEFT:
            return L"<<";
        case NODE_SHIFT_RIGHT:
            return L">>";
        default:
            assert(false);
            return L"";
    }
}

static opcode_t opcode(node_type_t type) {
    switch (type) {
        case NODE_LOGICAL_NOT:
            return LNOT;
        case NODE_BOOLEAN_CONVERSION:
            return BOOL;
        case NODE_BITWISE_NOT:
            return BNOT;
        case NODE_LOGICAL_AND:
            return LAND;
        case NODE_LOGICAL_OR:
            return LOR;
        case NODE_BITWISE_AND:
            return BAND;
        case NODE_BITWISE_OR:
            return BOR;
        case NODE_BITWISE_XOR:
            return BXOR;
        case NODE_SHIFT_LEFT:
            return SHL;
        case NODE_SHIFT_RIGHT:
            return SHR;
        default:
            assert(false);
            return NOP;
    }
}

/** @brief Implements node_vtbl_t::generate_goat_code. */
static string_value_t generate_goat_code(const node_t *node) {
    const binary_operation_t *expr = (const binary_operation_t *)node;
    string_value_t left = generate_goat_code_from_expression(expr->left_operand), result;
    if (expr->right_operand) {
        string_value_t right = generate_goat_code_from_expression(expr->right_operand);
        result = format_string(L"(%s %s %s)", left.data, symbol(node->vtbl->type), right.data);
        FREE_STRING(right);
    } else
        result = format_string(L"(%s(%s))", symbol(node->vtbl->type), left.data);
    FREE_STRING(left);
    return result;
}

static void
generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    append_formatted_source(builder, generate_goat_code(node));
}

/** @brief Visits only feasible short-circuit paths. */
static const lattice_element_t *analyze_short_reachability(node_t *node,
                                                           abstract_state_t **state,
                                                           analysis_collector_t *collector) {
    const lattice_element_t *left = visit_reachable_node(get_node_child(node, 0), state, collector);
    node_t *right_node = get_node_child(node, 1);
    if ((*state)->control_flow != FLOW_NORMAL) {
        mark_unreachable_subtree(right_node, collector);
        return make_bottom_element();
    }
    bool is_or = node->vtbl->type == NODE_LOGICAL_OR;
    abstract_truth_t truth = lattice_truth(left);
    const lattice_element_t *skipped = is_or ? make_true_element() : make_false_element();
    if (truth == (is_or ? ABSTRACT_TRUE : ABSTRACT_FALSE)) {
        mark_unreachable_subtree(right_node, collector);
        return skipped;
    }
    if (truth != ABSTRACT_EITHER)
        return lattice_boolean(visit_reachable_node(right_node, state, collector), false);
    abstract_state_t *right = clone_abstract_state(*state);
    const lattice_element_t *value =
        lattice_boolean(visit_reachable_node(right_node, &right, collector), false);
    if (right->control_flow != FLOW_NORMAL)
        value = make_bottom_element();
    abstract_state_t *merged = join_abstract_states(*state, right);
    destroy_abstract_state(right);
    destroy_abstract_state(*state);
    *state = merged;
    return lattice_join(merged->arena, skipped, value);
}

/** @brief Implements node_vtbl_t::analyze_reachability. */
static const lattice_element_t *
analyze_reachability(node_t *node, abstract_state_t **state, analysis_collector_t *collector) {
    node_type_t type = node->vtbl->type;
    if (type == NODE_LOGICAL_NOT || type == NODE_BOOLEAN_CONVERSION || type == NODE_BITWISE_NOT) {
        const lattice_element_t *value =
            visit_reachable_node(get_node_child(node, 0), state, collector);
        value = node->vtbl->type == NODE_BITWISE_NOT
                    ? lattice_bitwise_not((*state)->arena, value)
                    : lattice_boolean(value, node->vtbl->type == NODE_LOGICAL_NOT);
        if (value->type == LATTICE_BOTTOM)
            (*state)->control_flow = FLOW_UNREACHABLE;
        return value;
    }
    const lattice_element_t *left = visit_reachable_node(get_node_child(node, 0), state, collector);
    const lattice_element_t *right =
        visit_reachable_node(get_node_child(node, 1), state, collector);
    const lattice_element_t *value =
        lattice_bitwise((*state)->arena, left, right, node_bitwise_kind(node->vtbl->type));
    if (value->type == LATTICE_BOTTOM)
        (*state)->control_flow = FLOW_UNREACHABLE;
    return value;
}

/** @brief Preserves left effects even when reachability excludes the right operand. */
static instr_index_t generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    binary_operation_t *expr = (binary_operation_t *)node;
    instr_index_t first = generate_bytecode_from_expression(expr->left_operand, code, data);
    opcode_t op = opcode(node->vtbl->type);
    if (op == LAND || op == LOR) {
        if (node_has_flag(&expr->right_operand->base, NODE_FLAG_UNREACHABLE)) {
            add_instruction(code, (instruction_t){.opcode = BOOL});
        } else {
            instr_index_t jump = add_instruction(code, (instruction_t){.opcode = op});
            generate_bytecode_from_expression(expr->right_operand, code, data);
            add_instruction(code, (instruction_t){.opcode = BOOL});
            get_instruction(code, jump)->arg1 = get_next_instruction_index(code);
        }
    } else {
        if (expr->right_operand)
            generate_bytecode_from_expression(expr->right_operand, code, data);
        add_instruction(code, (instruction_t){.opcode = op});
    }
    return first;
}

static node_vtbl_t vtables[] = {
    {
        .type = NODE_LOGICAL_NOT,
        .analyze_reachability = analyze_reachability,
        .is_pure = children_are_pure,
        .type_name = L"logical not",
        .get_data = no_data,
        .get_property_count = no_properties,
        .get_property = no_property,
        .get_child_count = child_count,
        .get_child = child,
        .get_child_tag = tag,
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
        .can_generate_c_code = cannot_generate_c_code,
        .generate_c_code = no_c_code,
        .generate_indented_c_code = no_indented_c_code,
        .generate_bytecode_assign = no_bytecode_assignment,
        .generate_bytecode_deferred = no_deferred_bytecode,
    },
    {
        .type = NODE_BOOLEAN_CONVERSION,
        .analyze_reachability = analyze_reachability,
        .is_pure = children_are_pure,
        .type_name = L"boolean conversion",
        .get_data = no_data,
        .get_property_count = no_properties,
        .get_property = no_property,
        .get_child_count = child_count,
        .get_child = child,
        .get_child_tag = tag,
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
        .can_generate_c_code = cannot_generate_c_code,
        .generate_c_code = no_c_code,
        .generate_indented_c_code = no_indented_c_code,
        .generate_bytecode_assign = no_bytecode_assignment,
        .generate_bytecode_deferred = no_deferred_bytecode,
    },
    {
        .type = NODE_BITWISE_NOT,
        .analyze_reachability = analyze_reachability,
        .is_pure = children_are_pure,
        .type_name = L"bitwise not",
        .get_data = no_data,
        .get_property_count = no_properties,
        .get_property = no_property,
        .get_child_count = child_count,
        .get_child = child,
        .get_child_tag = tag,
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
        .can_generate_c_code = cannot_generate_c_code,
        .generate_c_code = no_c_code,
        .generate_indented_c_code = no_indented_c_code,
        .generate_bytecode_assign = no_bytecode_assignment,
        .generate_bytecode_deferred = no_deferred_bytecode,
    },
    {
        .type = NODE_LOGICAL_AND,
        .analyze_reachability = analyze_short_reachability,
        .is_pure = children_are_pure,
        .type_name = L"logical and",
        .get_data = no_data,
        .get_property_count = no_properties,
        .get_property = no_property,
        .get_child_count = child_count,
        .get_child = child,
        .get_child_tag = tag,
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
        .can_generate_c_code = cannot_generate_c_code,
        .generate_c_code = no_c_code,
        .generate_indented_c_code = no_indented_c_code,
        .generate_bytecode_assign = no_bytecode_assignment,
        .generate_bytecode_deferred = no_deferred_bytecode,
    },
    {
        .type = NODE_LOGICAL_OR,
        .analyze_reachability = analyze_short_reachability,
        .is_pure = children_are_pure,
        .type_name = L"logical or",
        .get_data = no_data,
        .get_property_count = no_properties,
        .get_property = no_property,
        .get_child_count = child_count,
        .get_child = child,
        .get_child_tag = tag,
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
        .can_generate_c_code = cannot_generate_c_code,
        .generate_c_code = no_c_code,
        .generate_indented_c_code = no_indented_c_code,
        .generate_bytecode_assign = no_bytecode_assignment,
        .generate_bytecode_deferred = no_deferred_bytecode,
    },
    {
        .type = NODE_BITWISE_AND,
        .analyze_reachability = analyze_reachability,
        .is_pure = children_are_pure,
        .type_name = L"bitwise and",
        .get_data = no_data,
        .get_property_count = no_properties,
        .get_property = no_property,
        .get_child_count = child_count,
        .get_child = child,
        .get_child_tag = tag,
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
        .can_generate_c_code = cannot_generate_c_code,
        .generate_c_code = no_c_code,
        .generate_indented_c_code = no_indented_c_code,
        .generate_bytecode_assign = no_bytecode_assignment,
        .generate_bytecode_deferred = no_deferred_bytecode,
    },
    {
        .type = NODE_BITWISE_OR,
        .analyze_reachability = analyze_reachability,
        .is_pure = children_are_pure,
        .type_name = L"bitwise or",
        .get_data = no_data,
        .get_property_count = no_properties,
        .get_property = no_property,
        .get_child_count = child_count,
        .get_child = child,
        .get_child_tag = tag,
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
        .can_generate_c_code = cannot_generate_c_code,
        .generate_c_code = no_c_code,
        .generate_indented_c_code = no_indented_c_code,
        .generate_bytecode_assign = no_bytecode_assignment,
        .generate_bytecode_deferred = no_deferred_bytecode,
    },
    {
        .type = NODE_BITWISE_XOR,
        .analyze_reachability = analyze_reachability,
        .is_pure = children_are_pure,
        .type_name = L"bitwise xor",
        .get_data = no_data,
        .get_property_count = no_properties,
        .get_property = no_property,
        .get_child_count = child_count,
        .get_child = child,
        .get_child_tag = tag,
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
        .can_generate_c_code = cannot_generate_c_code,
        .generate_c_code = no_c_code,
        .generate_indented_c_code = no_indented_c_code,
        .generate_bytecode_assign = no_bytecode_assignment,
        .generate_bytecode_deferred = no_deferred_bytecode,
    },
    {
        .type = NODE_SHIFT_LEFT,
        .analyze_reachability = analyze_reachability,
        .is_pure = children_are_pure,
        .type_name = L"shift left",
        .get_data = no_data,
        .get_property_count = no_properties,
        .get_property = no_property,
        .get_child_count = child_count,
        .get_child = child,
        .get_child_tag = tag,
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
        .can_generate_c_code = cannot_generate_c_code,
        .generate_c_code = no_c_code,
        .generate_indented_c_code = no_indented_c_code,
        .generate_bytecode_assign = no_bytecode_assignment,
        .generate_bytecode_deferred = no_deferred_bytecode,
    },
    {
        .type = NODE_SHIFT_RIGHT,
        .analyze_reachability = analyze_reachability,
        .is_pure = children_are_pure,
        .type_name = L"shift right",
        .get_data = no_data,
        .get_property_count = no_properties,
        .get_property = no_property,
        .get_child_count = child_count,
        .get_child = child,
        .get_child_tag = tag,
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
        .can_generate_c_code = cannot_generate_c_code,
        .generate_c_code = no_c_code,
        .generate_indented_c_code = no_indented_c_code,
        .generate_bytecode_assign = no_bytecode_assignment,
        .generate_bytecode_deferred = no_deferred_bytecode,
    },
};

expression_t *create_logical_not_node(arena_t *arena, expression_t *left) {
    binary_operation_t *expr = alloc_zeroed_from_arena(arena, sizeof(*expr));
    expr->base.base.vtbl = &vtables[0];
    expr->left_operand = left;
    expr->right_operand = NULL;
    return &expr->base;
}

expression_t *create_boolean_conversion_node(arena_t *arena, expression_t *left) {
    binary_operation_t *expr = alloc_zeroed_from_arena(arena, sizeof(*expr));
    expr->base.base.vtbl = &vtables[1];
    expr->left_operand = left;
    expr->right_operand = NULL;
    return &expr->base;
}

expression_t *create_bitwise_not_node(arena_t *arena, expression_t *left) {
    binary_operation_t *expr = alloc_zeroed_from_arena(arena, sizeof(*expr));
    expr->base.base.vtbl = &vtables[2];
    expr->left_operand = left;
    expr->right_operand = NULL;
    return &expr->base;
}

expression_t *create_logical_and_node(arena_t *arena, expression_t *left, expression_t *right) {
    binary_operation_t *expr = alloc_zeroed_from_arena(arena, sizeof(*expr));
    expr->base.base.vtbl = &vtables[3];
    expr->left_operand = left;
    expr->right_operand = right;
    return &expr->base;
}

expression_t *create_logical_or_node(arena_t *arena, expression_t *left, expression_t *right) {
    binary_operation_t *expr = alloc_zeroed_from_arena(arena, sizeof(*expr));
    expr->base.base.vtbl = &vtables[4];
    expr->left_operand = left;
    expr->right_operand = right;
    return &expr->base;
}

expression_t *create_bitwise_and_node(arena_t *arena, expression_t *left, expression_t *right) {
    binary_operation_t *expr = alloc_zeroed_from_arena(arena, sizeof(*expr));
    expr->base.base.vtbl = &vtables[5];
    expr->left_operand = left;
    expr->right_operand = right;
    return &expr->base;
}

expression_t *create_bitwise_or_node(arena_t *arena, expression_t *left, expression_t *right) {
    binary_operation_t *expr = alloc_zeroed_from_arena(arena, sizeof(*expr));
    expr->base.base.vtbl = &vtables[6];
    expr->left_operand = left;
    expr->right_operand = right;
    return &expr->base;
}

expression_t *create_bitwise_xor_node(arena_t *arena, expression_t *left, expression_t *right) {
    binary_operation_t *expr = alloc_zeroed_from_arena(arena, sizeof(*expr));
    expr->base.base.vtbl = &vtables[7];
    expr->left_operand = left;
    expr->right_operand = right;
    return &expr->base;
}

expression_t *create_shift_left_node(arena_t *arena, expression_t *left, expression_t *right) {
    binary_operation_t *expr = alloc_zeroed_from_arena(arena, sizeof(*expr));
    expr->base.base.vtbl = &vtables[8];
    expr->left_operand = left;
    expr->right_operand = right;
    return &expr->base;
}

expression_t *create_shift_right_node(arena_t *arena, expression_t *left, expression_t *right) {
    binary_operation_t *expr = alloc_zeroed_from_arena(arena, sizeof(*expr));
    expr->base.base.vtbl = &vtables[9];
    expr->left_operand = left;
    expr->right_operand = right;
    return &expr->base;
}
