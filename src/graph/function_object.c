/**
 * @file function_object.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of function object expressions.
 */

#include "analysis/c_body.h"
#include "analysis/function_summary.h"
#include "analysis/lattice.h"
#include "analysis/reachability.h"
#include "codegen/c_lowering.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/source_builder.h"
#include "common_methods.h"
#include "declarations.h"
#include "expression.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/linked_list.h"
#include "lib/string_ext.h"
#include "statement.h"
#include "statement_sequence.h"

#include <assert.h>

/** @brief A single function argument in the abstract syntax tree. */
typedef struct {
    /** @brief Base declarator structure. */
    declarator_t base;
} argument_t;

/** @brief Implements @ref node_vtbl_t::get_data. */
static node_display_value_t arg_get_data(const node_t *node) {
    const argument_t *arg = (const argument_t *)node;
    return (node_display_value_t){.text = VIEW_TO_VALUE(arg->base.name),
                                  .kind = NODE_DISPLAY_VALUE_PLAIN};
}

/** @brief Implements @ref node_vtbl_t::generate_goat_code. */
static string_value_t arg_generate_goat_code(const node_t *node) {
    const argument_t *arg = (const argument_t *)node;
    return VIEW_TO_VALUE(arg->base.name);
}

/** @brief Implements @ref node_vtbl_t::generate_indented_goat_code. */
static void
arg_generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    const argument_t *arg = (const argument_t *)node;
    append_formatted_source(builder, VIEW_TO_VALUE(arg->base.name));
}

/** @brief Implements @ref node_vtbl_t::generate_bytecode. */
static instr_index_t
arg_generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    assert(false);
    return BAD_INSTR_INDEX;
}

/** @brief Implements node_vtbl_t::analyze_reachability. */
static const lattice_element_t *
fobj_reachability(node_t *node, abstract_state_t **state, analysis_collector_t *collector) {
    return make_function_element();
}

/** @brief Virtual table for function argument nodes. */
static node_vtbl_t arg_vtbl = {
    .type = NODE_ARGUMENT,
    .analyze_reachability = reachability_unknown,
    .is_pure = children_are_pure,
    .simplify = no_simplification,
    .collect_direct_effects = collect_child_effects,
    .type_name = L"argument",
    .get_data = arg_get_data,
    .get_property_count = get_property_count_of_declarator,
    .get_property = get_property_of_declarator,
    .get_child_count = no_children,
    .get_child = no_child,
    .get_child_tag = no_tags,
    .insert_child_before = no_child_insertion,
    .replace_child = no_child_replacement,
    .get_related_count = no_related_nodes,
    .get_related = no_related_node,
    .get_relation_type = no_relation_type,
    .calculate = no_abstract_value,
    .execute = execute_nothing,
    .generate_goat_code = arg_generate_goat_code,
    .generate_indented_goat_code = arg_generate_indented_goat_code,
    .generate_bytecode = arg_generate_bytecode,
    .can_generate_c_code = cannot_generate_c_code,
    .generate_c_code = no_c_code,
    .generate_indented_c_code = no_indented_c_code,
    .generate_bytecode_assign = no_bytecode_assignment,
    .generate_bytecode_deferred = no_deferred_bytecode,
};

/**
 * @brief Creates a new function argument AST node.
 *
 * The name is copied to the arena so the node does not depend on parser temporary storage.
 */
static argument_t *create_argument_node(arena_t *arena, string_view_t name) {
    argument_t *arg = (argument_t *)alloc_zeroed_from_arena(arena, sizeof(argument_t));
    arg->base.base.vtbl = &arg_vtbl;
    arg->base.name = copy_string_to_arena(arena, name.data, name.length);
    return arg;
}

/** @brief A list of function arguments in the abstract syntax tree. */
typedef struct {
    /** @brief Base AST node structure. */
    node_t base;

    /**
     * @brief Array of function argument nodes.
     *
     * The array may be NULL when @ref arg_count is zero.
     */
    argument_t **arg_list;

    /** @brief Count of function arguments. */
    size_t arg_count;
} argument_list_t;

