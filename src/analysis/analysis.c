/**
 * @file analysis.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of static code analysis functions.
 */

#include "analysis.h"

#include "cli/options.h"
#include "common/compilation_error.h"
#include "function_summary.h"
#include "graph/declarations.h"
#include "graph/expression.h"
#include "graph/node.h"
#include "graph/statement.h"
#include "graph/variable.h"
#include "interpreter.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/queue.h"
#include "lib/vector.h"
#include "model/context.h"
#include "model/object.h"
#include "properties.h"
#include "reachability.h"
#include "resources/messages.h"

#include <assert.h>

/** @brief Adds built-ins to the root scope using a shared synthetic declarator. */
static scope_t *create_scope_from_root_context(arena_t *arena) {
    scope_t *scope = create_scope(arena, NULL);
    context_t *context = get_root_context();
    object_array_t keys = get_object_keys(context->data);
    const declarator_t *declarator = get_builtin_declarator();
    for (size_t index = 0; index < keys.size; index++) {
        const object_t *key = keys.items[index];
        string_value_t key_str = convert_object_to_string(key);
        add_symbol_to_scope(scope, key_str.data, declarator);
    }
    return scope;
}

/**
 * @brief Assigns parents, scopes, and node IDs; queues functions for later binding.
 * Functions restart numbering; ordinary blocks inherit the sequence. Outer scopes
 * must be bound before inner functions so closures can resolve later declarations.
 */
static void assign_node_indexes_and_scopes(node_t *node,
                                           node_t *parent,
                                           queue_t *functions,
                                           arena_t *arena,
                                           scope_t *scope,
                                           unsigned int *next_id) {
    node->parent = parent;
    node->scope = scope;
    if (node->vtbl->type == NODE_FUNCTION_OBJECT)
        reset_function_summary(get_function_summary(node));
    node->flags &= ~(NODE_FLAG_UNREACHABLE | NODE_FLAG_PURE | NODE_FLAG_C_COMPATIBLE);
    if (node->vtbl->type == NODE_IF_ELSE)
        set_if_else_condition_truth(node, ABSTRACT_EITHER);
    if (is_declarator(node->vtbl->type))
        ((declarator_t *)node)->abstract_value = NULL;
    node->id = (*next_id)++;
    const size_t child_count = get_node_child_count(node);
    for (size_t child_id = 0; child_id < child_count; child_id++) {
        node_t *child = get_node_child(node, child_id);
        if (node->vtbl->type == NODE_TRY_CATCH && child_id == 0) {
            assign_node_indexes_and_scopes(child,
                                           node,
                                           functions,
                                           arena,
                                           create_scope(arena, scope),
                                           next_id);
            continue;
        }
        switch (child->vtbl->type) {
            case NODE_FUNCTION_OBJECT: {
                /* Queue inner functions until enclosing names have been bound. */
                enqueue(functions, child);
                scope_t *inner_scope = create_scope(arena, scope);
                unsigned int inner_counter = 1;
                assign_node_indexes_and_scopes(child,
                                               node,
                                               functions,
                                               arena,
                                               inner_scope,
                                               &inner_counter);
                break;
            }
            case NODE_STATEMENT_LIST: {
                /* Ordinary blocks keep the enclosing ID sequence. */
                scope_t *inner_scope = create_scope(arena, scope);
                assign_node_indexes_and_scopes(child, node, functions, arena, inner_scope, next_id);
                break;
            }
            default: {
                assign_node_indexes_and_scopes(child, node, functions, arena, scope, next_id);
                break;
            }
        }
    }
    if (node->vtbl->type == NODE_TRY_CATCH) {
        declarator_t *decl = get_catch_declarator(node);
        decl->base.parent = node;
        decl->base.scope = get_node_child(node, 1)->scope;
        decl->base.id = (*next_id)++;
        decl->base.position = node->position;
        decl->abstract_value = NULL;
    }
}

/**
 * @brief Attaches a synthetic subtree to an existing scope and repairs parent links.
 * Does not assign IDs or introduce scopes.
 */
static void assign_scope_to_subtree(node_t *node, node_t *parent, scope_t *scope) {
    node->parent = parent;
    node->scope = scope;

    const size_t child_count = get_node_child_count(node);
    for (size_t child_id = 0; child_id < child_count; child_id++) {
        node_t *child = get_node_child(node, child_id);
        assign_scope_to_subtree(child, node, scope);
    }
}

/** @brief Finds the nearest statement that contains a variable usage. */
static node_t *find_parent_statement(variable_t *var) {
    node_t *node = var->base.base.base.parent;
    while (node) {
        if (node->parent && is_statement_list(node->parent->vtbl->type)
            && (is_statement(node->vtbl->type) || is_branch_or_loop(node->vtbl->type))) {
            return node;
        }
        node = node->parent;
    }
    assert(false);
}

/**
 * @brief Deferred AST insertion request.
 *
 * Stores a synthetic node that must be inserted into the tree after variable binding is complete.
 *
 * Insertions are deferred because adding nodes during the binding pass would change traversal
 * structure and node indexes while the analyzer is still walking the existing tree.
 */
