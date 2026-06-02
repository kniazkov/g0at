/**
 * @file analysis.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of static code analysis functions.
 *
 * This file provides the core implementation of the static analysis subsystem.
 * It walks the abstract syntax tree (AST), constructs and propagates scope
 * information, assigns node and scope identifiers, builds symbol bindings, and
 * performs semantic checks. These steps ensure that the code is structurally
 * and semantically correct before further compilation stages such as
 * optimization and code generation.
 */

#include <assert.h>

#include "analysis.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/queue.h"
#include "lib/vector.h"
#include "cli/options.h"
#include "common/compilation_error.h"
#include "graph/node.h"
#include "graph/declarations.h"
#include "graph/variable.h"
#include "model/context.h"
#include "model/object.h"
#include "resources/messages.h"

/**
 * @brief Creates a new scope initialized from the root execution context.
 *
 * This function builds a fresh lexical scope using global bindings from the
 * root context. It does the following:
 * 1. Creates a new scope with no parent (global scope).
 * 2. Retrieves all keys from the root context's data object.
 * 3. Inserts each key as a symbol into the scope, associating it with the
 *    given root node.
 *
 * This allows the  analyzer to start with a scope that already contains globally visible symbols,
 * e.g. built-in objects and standard library functions.
 *
 * @param arena Memory arena from which the scope and its bindings are allocated.
 * @return A pointer to the newly created and populated scope.
 */
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
 * @brief Recursively assigns node identifiers and connects nodes to lexical scopes.
 *
 * This function performs a depth-first traversal of the abstract syntax tree (AST),
 * assigning sequential identifiers to nodes and creating new scopes where required.
 * The numbering and scoping rules are:
 *
 * - For a node of type NODE_FUNCTION_OBJECT:
 *   - The function node itself is assigned the next identifier in the current scope.
 *   - A new scope is created for its body.
 *   - Node identifiers inside this new scope start from 1.
 *
 * - For a node of type NODE_STATEMENT_LIST:
 *   - The statement list node itself is assigned the next identifier in the current scope.
 *   - A new scope is created for its children.
 *   - Node identifiers continue from the current sequence (no reset).
 *
 * - For all other node types:
 *   - The node is assigned the next identifier in the current scope.
 *   - Its children are analyzed within the same scope and numbering sequence.
 *
 * @param node      Current AST node to analyze.
 * @param parent    Parent node.
 * @param functions ....
 * @param arena     Memory arena used for allocating new scopes.
 * @param scope     Scope that this node belongs to.
 * @param next_id   Pointer to the counter of node identifiers in the current scope.
 *  Updated as nodes are assigned ids.
 *
 * @note Each scope has its own sequence of node ids starting from 1, except
 *  statement list scopes which continue the numbering of their parent.
 */
static void assign_node_indexes_and_scopes(node_t *node, node_t *parent, queue_t *functions,
        arena_t *arena, scope_t *scope, unsigned int *next_id) {
    node->parent = parent;
    node->scope = scope;
    node->id = (*next_id)++;
    const size_t child_count = get_node_child_count(node);
    for (size_t child_id = 0; child_id < child_count; child_id++) {
        node_t *child = get_node_child(node, child_id);
        switch (child->vtbl->type) {
            case NODE_FUNCTION_OBJECT: {
                enqueue(functions, child);
                scope_t *inner_scope = create_scope(arena, scope);
                unsigned int inner_counter = 1;
                assign_node_indexes_and_scopes(
                    child,
                    node,
                    functions,
                    arena,
                    inner_scope,
                    &inner_counter
                );
                break;
            }
            case NODE_STATEMENT_LIST: {
                scope_t *inner_scope = create_scope(arena, scope);
                assign_node_indexes_and_scopes(
                    child,
                    node,
                    functions,
                    arena,
                    inner_scope,
                    next_id
                );
                break;
            }
            default: {
                assign_node_indexes_and_scopes(
                    child,
                    node,
                    functions,
                    arena,
                    scope,
                    next_id
                );
                break;
            }
        }
    }
}