/** @brief Implements @ref node_vtbl_t::get_child_count. */
static size_t alist_get_child_count(const node_t *node) {
    const argument_list_t *list = (const argument_list_t *)node;
    return list->arg_count;
}

/** @brief Implements @ref node_vtbl_t::get_child. */
static node_t *alist_get_child(const node_t *node, size_t index) {
    const argument_list_t *list = (const argument_list_t *)node;
    if (index >= list->arg_count) {
        return NULL;
    }
    return &list->arg_list[index]->base.base;
}

/** @brief Implements @ref node_vtbl_t::generate_goat_code. */
static string_value_t alist_generate_goat_code(const node_t *node) {
    assert(false);
    return EMPTY_STRING_VALUE;
}

/** @brief Implements @ref node_vtbl_t::generate_indented_goat_code. */
static void
alist_generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    assert(false);
}

/** @brief Implements @ref node_vtbl_t::generate_bytecode. */
static instr_index_t
alist_generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    assert(false);
    return BAD_INSTR_INDEX;
}

/** @brief Virtual table for function argument list nodes. */
static node_vtbl_t alist_vtbl = {
    .type = NODE_ARGUMENT_LIST,
    .analyze_reachability = reachability_unknown,
    .is_pure = children_are_pure,
    .simplify = no_simplification,
    .collect_direct_effects = collect_child_effects,
    .type_name = L"argument list",
    .get_data = no_data,
    .get_property_count = no_properties,
    .get_property = no_property,
    .get_child_count = alist_get_child_count,
    .get_child = alist_get_child,
    .get_child_tag = no_tags,
    .insert_child_before = no_child_insertion,
    .replace_child = no_child_replacement,
    .get_related_count = no_related_nodes,
    .get_related = no_related_node,
    .get_relation_type = no_relation_type,
    .calculate = no_abstract_value,
    .execute = execute_nothing,
    .generate_goat_code = alist_generate_goat_code,
    .generate_indented_goat_code = alist_generate_indented_goat_code,
    .generate_bytecode = alist_generate_bytecode,
    .can_generate_c_code = cannot_generate_c_code,
    .generate_c_code = no_c_code,
    .generate_indented_c_code = no_indented_c_code,
    .generate_bytecode_assign = no_bytecode_assignment,
    .generate_bytecode_deferred = no_deferred_bytecode,
};

/**
 * @brief Creates a new function argument list AST node.
 * `arg_list`: Array of formal argument names. May be NULL when arg_count is zero.
 */
static argument_list_t *
create_argument_list_node(arena_t *arena, string_view_t *arg_list, size_t arg_count) {
    argument_list_t *node =
        (argument_list_t *)alloc_zeroed_from_arena(arena, sizeof(argument_list_t));
    node->base.vtbl = &alist_vtbl;
    node->arg_count = arg_count;

    if (arg_count > 0) {
        assert(arg_list != NULL);
        node->arg_list = (argument_t **)alloc_from_arena(arena, arg_count * sizeof(argument_t *));
        for (size_t index = 0; index < arg_count; index++) {
            node->arg_list[index] = create_argument_node(arena, arg_list[index]);
        }
    }

    return node;
}

/**
 * @brief AST node that stores the body of a function.
 *
 * The node has the same syntactic shape as a regular statement list, but different execution
 * semantics: it does not create an additional lexical environment.
 */
typedef struct {
    /** @brief Base node structure. */
    node_t base;

    /** @brief Linked list of statements in the function body. */
    list_t *statements;
} function_body_t;

/** @brief Implements @ref node_vtbl_t::get_child_count. */
static size_t fbody_get_child_count(const node_t *node) {
    const function_body_t *body = (const function_body_t *)node;
    return body->statements->size;
}

/** @brief Implements @ref node_vtbl_t::get_child. */
static node_t *fbody_get_child(const node_t *node, size_t index) {
    const function_body_t *body = (const function_body_t *)node;
    return (node_t *)get_linked_list_value(body->statements, index).ptr;
}

