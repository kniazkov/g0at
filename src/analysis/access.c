/** @file access.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Access proofs use lexical identities, never the observed values of calls.
 */
#include "access.h"

#include "c_expression.h"
#include "graph/expression.h"
#include "graph/index_access.h"
#include "graph/member_access.h"
#include "graph/replacement.h"
#include "graph/variable.h"
#include "resources/messages.h"
#include "simplification.h"

static const node_t *enclosing_function(const node_t *node) {
    for (; node; node = node->parent)
        if (node->vtbl->type == NODE_FUNCTION_OBJECT)
            return node;
    return NULL;
}

/** @brief Captured constants can become mutable cells in flat; never resolve across functions. */
static const node_t *resolve(const node_t *node, const node_t *observer) {
    for (size_t depth = 0; node && depth < 64; depth++) {
        if (node->vtbl->type == NODE_EXPRESSION_PARENTHESIZED) {
            node = get_node_child(node, 0);
        } else if (node->vtbl->type == NODE_VARIABLE) {
            const variable_t *var = (const variable_t *)node;
            const declarator_t *decl = var->declarator;
            if (var->dynamic_lookup || !decl || decl == get_builtin_declarator()
                || decl->base.vtbl->type != NODE_CONSTANT_DECLARATOR
                || enclosing_function(&decl->base) != enclosing_function(observer))
                return NULL;
            node = get_node_child(&decl->base, 0);
        } else {
            return node;
        }
    }
    return NULL;
}

static bool within(const scope_t *scope, const scope_t *owner) {
    for (; scope; scope = scope->parent)
        if (scope == owner)
            return true;
    return false;
}

/** @brief A granted private slot wins before ordinary lookup, including lexical prototypes. */
static bool has_private_slot(const node_t *observer, string_view_t name) {
    for (const scope_t *scope = observer->scope; scope; scope = scope->parent) {
        const declarator_t *decl = find_symbol_in_scope(scope, name.data);
        if (decl && decl != get_builtin_declarator() && decl->modifier == ACCESS_PRIVATE)
            return true;
    }
    return false;
}

/** @brief Only unconditional own declarations prove a cell; inherited/dynamic lookup is unknown. */
static const declarator_t *own_declaration(const node_t *object, string_view_t name) {
    if (!object || object->vtbl->type != NODE_STATEMENT_LIST)
        return NULL;
    const declarator_t *found = NULL;
    for (size_t i = 0; i < get_node_child_count(object); i++) {
        const node_t *statement = get_node_child(object, i);
        if (statement->vtbl->type != NODE_CONSTANT_DECLARATION
            && statement->vtbl->type != NODE_VARIABLE_DECLARATION)
            continue;
        for (size_t j = 0; j < get_node_child_count(statement); j++) {
            const declarator_t *decl = (const declarator_t *)get_node_child(statement, j);
            if (decl->name.length == name.length
                && !MEMCMP(decl->name.data, name.data, name.length)) {
                if (found)
                    return NULL;
                found = decl;
            }
        }
    }
    return found;
}

/** @brief Protected grants can depend on the eventual method receiver; do not guess them. */
static bool may_have_inherited_grants(const node_t *node) {
    for (; node; node = node->parent)
        if (node->vtbl->type == NODE_FUNCTION_OBJECT || node->vtbl->type == NODE_INHERITANCE)
            return true;
    return false;
}

static const gchar_t *operation(const node_t *node) {
    const node_t *parent = node->parent;
    if (parent && get_node_child(parent, 0) == node) {
        if (parent->vtbl->type == NODE_SIMPLE_ASSIGNMENT)
            return U"write";
        if (parent->vtbl->type == NODE_COMPOUND_ASSIGNMENT
            || (parent->vtbl->type >= NODE_PREFIX_INCREMENT
                && parent->vtbl->type <= NODE_POSTFIX_DECREMENT))
            return U"read/write";
    }
    return U"read";
}

