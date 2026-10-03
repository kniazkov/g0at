/**
 * @file declarations.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of variable and constant declaration nodes.
 */

#include "declarations.h"

#include "analysis/abstract_state.h"
#include "analysis/c_body.h"
#include "analysis/lattice.h"
#include "analysis/reachability.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/source_builder.h"
#include "common_methods.h"
#include "expression.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "statement.h"

#include <assert.h>

size_t get_property_count_of_declarator(const node_t *node) {
    const declarator_t *decl = (const declarator_t *)node;
    return decl->abstract_value ? 1 : 0;
}

/** @brief Implements node_vtbl_t::can_generate_c_code for a complete numeric body. */
static bool can_generate_c_code(const node_t *node,
                                const lattice_element_t *value,
                                const c_expression_context_t *context) {
    if (!context || !context->graph)
        return false;
    if (!is_declarator(node->vtbl->type))
        return c_body_children(node, value, context);
    return get_node_child_count(node) == 1
           && c_local_binding(context,
                              (const declarator_t *)node,
                              c_expression_type(context, get_node_child(node, 0)));
}

/** @brief Implements @ref node_vtbl_t::get_property. */
const wchar_t *
get_property_of_declarator(const node_t *node, size_t index, node_display_value_t *out_value) {
    const declarator_t *decl = (const declarator_t *)node;
    if (index == 0 && decl->abstract_value) {
        *out_value =
            (node_display_value_t){.text = lattice_to_string(decl->abstract_value),
                                   .kind = decl->abstract_value->type == LATTICE_STRING_CONSTANT
                                               ? NODE_DISPLAY_VALUE_STRING_LITERAL
                                               : NODE_DISPLAY_VALUE_PLAIN};
        return L"abstract";
    } else {
        *out_value =
            (node_display_value_t){.text = EMPTY_STRING_VALUE, .kind = NODE_DISPLAY_VALUE_PLAIN};
        return NULL;
    }
}

/** @brief A single variable declarator in a declaration statement. */
typedef struct {
    /** @brief Base declarator structure. */
    declarator_t base;

    /**
     * @brief Optional initializer expression for the variable.
     *
     * If not NULL, this expression will be evaluated and its result will become the initial value
     * of the variable.
     */
    expression_t *initial;
} variable_declarator_t;

/** @brief Implements node_vtbl_t::replace_child for the expression slot. */
static bool vdeclr_replace_child(node_t *node, node_t *old_child, node_t *new_child) {
    variable_declarator_t *owner = (variable_declarator_t *)node;
    if (!owner->initial || (node_t *)owner->initial != old_child
        || !is_expression(new_child->vtbl->type))
        return false;
    owner->initial = (expression_t *)new_child;
    return true;
}

/** @brief Implements @ref node_vtbl_t::get_data. */
static node_display_value_t vdeclr_get_data(const node_t *node) {
    const variable_declarator_t *decl = (const variable_declarator_t *)node;
    return (node_display_value_t){.text = VIEW_TO_VALUE(decl->base.name),
                                  .kind = NODE_DISPLAY_VALUE_PLAIN};
}

/** @brief Implements @ref node_vtbl_t::get_child_count. */
static size_t vdeclr_get_child_count(const node_t *node) {
    variable_declarator_t *decl = (variable_declarator_t *)node;
    return decl->initial == NULL ? 0 : 1;
}

/** @brief Implements @ref node_vtbl_t::get_child. */
static node_t *vdeclr_get_child(const node_t *node, size_t index) {
    const variable_declarator_t *decl = (const variable_declarator_t *)node;
    if (index == 0 && decl->initial) {
        return &decl->initial->base;
    }
    return NULL;
}

/** @brief Implements @ref node_vtbl_t::get_child_tag. */
static const wchar_t *vdeclr_get_child_tag(const node_t *node, size_t index) {
    const variable_declarator_t *decl = (const variable_declarator_t *)node;
    if (index == 0 && decl->initial) {
        return L"initial";
    }
    return NULL;
}

/** @brief Implements @ref node_vtbl_t::generate_goat_code. */
static string_value_t vdeclr_generate_goat_code(const node_t *node) {
    const variable_declarator_t *decl = (const variable_declarator_t *)node;
    if (decl->initial) {
        string_builder_t builder;
        string_value_t initial_as_string = generate_goat_code_from_expression(decl->initial);
        init_string_builder(&builder, decl->base.name.length + 3 + initial_as_string.length);
        append_string_view(&builder, decl->base.name);
        append_static_string(&builder, L" = ");
        string_value_t value = append_string_value(&builder, initial_as_string);
        FREE_STRING(initial_as_string);
        return value;
    } else {
        return VIEW_TO_VALUE(decl->base.name);
    }
}