/** @brief Implements @ref node_vtbl_t::insert_child_before. */
static bool fbody_insert_child_before(node_t *node, node_t *new_child, node_t *before_child) {
    function_body_t *body = (function_body_t *)node;
    return insert_statement_to_list_before(body->statements, new_child, before_child);
}

/** @brief Implements @ref node_vtbl_t::generate_goat_code. */
static string_value_t fbody_generate_goat_code(const node_t *node) {
    assert(false);
    return EMPTY_STRING_VALUE;
}

/** @brief Implements @ref node_vtbl_t::generate_indented_goat_code. */
static void
fbody_generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    assert(false);
}

/** @brief Stub for @ref node_vtbl_t::generate_bytecode; the function object emits the body. */
static instr_index_t
fbody_generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    assert(false);
    return BAD_INSTR_INDEX;
}

/** @brief Implements node_vtbl_t::replace_child for a statement sequence. */
static bool fbody_replace_child(node_t *node, node_t *old_child, node_t *new_child) {
    return replace_statement_in_list(((function_body_t *)node)->statements, old_child, new_child);
}

/** @brief Virtual table for function_body node operations. */
static node_vtbl_t function_body_vtbl = {
    .type = NODE_FUNCTION_BODY,
    .analyze_reachability = reachability_unknown,
    .is_pure = children_are_pure,
    .simplify = no_simplification,
    .collect_direct_effects = collect_child_effects,
    .type_name = L"function body",
    .get_data = no_data,
    .get_property_count = no_properties,
    .get_property = no_property,
    .get_child_count = fbody_get_child_count,
    .get_child = fbody_get_child,
    .get_child_tag = no_tags,
    .insert_child_before = fbody_insert_child_before,
    .replace_child = fbody_replace_child,
    .get_related_count = no_related_nodes,
    .get_related = no_related_node,
    .get_relation_type = no_relation_type,
    .calculate = no_abstract_value,
    .execute = execute_nothing,
    .generate_goat_code = fbody_generate_goat_code,
    .generate_indented_goat_code = fbody_generate_indented_goat_code,
    .generate_bytecode = fbody_generate_bytecode,
    .can_generate_c_code = c_body_children,
    .generate_c_code = no_c_code,
    .generate_indented_c_code = c_emit_body,
    .generate_bytecode_assign = no_bytecode_assignment,
    .generate_bytecode_deferred = no_deferred_bytecode,
};

/** @brief Creates a new function body AST node. */
static function_body_t *create_function_body_node(arena_t *arena) {
    function_body_t *body =
        (function_body_t *)alloc_zeroed_from_arena(arena, sizeof(function_body_t));
    body->base.vtbl = &function_body_vtbl;
    return body;
}

/** @brief A function object expression in the AST. */
typedef struct {
    /** @brief Base expression structure from which function_object_t inherits. */
    expression_t base;

    /** @brief Formal argument list for the function. */
    argument_list_t *arguments;

    /** @brief Function body. */
    function_body_t *body;

    /** @brief Observed body signatures, owned by the graph arena. */
    function_summary_set_t *summaries;

    /**
     * @brief Index of the `ARG` instruction containing index of the first instruction of the
     * function body.
     */
    instr_index_t code_instr_index;
} function_object_t;

/** @brief Implements @ref node_vtbl_t::get_child_count. */
static size_t fobj_get_child_count(const node_t *node) {
    return 2;
}

/** @brief Implements @ref node_vtbl_t::get_child. */
static node_t *fobj_get_child(const node_t *node, size_t index) {
    const function_object_t *expr = (const function_object_t *)node;
    if (index == 0) {
        return &expr->arguments->base;
    }
    if (index == 1) {
        return &expr->body->base;
    }
    return NULL;
}

/** @brief Implements @ref node_vtbl_t::get_child_tag. */
static const wchar_t *fobj_get_child_tag(const node_t *node, size_t index) {
    if (index == 0) {
        return L"arguments";
    }
    if (index == 1) {
        return L"body";
    }
    return NULL;
}

/**
 * @brief Generates the function header in Goat syntax.
 * @return The resulting string after appending the header.
 */