/**
 * @brief Recursively assigns the same scope and parent links to a subtree.
 *
 * Walks through `node` and all of its children, assigning the specified lexical
 * scope to every visited node and wiring each node to its parent.
 *
 * This helper is intended for synthetic AST fragments inserted after the main
 * static-analysis indexing pass. It does not assign node ids and does not create
 * new scopes; it simply attaches the whole subtree to an existing scope, because
 * the fake nodes are already late to the party and do not get the full ceremony.
 *
 * @param node Current AST node to update.
 * @param parent Parent node to assign to `node`.
 * @param scope Lexical scope to assign to `node` and all descendants.
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

/**
 * ...
 */
static node_t *find_parent_statement(variable_t *var) {
    node_t *node = var->base.base.base.parent;
    while(node) {
        if (is_statement(node->vtbl->type)) {
            return node;
        }
        node = node->parent;
    }
    assert(false);
}

typedef struct {
    node_t *target;
    node_t *item;
    node_t *before;
} insertion_t;

/**
 * ...
 */
static void bind_variables_from_node_and_children(node_t *node, parser_memory_t *memory,
        vector_t *insertions, compilation_error_t **errors, options_t *options) {
    if (is_declarator(node->vtbl->type)) {
        declarator_t *declarator = (declarator_t*)node;
        add_symbol_to_scope(node->scope, declarator->name.data, declarator);
    }
    else if (node->vtbl->type == NODE_FUNCTION_OBJECT) {
        return;
    }
    else if (node->vtbl->type == NODE_VARIABLE) {
        variable_t *var = (variable_t*)node;
        const declarator_t *declarator = find_symbol_in_scope_and_parents(
            node->scope,
            var->name.data
        );
        if (declarator == NULL) {
            if (options->enable_warnings) {
                compilation_error_t *error = create_error_from_node(
                    memory->errors,
                    node,
                    WARNING,
                    get_messages()->variable_used_before_declaration,
                    var->name.data
                );
                error->next = *errors;
                *errors = error;
            }
            node_t *statement = find_parent_statement(var);
            assert(is_statement_list(statement->parent->vtbl->type));
            variable_declaration_pair_t pair = create_synthetic_variable_declaration_node(
                memory->graph,
                var->name
            );
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
        bind_variables_from_node_and_children(
            get_node_child(node, index),
            memory,
            insertions,
            errors,
            options
        );
    }
}

/**
 * ...
 */
static void bind_variables_in_functions(queue_t *functions, parser_memory_t *memory,
        vector_t *insertions, compilation_error_t **errors, options_t *options) {
    while(!is_queue_empty(functions)) {
        node_t *node  = (node_t*)dequeue(functions);
        size_t count = get_node_child_count(node);
        for (size_t index = 0; index < count; index++) {
            bind_variables_from_node_and_children(
                get_node_child(node, index),
                memory,
                insertions,
                errors,
                options
            );
        }
    }
}

compilation_error_t *analyze(node_t *root_node, parser_memory_t *memory, options_t *options) {
    scope_t *root_scope = create_scope_from_root_context(memory->graph);
    unsigned int node_counter = 0;
    queue_t *functions = create_queue();
    enqueue(functions, root_node);
    assign_node_indexes_and_scopes(
        root_node,
        NULL,
        functions,
        memory->graph,
        root_scope,
        &node_counter
    );
    
    compilation_error_t *errors = NULL;
    vector_t *insertions = create_vector();
    bind_variables_in_functions(functions, memory, insertions, &errors, options);
    destroy_queue(functions);

    for (int index = 0; index < insertions->size; index++) {
        insertion_t *insertion = (insertion_t*)insertions->data[index];
        insert_child_node_before(insertion->target, insertion->item, insertion->before);
        assign_scope_to_subtree(insertion->item, insertion->target, insertion->target->scope);
    }
    destroy_vector_ex(insertions, FREE);

    // ... further analysis ...
    return errors;
}