/** @brief Implements @ref node_vtbl_t::generate_indented_goat_code. */
static void
vdeclr_generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    const variable_declarator_t *decl = (const variable_declarator_t *)node;
    append_formatted_source(builder, VIEW_TO_VALUE(decl->base.name));
    if (decl->initial) {
        append_static_source(builder, L" = ");
        generate_indented_goat_code_from_expression(decl->initial, builder, indent);
    }
}

/** @brief Implements @ref node_vtbl_t::generate_bytecode. */
static instr_index_t
vdeclr_generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    const variable_declarator_t *decl = (const variable_declarator_t *)node;
    instr_index_t first;
    if (decl->initial) {
        first = generate_bytecode_from_expression(decl->initial, code, data);
    } else {
        first = add_instruction(code, (instruction_t){.opcode = NIL});
    }
    uint32_t index = add_string_to_data_segment_ex(data, decl->base.name);
    add_instruction(code, (instruction_t){.opcode = VAR, .arg1 = index});
    return first;
}

/** @brief Implements node_vtbl_t::analyze_reachability. */
static const lattice_element_t *
declarator_reachability(node_t *node, abstract_state_t **state, analysis_collector_t *collector) {
    const lattice_element_t *value =
        get_node_child_count(node) ? visit_reachable_node(get_node_child(node, 0), state, collector)
                                   : make_null_element();
    if ((*state)->control_flow == FLOW_NORMAL) {
        set_in_abstract_state(*state, (declarator_t *)node, value);
    }
    return value;
}

/** @brief Virtual table for variable declarator operations. */
static node_vtbl_t vdeclr_vtbl = {
    .type = NODE_VARIABLE_DECLARATOR,
    .analyze_reachability = declarator_reachability,
    .is_pure = not_pure,
    .simplify = no_simplification,
    .collect_direct_effects = collect_child_effects,
    .type_name = L"variable declarator",
    .get_data = vdeclr_get_data,
    .get_property_count = get_property_count_of_declarator,
    .get_property = get_property_of_declarator,
    .get_child_count = vdeclr_get_child_count,
    .get_child = vdeclr_get_child,
    .get_child_tag = vdeclr_get_child_tag,
    .insert_child_before = no_child_insertion,
    .replace_child = vdeclr_replace_child,
    .get_related_count = no_related_nodes,
    .get_related = no_related_node,
    .get_relation_type = no_relation_type,
    .calculate = no_abstract_value,
    .execute = execute_nothing,
    .generate_goat_code = vdeclr_generate_goat_code,
    .generate_indented_goat_code = vdeclr_generate_indented_goat_code,
    .generate_bytecode = vdeclr_generate_bytecode,
    .can_generate_c_code = can_generate_c_code,
    .generate_c_code = no_c_code,
    .generate_indented_c_code = no_indented_c_code,
    .generate_bytecode_assign = no_bytecode_assignment,
    .generate_bytecode_deferred = no_deferred_bytecode,
};

/** @brief Creates a new variable declarator node. */
static variable_declarator_t *create_variable_declarator_node(arena_t *arena,
                                                              const declarator_spec_t *spec) {
    variable_declarator_t *decl =
        (variable_declarator_t *)alloc_zeroed_from_arena(arena, sizeof(variable_declarator_t));
    decl->base.base.vtbl = &vdeclr_vtbl;
    decl->base.name = spec->name;
    decl->initial = spec->initial;
    return decl;
}

/** @brief A variable declaration statement containing multiple declarators. */
typedef struct {
    /** @brief Base statement structure. */
    statement_t base;

    /** @brief Array of variable declarators. */
    variable_declarator_t **decl_list;

    /**
     * @brief Count of variable declarators.
     *
     * Must be at least 1 (empty declarations are not valid).
     */
    size_t decl_count;
} variable_declaration_t;

/** @brief Implements @ref node_vtbl_t::get_child_count. */
static size_t vdecln_get_child_count(const node_t *node) {
    const variable_declaration_t *root = (const variable_declaration_t *)node;
    return root->decl_count;
}

