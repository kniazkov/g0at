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
#include "interpreter.h"
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
 * @brief Creates the root lexical scope and fills it with built-in symbols.
 *
 * Builds the top-level scope used by the static analyzer. The scope has no
 * parent and is populated from the root runtime context, which contains
 * built-in functions, constants, and other predefined names.
 *
 * Built-in symbols do not have source-level declarations, so they are bound to
 * a shared fake declarator singleton. This gives every resolved built-in a
 * non-NULL declaration target, which is useful for diagnostics, graph edges,
 * and later analysis passes.
 *
 * @param arena Memory arena used to allocate the scope and its bindings.
 * @return Newly created root lexical scope.
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
 * @brief Assigns parent links, lexical scopes, and per-scope node identifiers.
 *
 * Recursively walks the AST in depth-first order. Every visited node receives:
 * - a pointer to its parent node;
 * - a pointer to the lexical scope it belongs to;
 * - a sequential identifier inside that scope.
 *
 * Function objects are also pushed into `functions`. This queue is later used
 * to bind variables function-by-function. The order matters: outer code must be
 * processed before inner function bodies, because closures may reference names
 * declared later in the outer scope.
 *
 * Scope handling rules:
 * - Function objects create a new lexical scope and restart node numbering
 *   inside that function scope from 1.
 * - Statement-list-like nodes create a new lexical scope, but continue the
 *   current numbering sequence.
 * - All other nodes reuse the current scope and current numbering sequence.
 *
 * @param node Current AST node.
 * @param parent Parent node, or NULL for the root.
 * @param functions Queue receiving function objects for later semantic binding.
 * @param arena Memory arena used to allocate newly created scopes.
 * @param scope Lexical scope assigned to the current node.
 * @param next_id Counter used to assign node identifiers in the current scope.
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
                /*
                    Function bodies are analyzed in their own lexical scope.

                    The function node is queued so variable binding can process
                    functions after the enclosing scope has already been scanned.
                    This allows closures to resolve variables declared later in the
                    outer block.
                */
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
                /*
                    A regular statement list creates a nested lexical scope, but it
                    does not restart the execution/topology numbering sequence.
                */
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
                /*
                    Ordinary nodes do not introduce a scope. Their children remain
                    in the current lexical environment.
                */
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
 * @brief Assigns one existing scope and correct parent links to a subtree.
 *
 * Recursively attaches `node` and all descendants to the same lexical scope,
 * while also rebuilding parent links inside the subtree.
 *
 * This helper is used for synthetic AST fragments inserted after the main
 * indexing pass. It intentionally does not assign node ids and does not create
 * new scopes. The synthetic nodes are added late, so they only need enough
 * metadata to be valid tree participants.
 *
 * @param node Current node to update.
 * @param parent Parent node assigned to `node`.
 * @param scope Lexical scope assigned to `node` and all descendants.
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
 * @brief Finds the nearest statement that contains a variable usage.
 *
 * Walks upward from the variable node until it reaches the first ancestor whose
 * type is a statement. This statement is used as the insertion anchor when the
 * analyzer needs to synthesize a declaration for an implicitly introduced
 * variable.
 *
 * @param var Variable node whose enclosing statement should be found.
 * @return Nearest ancestor statement node.
 *
 * @note The function asserts if no statement ancestor exists, because a
 *       variable usage outside a statement is not expected at this analysis
 *       stage.
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

/**
 * @struct insertion_t
 * @brief Deferred AST insertion request.
 *
 * Stores a synthetic node that must be inserted into the tree after variable
 * binding is complete.
 *
 * Insertions are deferred because adding nodes during the binding pass would
 * change traversal structure and node indexes while the analyzer is still
 * walking the existing tree.
 */
typedef struct {
    /**
     * @brief Parent node that receives the inserted child.
     */
    node_t *target;

    /**
     * @brief Synthetic node to insert.
     */
    node_t *item;

    /**
     * @brief Existing child before which `item` should be inserted.
     */
    node_t *before;
} insertion_t;