void analyze_access(node_t *node, arena_t *errors, compilation_error_t **list) {
    node->access_proof = ACCESS_PROOF_NONE;
    const declarator_t *decl = NULL;
    node_type_t type = node->vtbl->type;
    if (type == NODE_VARIABLE) {
        const variable_t *var = (const variable_t *)node;
        node->access_proof =
            var->dynamic_lookup || !var->declarator ? ACCESS_PROOF_UNKNOWN : ACCESS_PROOF_ALLOWED;
        if (var->name.length == 4 && !MEMCMP(var->name.data, U"this", 4))
            node->access_proof = ACCESS_PROOF_UNKNOWN;
    } else if (type == NODE_MEMBER_ACCESS || type == NODE_INDEX_ACCESS) {
        node->access_proof = ACCESS_PROOF_UNKNOWN;
        const node_t *object = resolve(get_node_child(node, 0), node);
        string_view_t name = {0};
        if (type == NODE_MEMBER_ACCESS) {
            name = ((const member_access_t *)node)->name;
        } else {
            const index_access_t *index = (const index_access_t *)node;
            const node_t *key =
                index->count == 1 ? resolve((node_t *)index->indices[0], node) : NULL;
            if (key && key->vtbl->type == NODE_STATIC_STRING) {
                string_value_t value = key->vtbl->get_data(key).text;
                name = VALUE_TO_VIEW(value);
            }
        }
        if (name.data)
            decl = own_declaration(object, name);
        if (decl) {
            if (has_private_slot(node, name))
                node->access_proof = ACCESS_PROOF_UNKNOWN;
            else if (decl->modifier == ACCESS_PUBLIC || decl->modifier == ACCESS_MODULE)
                node->access_proof = ACCESS_PROOF_ALLOWED;
            else if (within(node->scope, decl->base.scope))
                /* A lexical template is not proof of a particular factory activation. */
                node->access_proof = ACCESS_PROOF_UNKNOWN;
            else if (decl->modifier == ACCESS_PRIVATE || !may_have_inherited_grants(node))
                node->access_proof = ACCESS_PROOF_DENIED;
        }
    }
    if (node->access_proof == ACCESS_PROOF_DENIED) {
        const node_t *declaration = &decl->base;
        while (declaration && !declaration->position)
            declaration = declaration->parent;
        const full_position_t *position = declaration ? declaration->position->begin : NULL;
        compilation_error_t *error =
            create_error_from_node(errors,
                                   node,
                                   ERROR,
                                   get_messages()->access_denied_statically,
                                   operation(node),
                                   decl->name.data,
                                   position ? position->row : 0,
                                   position ? position->column : 0);
        error->next = *list;
        *list = error;
    }
    for (size_t i = 0; i < get_node_child_count(node); i++)
        analyze_access(get_node_child(node, i), errors, list);
}

/** @brief Nested function bodies have independent contracts. */
static bool
needs_checks(const node_t *node, const node_t *function, const c_expression_context_t *context) {
    if (node != function && node->vtbl->type == NODE_FUNCTION_OBJECT)
        return false;
    bool primitive_receiver = false;
    if (context
        && (node->vtbl->type == NODE_MEMBER_ACCESS || node->vtbl->type == NODE_INDEX_ACCESS)) {
        for (const c_expression_proof_t *proof = context->head; proof; proof = proof->next)
            if (proof->node == get_node_child(node, 0)) {
                primitive_receiver =
                    proof->value_type == C_VALUE_STRING || proof->value_type == C_VALUE_CHAR;
                break;
            }
    }
    if (node->access_proof == ACCESS_PROOF_DENIED
        || (node->access_proof == ACCESS_PROOF_UNKNOWN && !primitive_receiver))
        return true;
    if (node->vtbl->type == NODE_VARIABLE) {
        const declarator_t *decl = ((const variable_t *)node)->declarator;
        if (decl && decl != get_builtin_declarator() && decl->modifier != ACCESS_MODULE
            && !within(decl->base.scope, function->scope))
            return true;
    }
    for (size_t i = 0; i < get_node_child_count(node); i++)
        if (needs_checks(get_node_child(node, i), function, context))
            return true;
    return false;
}

bool function_needs_access_checks(const node_t *function) {
    if (!function || !function->scope)
        return false;
    /* Legacy primitive member/method proofs retain their existing native path. */
    const node_t *root = function;
    while (root->parent)
        root = root->parent;
    return has_access_modifiers(root) && needs_checks(function, function, NULL);
}

bool function_signature_needs_access_checks(const function_summary_t *summary) {
    if (!function_needs_access_checks(summary->function))
        return false;
    c_expression_context_t context = {.head = summary->c_expressions};
    return needs_checks(summary->function, summary->function, &context);
}

/** @brief No binding, call, property or scope may disappear during this restricted folding. */
static bool closed_scalar(const node_t *node) {
    node_type_t type = node->vtbl->type;
    bool scalar = (type >= NODE_NULL && type <= NODE_CHARACTER)
                  || type == NODE_EXPRESSION_PARENTHESIZED || type == NODE_ADDITION
                  || type == NODE_SUBTRACTION
                  || (type >= NODE_LOGICAL_NOT && type <= NODE_NOT_EQUAL);
    if (!scalar)
        return false;
    for (size_t i = 0; i < get_node_child_count(node); i++)
        if (!closed_scalar(get_node_child(node, i)))
            return false;
    return true;
}

void simplify_access_safe_expressions(node_t *node, arena_t *arena) {
    for (size_t i = 0; i < get_node_child_count(node); i++) {
        node_t *child = get_node_child(node, i);
        if (closed_scalar(child)) {
            simplify_graph(child, arena);
            node_t *result = child->vtbl->simplify(child, arena);
            if (result != child) {
                node_t *wrapper = (node_t *)create_expression_replacement(arena,
                                                                          (expression_t *)child,
                                                                          (expression_t *)result);
                if (replace_child_node(node, child, wrapper)) {
                    wrapper->parent = node;
                    child->parent = wrapper;
                    result->parent = wrapper;
                }
            }
        } else
            simplify_access_safe_expressions(child, arena);
    }
}