typedef struct {
    /** @brief Parent node that receives the inserted child. */
    node_t *target;

    /** @brief Synthetic node to insert. */
    node_t *item;

    /** @brief Existing child before which `item` should be inserted. */
    node_t *before;
} insertion_t;

/**
 * @brief Binds names within a subtree, skipping nested functions.
 * Implicit declarations are queued for insertion after traversal to avoid invalidating
 * child indexes. Nested functions are bound separately after their enclosing scopes.
 */
static void bind_variables_from_node_and_children(node_t *node,
                                                  parser_memory_t *memory,
                                                  vector_t *insertions,
                                                  compilation_error_t **errors,
                                                  options_t *options) {
    if (node->vtbl->type == NODE_TRY_CATCH) {
        bind_variables_from_node_and_children(get_node_child(node, 0),
                                              memory,
                                              insertions,
                                              errors,
                                              options);
        declarator_t *decl = get_catch_declarator(node);
        add_symbol_to_scope(decl->base.scope, decl->name.data, decl);
        bind_variables_from_node_and_children(get_node_child(node, 1),
                                              memory,
                                              insertions,
                                              errors,
                                              options);
        return;
    }
    if (is_declarator(node->vtbl->type)) {
        declarator_t *declarator = (declarator_t *)node;
        add_symbol_to_scope(node->scope, declarator->name.data, declarator);
    } else if (node->vtbl->type == NODE_FUNCTION_OBJECT) {
        /* Bind nested functions later so closures can refer to later declarations. */
        return;
    } else if (node->vtbl->type == NODE_VARIABLE) {
        variable_t *var = (variable_t *)node;
        declarator_t *declarator = find_symbol_in_scope_and_parents(node->scope, var->name.data);
        if (declarator == NULL) {
            /* Defer the synthetic declaration until traversal finishes. */
            if (options->enable_warnings) {
                compilation_error_t *error =
                    create_error_from_node(memory->errors,
                                           node,
                                           WARNING,
                                           get_messages()->variable_used_before_declaration,
                                           var->name.data);
                error->next = *errors;
                *errors = error;
            }
            node_t *statement = find_parent_statement(var);
            assert(is_statement_list(statement->parent->vtbl->type));
            variable_declaration_pair_t pair =
                create_synthetic_variable_declaration_node(memory->graph, var->name);
            insertion_t *insertion = ALLOC(sizeof(insertion_t));
            insertion->target = statement->parent;
            insertion->item = pair.declaration;
            insertion->before = statement;
            append_to_vector(insertions, insertion);
            var->declarator = pair.declarator;
            add_symbol_to_scope(node->scope, var->name.data, pair.declarator);
        } else {
            var->declarator = declarator;
        }
        return;
    }

    size_t count = get_node_child_count(node);
    for (size_t index = 0; index < count; index++) {
        bind_variables_from_node_and_children(get_node_child(node, index),
                                              memory,
                                              insertions,
                                              errors,
                                              options);
    }
}

/** @brief Binds queued roots in enclosing-before-nested order for closure resolution. */
static void bind_variables_in_functions(queue_t *functions,
                                        parser_memory_t *memory,
                                        vector_t *insertions,
                                        compilation_error_t **errors,
                                        options_t *options) {
    while (!is_queue_empty(functions)) {
        node_t *node = (node_t *)dequeue(functions);
        size_t count = get_node_child_count(node);
        for (size_t index = 0; index < count; index++) {
            bind_variables_from_node_and_children(get_node_child(node, index),
                                                  memory,
                                                  insertions,
                                                  errors,
                                                  options);
        }
    }
}

compilation_error_t *analyze(node_t *root_node,
                             parser_memory_t *memory,
                             options_t *options,
                             analysis_collector_t *collector) {
    scope_t *root_scope = create_scope_from_root_context(memory->graph);

    /* Queue functions in enclosing-before-nested order. */
    unsigned int node_counter = 0;
    root_node->id = ++node_counter;
    queue_t *functions = create_queue();
    enqueue(functions, root_node);
    assign_node_indexes_and_scopes(root_node,
                                   NULL,
                                   functions,
                                   memory->graph,
                                   root_scope,
                                   &node_counter);

    compilation_error_t *errors = NULL;
    vector_t *insertions = create_vector();
    bind_variables_in_functions(functions, memory, insertions, &errors, options);
    destroy_queue(functions);

    /* Traversal is complete; synthetic declarations can now be inserted. */
    for (size_t index = 0; index < insertions->size; index++) {
        insertion_t *insertion = (insertion_t *)insertions->data[index];
        insert_child_node_before(insertion->target, insertion->item, insertion->before);
        assign_scope_to_subtree(insertion->item, insertion->target, insertion->target->scope);
    }
    destroy_vector_ex(insertions, FREE);

    if (options->optimization_level == OPTIMIZATION_NONE)
        return errors;

    interpret(root_node, memory, collector);

    mark_unreachable_code(root_node, memory->graph, collector);
    classify_node_properties(root_node, collector);
    return errors;
}