/**
 * @brief Binds declarations and variable usages inside one AST subtree.
 *
 * Recursively walks a subtree and updates lexical scopes and variable nodes:
 * - declarator nodes are added to the current scope;
 * - function objects are skipped because they are processed separately from
 *   the function queue;
 * - variable nodes are resolved to their declarators through the current scope
 *   chain.
 *
 * If a variable is used before it has a visible declaration, the analyzer:
 * - optionally emits a warning;
 * - creates a synthetic variable declaration node;
 * - stores an insertion request instead of modifying the tree immediately;
 * - binds the variable to the synthetic declarator;
 * - adds the synthetic declarator to the current scope.
 *
 * Deferred insertion keeps the current traversal stable and prevents existing
 * node indexes from shifting during analysis.
 *
 * @param node Current AST node to process.
 * @param memory Parser memory containing arenas used for errors and graph nodes.
 * @param insertions Vector receiving deferred synthetic declaration insertions.
 * @param errors Output linked list of compilation warnings/errors.
 * @param options Command-line options controlling warning emission.
 */
static void bind_variables_from_node_and_children(node_t *node, parser_memory_t *memory,
        vector_t *insertions, compilation_error_t **errors, options_t *options) {
    if (is_declarator(node->vtbl->type)) {
        /*
            Declarators introduce names. Add each declared name to the lexical scope
            associated with the declarator node.
        */
        declarator_t *declarator = (declarator_t*)node;
        add_symbol_to_scope(node->scope, declarator->name.data, declarator);
    }
    else if (node->vtbl->type == NODE_FUNCTION_OBJECT) {
        /*
            Function objects are intentionally skipped here. They are processed later
            from the function queue, after their enclosing scopes have been fully
            scanned. This is required for closures that refer to later declarations.
        */
        return;
    }
    else if (node->vtbl->type == NODE_VARIABLE) {
        /*
            Variable usages are resolved against the current scope and its parents.
            The resolved declarator is stored directly in the variable node.
        */
        variable_t *var = (variable_t*)node;
        declarator_t *declarator = find_symbol_in_scope_and_parents(
            node->scope,
            var->name.data
        );
        if (declarator == NULL) {
            /*
                The name is not declared yet. Treat assignment-created variables
                as implicit declarations by creating a synthetic declaration.
                The declaration is not inserted immediately; only an insertion
                request is recorded.
             */            
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
    /*
        Continue binding through ordinary child nodes.
    */
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
 * @brief Binds variables in queued functions and the root subtree.
 *
 * Processes function-like roots from the queue filled during scope assignment.
 * The queue order follows depth-first discovery order, which ensures that
 * outer scopes are bound before nested function scopes.
 *
 * This order is important for closures: an inner function may reference names
 * declared later in an enclosing scope, so the enclosing function or root must
 * be processed before the inner function body.
 *
 * @param functions Queue of root/function nodes to process.
 * @param memory Parser memory containing arenas used for errors and graph nodes.
 * @param insertions Vector receiving deferred synthetic declaration insertions.
 * @param errors Output linked list of compilation warnings/errors.
 * @param options Command-line options controlling warning emission.
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
    /*
        Build the initial global scope from the runtime root context. This makes
        built-in names visible before user code is analyzed.
     */
    scope_t *root_scope = create_scope_from_root_context(memory->graph);

    /*
        Collect the root and all function objects into a queue while assigning
        parent links, scopes, and node ids. Functions are queued in depth-first
        discovery order, but binding is deferred until after the whole outer
        structure has scopes.
     */
    unsigned int node_counter = 0;
    root_node->id = ++node_counter;
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
    
    /*
        Bind names after scope construction. Synthetic declarations discovered
        during this pass are stored as insertion requests, not inserted yet.
     */
    compilation_error_t *errors = NULL;
    vector_t *insertions = create_vector();
    bind_variables_in_functions(functions, memory, insertions, &errors, options);
    destroy_queue(functions);

    /*
        Apply deferred insertions now that binding traversal is complete.
        Synthetic nodes are attached to the existing scope of their target
        statement list-like parent.
     */    
    for (size_t index = 0; index < insertions->size; index++) {
        insertion_t *insertion = (insertion_t*)insertions->data[index];
        insert_child_node_before(insertion->target, insertion->item, insertion->before);
        assign_scope_to_subtree(insertion->item, insertion->target, insertion->target->scope);
    }
    destroy_vector_ex(insertions, FREE);

    /*
        Perform an abstract interpretation. This will complete the syntax tree.
    */
    interpret(root_node, memory);

    // ... further analysis ...
    return errors;
}