/** @brief Implements @ref node_vtbl_t::get_child. */
static node_t *vdecln_get_child(const node_t *node, size_t index) {
    const variable_declaration_t *decl = (const variable_declaration_t *)node;
    if (index >= decl->decl_count) {
        return NULL;
    }
    return &decl->decl_list[index]->base.base;
}

/** @brief Implements @ref node_vtbl_t::execute. */
static abstract_state_t *vdecln_execute(node_t *node, abstract_state_t *state, arena_t *arena) {
    const variable_declaration_t *decl = (const variable_declaration_t *)node;
    for (size_t index = 0; index < decl->decl_count; index++) {
        variable_declarator_t *vdr = decl->decl_list[index];
        if (vdr->initial) {
            const lattice_element_t *element = calculate_expression(vdr->initial, state, arena);
            if (state->control_flow != FLOW_NORMAL)
                break;
            set_in_abstract_state(state, &vdr->base, element);
        } else {
            set_in_abstract_state(state, &vdr->base, make_null_element());
        }
    }
    return state;
}

/** @brief Implements @ref node_vtbl_t::generate_goat_code. */
static string_value_t vdecln_generate_goat_code(const node_t *node) {
    const variable_declaration_t *decl = (const variable_declaration_t *)node;
    string_builder_t builder;
    init_string_builder(&builder, 0);
    append_static_string(&builder, L"var ");
    for (size_t index = 0; index < decl->decl_count; index++) {
        if (index > 0) {
            append_static_string(&builder, L", ");
        }
        variable_declarator_t *vdr = decl->decl_list[index];
        string_value_t vdr_as_string = vdeclr_generate_goat_code(&vdr->base.base);
        append_string_value(&builder, vdr_as_string);
        FREE_STRING(vdr_as_string);
    }
    return append_char(&builder, L';');
}

/** @brief Implements @ref node_vtbl_t::generate_indented_goat_code. */
static void
vdecln_generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    const variable_declaration_t *decl = (const variable_declaration_t *)node;
    add_static_source(builder, indent, L"var ");
    for (size_t index = 0; index < decl->decl_count; index++) {
        if (index > 0) {
            append_static_source(builder, L", ");
        }
        variable_declarator_t *vdr = decl->decl_list[index];
        vdeclr_generate_indented_goat_code(&vdr->base.base, builder, indent);
    }
    append_static_source(builder, L";");
}

/** @brief Implements @ref node_vtbl_t::generate_bytecode. */
static instr_index_t
vdecln_generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    const variable_declaration_t *decl = (const variable_declaration_t *)node;
    instr_index_t first = generate_bytecode_from_node(&decl->decl_list[0]->base.base, code, data);
    for (size_t index = 1; index < decl->decl_count; index++) {
        variable_declarator_t *vdr = decl->decl_list[index];
        generate_bytecode_from_node(&vdr->base.base, code, data);
    }
    return first;
}

/** @brief Virtual table for variable declaration nodes. */
static node_vtbl_t vdecln_vtbl = {
    .type = NODE_VARIABLE_DECLARATION,
    .analyze_reachability = visit_reachable_children,
    .is_pure = not_pure,
    .simplify = no_simplification,
    .collect_direct_effects = collect_child_effects,
    .type_name = L"variable declaration",
    .get_data = no_data,
    .get_property_count = no_properties,
    .get_property = no_property,
    .get_child_count = vdecln_get_child_count,
    .get_child = vdecln_get_child,
    .get_child_tag = no_tags,
    .insert_child_before = no_child_insertion,
    .replace_child = no_child_replacement,
    .get_related_count = no_related_nodes,
    .get_related = no_related_node,
    .get_relation_type = no_relation_type,
    .calculate = no_abstract_value,
    .execute = vdecln_execute,
    .generate_goat_code = vdecln_generate_goat_code,
    .generate_indented_goat_code = vdecln_generate_indented_goat_code,
    .generate_bytecode = vdecln_generate_bytecode,
    .can_generate_c_code = can_generate_c_code,
    .generate_c_code = no_c_code,
    .generate_indented_c_code = no_indented_c_code,
    .generate_bytecode_assign = no_bytecode_assignment,
    .generate_bytecode_deferred = no_deferred_bytecode,
};