static string_value_t generate_header(const function_object_t *expr, string_builder_t *builder) {
    append_static_string(builder, L"func(");
    for (size_t index = 0; index < expr->arguments->arg_count; index++) {
        if (index > 0) {
            append_static_string(builder, L", ");
        }
        append_string_view(builder, expr->arguments->arg_list[index]->base.name);
    }
    return append_static_string(builder, L") ");
}

/** @brief Implements @ref node_vtbl_t::calculate. */
static const lattice_element_t *
fobj_calculate(node_t *node, abstract_state_t *state, arena_t *arena) {
    return make_known_function_element(arena, node, state->call_frame);
}

/** @brief Implements @ref node_vtbl_t::generate_goat_code. */
static string_value_t fobj_generate_goat_code(const node_t *node) {
    const function_object_t *expr = (const function_object_t *)node;
    string_builder_t builder;
    init_string_builder(&builder, 128);
    generate_header(expr, &builder);
    return generate_goat_code_from_statement_list(expr->body->statements, &builder, true);
}

/** @brief Implements @ref node_vtbl_t::generate_indented_goat_code. */
static void
fobj_generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    const function_object_t *expr = (const function_object_t *)node;
    string_builder_t header;
    init_string_builder(&header, 16);
    generate_header(expr, &header);
    append_formatted_source(builder, append_char(&header, L'{'));
    list_item_t *item = expr->body->statements->head;
    while (item) {
        statement_t *stmt = (statement_t *)item->value.ptr;
        generate_indented_goat_code_from_statement(stmt, builder, indent + 1);
        item = item->next;
    }
    add_static_source(builder, indent, L"}");
}

/** @brief Implements @ref node_vtbl_t::generate_bytecode. */
static instr_index_t
fobj_generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    function_object_t *expr = (function_object_t *)node;
    instr_index_t first = expr->code_instr_index =
        add_instruction(code, (instruction_t){.opcode = ARG, .arg1 = 0xFFFFFFFF} // placeholder
        );
    uint32_t arg_names_idx = 0;
    if (expr->arguments->arg_count > 0) {
        size_t arg_size = expr->arguments->arg_count * sizeof(uint32_t);
        uint32_t *arg_names = (uint32_t *)ALLOC(arg_size);
        for (size_t index = 0; index < expr->arguments->arg_count; index++) {
            arg_names[index] =
                add_string_to_data_segment_ex(data, expr->arguments->arg_list[index]->base.name);
        }
        arg_names_idx = add_data_to_data_segment(data, arg_names, arg_size);
        FREE(arg_names);
    }
    add_instruction(code,
                    (instruction_t){.opcode = FUNC,
                                    .arg0 = (uint16_t)expr->arguments->arg_count,
                                    .arg1 = arg_names_idx});
    return first;
}

/** @brief Implements @ref node_vtbl_t::generate_bytecode_deferred. */
static bool
fobj_generate_bytecode_deferred(const node_t *node, code_builder_t *code, data_builder_t *data) {
    function_object_t *expr = (function_object_t *)node;
    if (expr->code_instr_index == BAD_INSTR_INDEX) {
        return false;
    }
    instr_index_t first;
    if (expr->body->statements->size == 0) {
        first = add_instruction(code, (instruction_t){.opcode = NIL});
        add_instruction(code, (instruction_t){.opcode = RET});
    } else {
        list_item_t *item = expr->body->statements->head;
        statement_t *stmt = (statement_t *)item->value.ptr;
        first = generate_bytecode_from_statement(stmt, code, data);

        while (item->next) {
            item = item->next;
            stmt = (statement_t *)item->value.ptr;
            generate_bytecode_from_statement(stmt, code, data);
        }

        if (stmt->base.vtbl->type != NODE_RETURN) {
            add_instruction(code, (instruction_t){.opcode = NIL});
            add_instruction(code, (instruction_t){.opcode = RET});
        }
    }
    code->instructions[expr->code_instr_index].arg1 = (uint32_t)first;
    return true;
}

/** @brief Function purity describes all registered bodies, not closure allocation. */
static bool fobj_is_pure(const node_t *node) {
    return function_summary_flags(get_function_summaries(node)) & NODE_FLAG_PURE;
}