node_t *
create_variable_declaration_node(arena_t *arena, declarator_spec_t **decl_list, size_t decl_count) {
    assert(decl_count > 0);
    variable_declaration_t *node =
        (variable_declaration_t *)alloc_zeroed_from_arena(arena, sizeof(variable_declaration_t));
    node->base.base.vtbl = &vdecln_vtbl;
    node->decl_list =
        (variable_declarator_t **)alloc_from_arena(arena,
                                                   decl_count * sizeof(variable_declarator_t *));
    node->decl_count = decl_count;

    for (size_t index = 0; index < decl_count; index++) {
        node->decl_list[index] = create_variable_declarator_node(arena, decl_list[index]);
    }

    return &node->base.base;
}

/**
 * @brief A constant declaration in the abstract syntax tree.
 *
 * Unlike variables, constants must be initialized at declaration time and cannot be modified
 * afterward.
 */
typedef struct {
    /** @brief Base declarator structure. */
    declarator_t base;

    /**
     * @brief Initializer expression for the constant.
     *
     * Must be non-NULL as constants require initialization. The expression is evaluated once at
     * declaration time and its result becomes the immutable value of the constant.
     */
    expression_t *initial;
} constant_declarator_t;

/** @brief Implements node_vtbl_t::replace_child for the expression slot. */
static bool cdeclr_replace_child(node_t *node, node_t *old_child, node_t *new_child) {
    constant_declarator_t *owner = (constant_declarator_t *)node;
    if (!owner->initial || (node_t *)owner->initial != old_child
        || !is_expression(new_child->vtbl->type))
        return false;
    owner->initial = (expression_t *)new_child;
    return true;
}

/** @brief Implements @ref node_vtbl_t::get_data. */
static node_display_value_t cdeclr_get_data(const node_t *node) {
    const constant_declarator_t *decl = (const constant_declarator_t *)node;
    return (node_display_value_t){.text = VIEW_TO_VALUE(decl->base.name),
                                  .kind = NODE_DISPLAY_VALUE_PLAIN};
}

/** @brief Implements @ref node_vtbl_t::get_child_count. */
static size_t cdeclr_get_child_count(const node_t *node) {
    return 1;
}

/** @brief Implements @ref node_vtbl_t::get_child. */
static node_t *cdeclr_get_child(const node_t *node, size_t index) {
    const constant_declarator_t *decl = (const constant_declarator_t *)node;
    if (index == 0) {
        return &decl->initial->base;
    }
    return NULL;
}

/** @brief Implements @ref node_vtbl_t::get_child_tag. */
static const wchar_t *cdeclr_get_child_tag(const node_t *node, size_t index) {
    if (index == 0) {
        return L"initial";
    }
    return NULL;
}

/** @brief Implements @ref node_vtbl_t::generate_goat_code. */
static string_value_t cdeclr_generate_goat_code(const node_t *node) {
    const constant_declarator_t *decl = (const constant_declarator_t *)node;
    string_builder_t builder;
    string_value_t initial_as_string = generate_goat_code_from_expression(decl->initial);
    init_string_builder(&builder, decl->base.name.length + 3 + initial_as_string.length);
    append_string_view(&builder, decl->base.name);
    append_static_string(&builder, L" = ");
    string_value_t value = append_string_value(&builder, initial_as_string);
    FREE_STRING(initial_as_string);
    return value;
}

/** @brief Implements @ref node_vtbl_t::generate_indented_goat_code. */
static void
cdeclr_generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    const constant_declarator_t *decl = (const constant_declarator_t *)node;
    append_formatted_source(builder, VIEW_TO_VALUE(decl->base.name));
    append_static_source(builder, L" = ");
    generate_indented_goat_code_from_expression(decl->initial, builder, indent);
}

/** @brief Implements @ref node_vtbl_t::generate_bytecode. */
static instr_index_t
cdeclr_generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    const constant_declarator_t *decl = (const constant_declarator_t *)node;
    instr_index_t first = generate_bytecode_from_expression(decl->initial, code, data);
    uint32_t index = add_string_to_data_segment_ex(data, decl->base.name);
    add_instruction(code, (instruction_t){.opcode = CONST, .arg1 = index});
    return first;
}

/** @brief Virtual table for constant declarator operations. */
static node_vtbl_t cdeclr_vtbl = {
    .type = NODE_CONSTANT_DECLARATOR,
    .analyze_reachability = declarator_reachability,
    .is_pure = not_pure,
    .simplify = no_simplification,
    .collect_direct_effects = collect_child_effects,
    .type_name = L"constant declarator",
    .get_data = cdeclr_get_data,
    .get_property_count = get_property_count_of_declarator,
    .get_property = get_property_of_declarator,
    .get_child_count = cdeclr_get_child_count,
    .get_child = cdeclr_get_child,
    .get_child_tag = cdeclr_get_child_tag,
    .insert_child_before = no_child_insertion,
    .replace_child = cdeclr_replace_child,
    .get_related_count = no_related_nodes,
    .get_related = no_related_node,
    .get_relation_type = no_relation_type,
    .calculate = no_abstract_value,
    .execute = execute_nothing,
    .generate_goat_code = cdeclr_generate_goat_code,
    .generate_indented_goat_code = cdeclr_generate_indented_goat_code,
    .generate_bytecode = cdeclr_generate_bytecode,
    .can_generate_c_code = can_generate_c_code,
    .generate_c_code = no_c_code,
    .generate_indented_c_code = no_indented_c_code,
    .generate_bytecode_assign = no_bytecode_assignment,
    .generate_bytecode_deferred = no_deferred_bytecode,
};

/** @brief Creates a new constant declarator AST node. */
static constant_declarator_t *create_constant_declarator_node(arena_t *arena,
                                                              const declarator_spec_t *spec) {
    assert(spec->initial != NULL);

    constant_declarator_t *decl =
        (constant_declarator_t *)alloc_zeroed_from_arena(arena, sizeof(constant_declarator_t));
    decl->base.base.vtbl = &cdeclr_vtbl;
    decl->base.name = spec->name;
    decl->initial = spec->initial;
    return decl;
}

/**
 * @brief A constant declaration statement in the abstract syntax tree.
 *
 * Unlike variables, all constants must be initialized at declaration time.
 */
typedef struct {
    /** @brief Base statement structure. */
    statement_t base;

    /** @brief Array of constant declarators. */
    constant_declarator_t **decl_list;

    /** @brief Count of constant declarators. */
    size_t decl_count;
} constant_declaration_t;

/** @brief Implements @ref node_vtbl_t::get_child_count. */
static size_t cdecln_get_child_count(const node_t *node) {
    const constant_declaration_t *root = (const constant_declaration_t *)node;
    return root->decl_count;
}

/** @brief Implements @ref node_vtbl_t::get_child. */
static node_t *cdecln_get_child(const node_t *node, size_t index) {
    const constant_declaration_t *decl = (const constant_declaration_t *)node;
    if (index >= decl->decl_count) {
        return NULL;
    }
    return &decl->decl_list[index]->base.base;
}

/** @brief Implements @ref node_vtbl_t::execute. */
static abstract_state_t *cdecln_execute(node_t *node, abstract_state_t *state, arena_t *arena) {
    const constant_declaration_t *decl = (const constant_declaration_t *)node;
    for (size_t index = 0; index < decl->decl_count; index++) {
        constant_declarator_t *cdr = decl->decl_list[index];
        const lattice_element_t *element = calculate_expression(cdr->initial, state, arena);
        if (state->control_flow != FLOW_NORMAL)
            break;
        set_in_abstract_state(state, &cdr->base, element);
    }
    return state;
}

/** @brief Implements @ref node_vtbl_t::generate_goat_code. */
static string_value_t cdecln_generate_goat_code(const node_t *node) {
    const constant_declaration_t *decl = (const constant_declaration_t *)node;
    string_builder_t builder;
    init_string_builder(&builder, 0);
    append_static_string(&builder, L"const ");
    for (size_t index = 0; index < decl->decl_count; index++) {
        if (index > 0) {
            append_static_string(&builder, L", ");
        }
        constant_declarator_t *cdr = decl->decl_list[index];
        string_value_t cdr_as_string = generate_goat_code_from_node(&cdr->base.base);
        append_string_value(&builder, cdr_as_string);
        FREE_STRING(cdr_as_string);
    }
    return append_char(&builder, L';');
}

/** @brief Implements @ref node_vtbl_t::generate_indented_goat_code. */
static void
cdecln_generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    const constant_declaration_t *decl = (const constant_declaration_t *)node;
    add_static_source(builder, indent, L"const ");
    for (size_t index = 0; index < decl->decl_count; index++) {
        if (index > 0) {
            append_static_source(builder, L", ");
        }
        constant_declarator_t *cdr = decl->decl_list[index];
        cdeclr_generate_indented_goat_code(&cdr->base.base, builder, indent);
    }
    append_static_source(builder, L";");
}