/** @brief Signature results and the explicitly selected visualization context. */
static size_t fobj_get_property_count(const node_t *node) {
    const function_summary_set_t *set = get_function_summaries(node);
    size_t count = select_function_c_view(set) ? 1 : 0;
    for (const function_summary_t *s = set->head; s; s = s->next)
        count++;
    return count;
}

/** @brief Implements node_vtbl_t::get_property. */
static const wchar_t *
fobj_get_property(const node_t *node, size_t index, node_display_value_t *out) {
    const function_summary_set_t *set = get_function_summaries(node);
    const function_summary_t *view = select_function_c_view(set);
    if (view && index == 0) {
        *out = (node_display_value_t){.text = function_signature_to_string(view)};
        return L"C view";
    }
    if (view)
        index--;
    const function_summary_t *s = set->head;
    while (s && index--)
        s = s->next;
    if (!s)
        return NULL;
    string_value_t signature = function_signature_to_string(s);
    string_value_t result = lattice_to_string(s->return_type);
    out->kind = NODE_DISPLAY_VALUE_PLAIN;
    out->text = format_string(L"%s \u2192 %s; %s; C=%s",
                              signature.data,
                              result.data,
                              function_summary_is_pure(s) ? L"pure" : L"unknown purity",
                              s->c_support == FUNCTION_C_SUPPORTED     ? L"supported"
                              : s->c_support == FUNCTION_C_UNSUPPORTED ? L"unsupported"
                                                                       : L"unknown");
    FREE_STRING(signature);
    FREE_STRING(result);
    return L"specialization";
}

/** @brief Virtual table for function object node operations. */
static node_vtbl_t fo_vtbl = {
    .type = NODE_FUNCTION_OBJECT,
    .analyze_reachability = fobj_reachability,
    .is_pure = fobj_is_pure,
    .simplify = no_simplification,
    .collect_direct_effects = no_direct_effects,
    .type_name = L"function object",
    .get_data = no_data,
    .get_property_count = fobj_get_property_count,
    .get_property = fobj_get_property,
    .get_child_count = fobj_get_child_count,
    .get_child = fobj_get_child,
    .get_child_tag = fobj_get_child_tag,
    .insert_child_before = no_child_insertion,
    .replace_child = no_child_replacement,
    .get_related_count = no_related_nodes,
    .get_related = no_related_node,
    .get_relation_type = no_relation_type,
    .calculate = fobj_calculate,
    .execute = execute_nothing,
    .generate_goat_code = fobj_generate_goat_code,
    .generate_indented_goat_code = fobj_generate_indented_goat_code,
    .generate_bytecode = fobj_generate_bytecode,
    .generate_bytecode_deferred = fobj_generate_bytecode_deferred,
    .can_generate_c_code = cannot_generate_c_code,
    .generate_c_code = no_c_code,
    .generate_indented_c_code = c_emit_function,
    .generate_bytecode_assign = no_bytecode_assignment,
};

node_t *create_function_object_node(arena_t *arena, string_view_t *arg_list, size_t arg_count) {
    function_object_t *fobj =
        (function_object_t *)alloc_zeroed_from_arena(arena, sizeof(function_object_t));
    fobj->base.base.vtbl = &fo_vtbl;
    fobj->arguments = create_argument_list_node(arena, arg_list, arg_count);
    fobj->body = create_function_body_node(arena);
    fobj->summaries = create_function_summary_set(arena, &fobj->base.base, arg_count);
    return &fobj->base.base;
}

void fill_function_body(node_t *node, list_t *statements) {
    assert(node->vtbl->type == NODE_FUNCTION_OBJECT);
    function_object_t *fobj = (function_object_t *)node;
    fobj->body->statements = statements;
    fobj->code_instr_index = BAD_INSTR_INDEX;
}

function_summary_set_t *get_function_summaries(const node_t *node) {
    assert(node->vtbl->type == NODE_FUNCTION_OBJECT);
    return ((const function_object_t *)node)->summaries;
}