/** @brief Implements @ref node_vtbl_t::generate_bytecode. */
static instr_index_t
cdecln_generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    const constant_declaration_t *decl = (const constant_declaration_t *)node;
    instr_index_t first = generate_bytecode_from_node(&decl->decl_list[0]->base.base, code, data);
    for (size_t index = 1; index < decl->decl_count; index++) {
        constant_declarator_t *cdr = decl->decl_list[index];
        generate_bytecode_from_node(&cdr->base.base, code, data);
    }
    return first;
}

/** @brief Virtual table for constant declaration nodes. */
static node_vtbl_t cdecln_vtbl = {
    .type = NODE_CONSTANT_DECLARATION,
    .analyze_reachability = visit_reachable_children,
    .is_pure = not_pure,
    .simplify = no_simplification,
    .collect_direct_effects = collect_child_effects,
    .type_name = L"constant declaration",
    .get_data = no_data,
    .get_property_count = no_properties,
    .get_property = no_property,
    .get_child_count = cdecln_get_child_count,
    .get_child = cdecln_get_child,
    .get_child_tag = no_tags,
    .insert_child_before = no_child_insertion,
    .replace_child = no_child_replacement,
    .get_related_count = no_related_nodes,
    .get_related = no_related_node,
    .get_relation_type = no_relation_type,
    .calculate = no_abstract_value,
    .execute = cdecln_execute,
    .generate_goat_code = cdecln_generate_goat_code,
    .generate_indented_goat_code = cdecln_generate_indented_goat_code,
    .generate_bytecode = cdecln_generate_bytecode,
    .can_generate_c_code = can_generate_c_code,
    .generate_c_code = no_c_code,
    .generate_indented_c_code = no_indented_c_code,
    .generate_bytecode_assign = no_bytecode_assignment,
    .generate_bytecode_deferred = no_deferred_bytecode,
};

node_t *
create_constant_declaration_node(arena_t *arena, declarator_spec_t **decl_list, size_t decl_count) {
    assert(decl_count > 0);
    constant_declaration_t *node =
        (constant_declaration_t *)alloc_zeroed_from_arena(arena, sizeof(constant_declaration_t));
    node->base.base.vtbl = &cdecln_vtbl;
    node->decl_list =
        (constant_declarator_t **)alloc_from_arena(arena,
                                                   decl_count * sizeof(constant_declarator_t *));
    node->decl_count = decl_count;

    for (size_t index = 0; index < decl_count; index++) {
        node->decl_list[index] = create_constant_declarator_node(arena, decl_list[index]);
    }

    return &node->base.base;
}

variable_declaration_pair_t create_synthetic_variable_declaration_node(arena_t *arena,
                                                                       string_view_t name) {
    variable_declarator_t *declarator =
        (variable_declarator_t *)alloc_zeroed_from_arena(arena, sizeof(variable_declarator_t));
    declarator->base.base.vtbl = &vdeclr_vtbl;
    declarator->base.name = name;
    declarator->initial = NULL;

    variable_declaration_t *declaration =
        (variable_declaration_t *)alloc_zeroed_from_arena(arena, sizeof(variable_declaration_t));
    declaration->base.base.vtbl = &vdecln_vtbl;
    declaration->decl_list =
        (variable_declarator_t **)alloc_from_arena(arena, sizeof(variable_declarator_t *) * 1);
    declaration->decl_list[0] = declarator;
    declaration->decl_count = 1;

    return (variable_declaration_pair_t){.declaration = &declaration->base.base,
                                         .declarator = &declarator->base};
}

/** @brief Invalid name used by the built-in declarator singleton. */
static wchar_t builtin_declarator_name_data[] = L"*";

/**
 * @brief Singleton fake declarator for built-in names.
 *
 * Used as a non-NULL declaration target for runtime-provided names such as built-in functions and
 * constants.
 */
static declarator_t builtin_declarator = {
    .base = {.vtbl = &cdeclr_vtbl, .parent = NULL, .position = NULL, .scope = NULL, .id = 0},
    .name = {.data = L"*", .length = 1}};

const declarator_t *get_builtin_declarator() {
    return &builtin_declarator;
}
