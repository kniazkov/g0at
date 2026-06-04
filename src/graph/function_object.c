/**
 * @file function_object.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of function object expressions.
 * 
 * This file defines the behavior of function object expressions in the abstract syntax tree (AST).
 * A function object consists of a parameter list (identifiers) and a function body.
 * When evaluated, it produces a callable function value capturing the current context.
 */

 #include <assert.h>

#include "common_methods.h"
#include "expression.h"
#include "statement.h"
#include "declarations.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/linked_list.h"
#include "lib/string_ext.h"
#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/source_builder.h"

/**
 * @struct argument_t
 * @brief Represents a single function argument in the abstract syntax tree.
 *
 * This structure defines a node for one formal argument of a function object
 * (e.g., "x" in "func(x, y) { ... }"). Arguments are represented as separate
 * AST nodes so analysis passes, including data-flow graph construction, can
 * attach edges directly to individual formal parameters instead of relying on
 * a plain list of names.
 */
typedef struct {
    /**
     * @brief Base declarator structure.
     *
     * Stores the argument name and allows the argument to be treated as a
     * regular AST node through the embedded node_t object.
     */
    declarator_t base;
} argument_t;

/**
 * @brief Gets the argument name as string data.
 *
 * Provides access to the formal parameter name stored in the argument node.
 *
 * @param node Pointer to the argument node.
 * @return `string_value_t` containing the argument name.
 */
static string_value_t arg_get_data(const node_t *node) {
    const argument_t *arg = (const argument_t *)node;
    return VIEW_TO_VALUE(arg->base.name);
}

/**
 * @brief Generates Goat source code for a function argument.
 *
 * Produces the textual representation of a single formal parameter.
 *
 * @param node Pointer to the argument node.
 * @return `string_value_t` containing the generated code.
 */
static string_value_t arg_generate_goat_code(const node_t *node) {
    const argument_t *arg = (const argument_t *)node;
    return VIEW_TO_VALUE(arg->base.name);
}

/**
 * @brief Generates indented Goat source code for a function argument.
 *
 * Appends the argument name to the source builder. The argument itself does
 * not introduce indentation; indentation is controlled by the enclosing
 * function object or argument list.
 *
 * @param node Pointer to the argument node.
 * @param builder Pointer to the source builder where generated code is stored.
 * @param indent Current indentation level (unused).
 */
static void arg_generate_indented_goat_code(const node_t *node,
        source_builder_t *builder, size_t indent) {
    const argument_t *arg = (const argument_t *)node;
    append_formatted_source(builder, VIEW_TO_VALUE(arg->base.name));
}

/**
 * @brief Rejects direct bytecode generation for an argument node.
 *
 * Formal arguments are encoded by the enclosing function object, not emitted
 * as standalone bytecode. This method exists to keep the node virtual table
 * complete and to fail fast if code generation is accidentally invoked on an
 * argument node directly.
 *
 * @param node Pointer to the argument node.
 * @param code Pointer to the code builder.
 * @param data Pointer to the data builder.
 * @return Never returns in debug builds; returns BAD_INSTR_INDEX otherwise.
 */
static instr_index_t arg_generate_bytecode(node_t *node, code_builder_t *code,
        data_builder_t *data) {
    assert(false);
    return BAD_INSTR_INDEX;
}

/**
 * @brief Virtual table for function argument nodes.
 *
 * Provides implementations of operations specific to a single formal argument.
 */
static node_vtbl_t arg_vtbl = {
    .type = NODE_ARGUMENT,
    .type_name = L"argument",
    .get_data = arg_get_data,
    .get_child_count = no_children,
    .get_child = no_child,
    .get_child_tag = no_tags,
    .insert_child_before = no_child_insertion,
    .replace_child = no_child_replacement,
    .get_related_count = no_related_nodes,
    .get_related = no_related_node,
    .get_relation_type = no_relation_type,
    .calculate = cannot_calculate,
    .generate_goat_code = arg_generate_goat_code,
    .generate_indented_goat_code = arg_generate_indented_goat_code,
    .generate_bytecode = arg_generate_bytecode,
};

/**
 * @brief Creates a new function argument AST node.
 *
 * Allocates and initializes an argument node with the given formal parameter
 * name. The name is copied to the arena so the node does not depend on parser
 * temporary storage.
 *
 * @param arena Arena allocator to use for node allocation.
 * @param name Formal argument name.
 * @return Pointer to the newly created argument node.
 */
static argument_t *create_argument_node(arena_t *arena, string_view_t name) {
    argument_t *arg =
        (argument_t *)alloc_zeroed_from_arena(arena, sizeof(argument_t));
    arg->base.base.vtbl = &arg_vtbl;
    arg->base.name = copy_string_to_arena(arena, name.data, name.length);
    return arg;
}

/**
 * @struct argument_list_t
 * @brief Represents a list of function arguments in the abstract syntax tree.
 *
 * This structure defines a container node for formal function arguments
 * (e.g., "x, y, z" in "func(x, y, z) { ... }"). Each argument is stored as a
 * separate @ref argument_t child node so graph-based analysis can address every
 * parameter independently.
 */
typedef struct {
    /**
     * @brief Base AST node structure.
     *
     * Allows the argument list to be treated as a regular AST node and attached
     * as a child of a function object node.
     */
    node_t base;

    /**
     * @brief Array of function argument nodes.
     *
     * An arena-allocated array of pointers to @ref argument_t nodes. The array
     * may be NULL when @ref arg_count is zero.
     */
    argument_t **arg_list;

    /**
     * @brief Count of function arguments.
     *
     * Specifies the number of elements in @ref arg_list. Zero is valid for
     * functions without formal parameters.
     */
    size_t arg_count;
} argument_list_t;

/**
 * @brief Gets the child count for an argument list node.
 *
 * Returns the number of formal arguments contained in this list.
 *
 * @param node Pointer to the argument list node.
 * @return The count of child argument nodes.
 */
static size_t alist_get_child_count(const node_t *node) {
    const argument_list_t *list = (const argument_list_t *)node;
    return list->arg_count;
}

/**
 * @brief Retrieves a specific argument child node.
 *
 * Provides access to individual formal argument nodes within the list.
 *
 * @param node Pointer to the argument list node.
 * @param index Zero-based index of the argument to retrieve.
 * @return Pointer to the requested argument node, or NULL if index is invalid.
 */
static node_t *alist_get_child(const node_t *node, size_t index) {
    const argument_list_t *list = (const argument_list_t *)node;
    if (index >= list->arg_count) {
        return NULL;
    }
    return &list->arg_list[index]->base.base;
}

/**
 * @brief Stub for Goat source generation from an argument list.
 *
 * Argument lists are formatted by the enclosing function object. This function
 * must never be called directly.
 *
 * @param node Pointer to the argument list node.
 * @return This function does not return.
 */
static string_value_t alist_generate_goat_code(const node_t *node) {
    assert(false);
    return EMPTY_STRING_VALUE;
}

/**
 * @brief Stub for indented Goat source generation from an argument list.
 *
 * Argument lists are formatted by the enclosing function object. This function
 * must never be called directly.
 *
 * @param node Pointer to the argument list node.
 * @param builder Pointer to the source builder.
 * @param indent Current indentation level.
 */
static void alist_generate_indented_goat_code(const node_t *node,
        source_builder_t *builder, size_t indent) {
    assert(false);
}

/**
 * @brief Rejects direct bytecode generation for an argument list node.
 *
 * Argument lists are consumed by the enclosing function object during bytecode
 * generation. They are not executable nodes and must not emit instructions by
 * themselves.
 *
 * @param node Pointer to the argument list node.
 * @param code Pointer to the code builder.
 * @param data Pointer to the data builder.
 * @return Never returns in debug builds; returns BAD_INSTR_INDEX otherwise.
 */
static instr_index_t alist_generate_bytecode(node_t *node, code_builder_t *code,
        data_builder_t *data) {
    assert(false);
    return BAD_INSTR_INDEX;
}

/**
 * @brief Virtual table for function argument list nodes.
 *
 * Contains function pointers implementing all operations for an argument list
 * container node.
 */
static node_vtbl_t alist_vtbl = {
    .type = NODE_ARGUMENT_LIST,
    .type_name = L"argument list",
    .get_data = no_data,
    .get_child_count = alist_get_child_count,
    .get_child = alist_get_child,
    .get_child_tag = no_tags,
    .insert_child_before = no_child_insertion,
    .replace_child = no_child_replacement,
    .get_related_count = no_related_nodes,
    .get_related = no_related_node,
    .get_relation_type = no_relation_type,
    .calculate = cannot_calculate,
    .generate_goat_code = alist_generate_goat_code,
    .generate_indented_goat_code = alist_generate_indented_goat_code,
    .generate_bytecode = alist_generate_bytecode,
};

/**
 * @brief Creates a new function argument list AST node.
 *
 * Allocates an argument list container and creates one @ref argument_t child
 * node for every formal parameter name in the input array.
 *
 * @param arena Arena allocator to use for node allocation.
 * @param arg_list Array of formal argument names. May be NULL when arg_count is zero.
 * @param arg_count Number of formal arguments.
 * @return Pointer to the newly created argument list node.
 */
static argument_list_t *create_argument_list_node(arena_t *arena, string_view_t *arg_list,
        size_t arg_count) {
    argument_list_t *node =
        (argument_list_t *)alloc_zeroed_from_arena(arena, sizeof(argument_list_t));
    node->base.vtbl = &alist_vtbl;
    node->arg_count = arg_count;

    if (arg_count > 0) {
        assert(arg_list != NULL);
        node->arg_list = (argument_t **)alloc_from_arena(arena,
                arg_count * sizeof(argument_t *));
        for (size_t index = 0; index < arg_count; index++) {
            node->arg_list[index] = create_argument_node(arena, arg_list[index]);
        }
    }

    return node;
}

/**
 * @struct function_body_t
 * @brief AST node that stores the body of a function.
 *
 * Holds a linked list of statements wrapped by curly braces. The node has the
 * same syntactic shape as a regular statement list, but different execution
 * semantics: it does not create an additional lexical environment. The function
 * call itself provides the execution context that contains the formal arguments.
 */
typedef struct {
    /**
     * @brief Base node structure.
     */
    node_t base;

    /**
     * @brief Linked list of statements in the function body.
     *
     * Stores statements in execution order.
     */
    list_t *statements;
} function_body_t;

/**
 * @brief Returns the number of child statements in the function body.
 *
 * @param node Pointer to the function body node.
 * @return The number of child statements, or 0 for an empty body.
 */
static size_t fbody_get_child_count(const node_t *node) {
    const function_body_t* body = (const function_body_t*)node;
    return body->statements->size;
}

/**
 * @brief Retrieves a specific child statement from the function body.
 *
 * Performs bounds-checked access. Valid indices are in the range [0, stmt_count).
 *
 * @param node Pointer to the function body node.
 * @param index Zero-based statement index.
 * @return Pointer to the child node, or NULL if index is out of bounds.
 */
static node_t* fbody_get_child(const node_t *node, size_t index) {
    const function_body_t* body = (const function_body_t*)node;
    return (node_t*)get_linked_list_value(body->statements, index).ptr;
}

/**
 * @brief Inserts a child statement before another child statement.
 *
 * Searches the function body statement list for `before_child` and inserts
 * `new_child` immediately before it. The function body accepts only statements
 * as children; if `before_child` is not found, or `new_child` is not a statement
 * node, the body remains unchanged and the function returns `false`.
 *
 * This is used by static analysis to inject synthetic statements while
 * preserving execution order.
 *
 * @param node Pointer to the function body node.
 * @param new_child Statement node to insert.
 * @param before_child Existing child statement before which insertion should happen.
 * @return `true` if insertion succeeded, otherwise `false`.
 */
static bool fbody_insert_child_before(node_t *node, node_t *new_child,
        node_t *before_child) {
    if (!is_statement(new_child->vtbl->type)) {
        return false;
    }

    function_body_t* body = (function_body_t*)node;
    list_item_t *item = body->statements->head;
    while(item) {
        if (item->value.ptr == before_child) {
            break;
        }
        item = item->next;
    }
    if (!item) {
        return false;
    }

    insert_item_to_linked_list_before_existing(
        body->statements,
        item,
        (value_t){ .ptr = new_child }
    );
    return true;
}

/**
 * @brief Stub for Goat source generation from a function body.
 *
 * Function bodies are formatted by the enclosing function object. This function
 * must never be called directly.
 *
 * @param node Pointer to the function body node.
 * @return This function does not return.
 */
static string_value_t fbody_generate_goat_code(const node_t *node) {
    assert(false);
    return EMPTY_STRING_VALUE;
}

/**
 * @brief Stub for indented Goat source generation from a function body.
 *
 * Function bodies are formatted by the enclosing function object. This function
 * must never be called directly.
 *
 * @param node Pointer to the function body node.
 * @param builder Source builder accumulating the output.
 * @param indent Base indentation level.
 */
static void fbody_generate_indented_goat_code(const node_t *node, source_builder_t *builder,
        size_t indent) {
    assert(false);
}

/**
 * @brief Stub for bytecode generation from a function body.
 *
 * Function body bytecode is emitted by the enclosing function object as deferred
 * code. This function must never be called directly.
 *
 * @param node Pointer to the function body node.
 * @param code Code builder receiving emitted instructions.
 * @param data Data builder for the constant pool.
 * @return This function does not return.
 */
static instr_index_t fbody_generate_bytecode(node_t *node, code_builder_t *code,
        data_builder_t *data) {
    assert(false);
    return BAD_INSTR_INDEX;
}

/**
 * @brief Virtual table for function_body node operations.
 */
static node_vtbl_t function_body_vtbl = {
    .type = NODE_FUNCTION_BODY,
    .type_name = L"function body",
    .get_data = no_data,
    .get_child_count = fbody_get_child_count,
    .get_child = fbody_get_child,
    .get_child_tag = no_tags,
    .insert_child_before = fbody_insert_child_before,
    .replace_child = no_child_replacement,
    .get_related_count = no_related_nodes,
    .get_related = no_related_node,
    .get_relation_type = no_relation_type,
    .calculate = cannot_calculate,
    .generate_goat_code = fbody_generate_goat_code,
    .generate_indented_goat_code = fbody_generate_indented_goat_code,
    .generate_bytecode = fbody_generate_bytecode
};

/**
 * @brief Creates a new function body AST node.
 *
 * @param arena Arena allocator for node allocation.
 * @return Pointer to the newly created function body node.
 */
static function_body_t *create_function_body_node(arena_t *arena) {
    function_body_t *body = (function_body_t *)alloc_zeroed_from_arena(
        arena,
        sizeof(function_body_t)
    );
    body->base.vtbl = &function_body_vtbl;
    return body;
}

/**
 * @struct function_object_t
 * @brief Represents a function object expression in the AST.
 * 
 * This structure defines a function expression node that encapsulates a list
 * of parameter names and a function body. When evaluated, it produces a function
 * value that can be called with arguments.
 */
typedef struct {
    /**
     * @brief Base expression structure from which function_object_t inherits.
     * 
     * This allows the node to be treated as an expression in the AST while
     * providing the necessary functionality for tree traversal and manipulation.
     */
    expression_t base;

    /**
     * @brief Formal argument list for the function.
     *
     * Contains one child node per declared argument.
     */
    argument_list_t *arguments;

    /**
     * @brief Function body.
     *
     * Contains the statements executed when the function is called.
     */
    function_body_t *body;

    /**
     * @brief Index of the `ARG` instruction containing index of the first instruction 
     *  of the function body.
     * 
     * This points to the entry instruction in the generated bytecode,
     * used when the function is called.
     */
    instr_index_t code_instr_index;
} function_object_t;

/**
 * @brief Gets the number of child nodes in a function object.
 *
 * A function object always exposes two child nodes:
 * the formal argument list and the function body.
 *
 * @param node Pointer to the function object node.
 * @return Always returns 2.
 */
static size_t fobj_get_child_count(const node_t *node) {
    return 2;
}

/**
 * @brief Retrieves a specific child node from a function object.
 *
 * A function object exposes its children in a fixed order:
 * index 0 is the formal argument list, and index 1 is the function body.
 *
 * @param node Pointer to the function object node.
 * @param index Zero-based child index.
 * @return Pointer to the requested child node, or NULL if index is out of bounds.
 */
static node_t* fobj_get_child(const node_t *node, size_t index) {
    const function_object_t* expr = (const function_object_t*)node;
    if (index == 0) {
        return &expr->arguments->base;
    }
    if (index == 1) {
        return &expr->body->base;
    }
    return NULL;
}

/**
 * @brief Gets the semantic tag for a function object child node.
 *
 * Tags identify the role of each child in the function object:
 * index 0 is tagged as "arguments", and index 1 is tagged as "body".
 *
 * @param node Pointer to the function object node.
 * @param index Zero-based child index.
 * @return Static wide string tag for the child, or NULL if index is out of bounds.
 */
static const wchar_t* fobj_get_child_tag(const node_t *node, size_t index) {
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
 *
 * Constructs a string like `func(arg1, arg2, ...) {` using the parameter list
 * from the given function object. Appends the result to the provided string builder.
 *
 * @param expr Pointer to the function object expression.
 * @param builder Pointer to the string builder used to accumulate the output.
 * @return The resulting string after appending the header.
 */
static string_value_t generate_header(const function_object_t* expr, string_builder_t *builder) {
    append_static_string(builder, L"func(");
    for (size_t index = 0; index < expr->arguments->arg_count; index++) {
        if (index > 0) {
            append_static_string(builder, L", ");
        }
        append_string_view(builder, expr->arguments->arg_list[index]->base.name);
    }
    return append_static_string(builder, L") {");
}

/**
 * @brief Converts a function object node to its compact Goat syntax representation.
 * 
 * Generates a single-line canonical Goat representation of the function, including:
 * - The function keyword with its parameters: `func(arg1, arg2, ...)`
 * - A block body with all statements concatenated and space-separated
 * 
 * Example: `func(x, y) { return x + y }`
 * 
 * @param node Pointer to the function object node.
 * @return `string_value_t` containing the generated code.
 */
static string_value_t fobj_generate_goat_code(const node_t *node) {
    const function_object_t* expr = (const function_object_t*)node;
    string_builder_t builder;
    init_string_builder(&builder, 128);
    generate_header(expr, &builder);
    list_item_t *item = expr->body->statements->head;
    bool needs_space = false;
    while (item) {
        if (needs_space) {
            append_char(&builder, L' ');
        }
        needs_space = true;
        statement_t *stmt = (statement_t*)item->value.ptr;
        string_value_t stmt_as_string = generate_goat_code_from_statement(stmt);
        append_string_value(&builder, stmt_as_string);
        FREE_STRING(stmt_as_string);
        item = item->next;
    }
    return append_char(&builder, L'}');
}

/**
 * @brief Generates a multi-line, indented Goat source code representation of the function.
 * 
 * Produces a human-readable version of the function definition with proper formatting:
 * - Header and braces placed on separate lines
 * - Statements indented according to nesting level
 * - Supports arbitrary levels of indentation
 * 
 * Example output:
 * ```
 * func(x, y) {
 *     return x + y
 * }
 * ```
 *
 * @param node Pointer to the function object node.
 * @param builder Output accumulator for the generated source code.
 * @param indent Base indentation level (number of tabs).
 */
static void fobj_generate_indented_goat_code(const node_t *node, source_builder_t *builder,
        size_t indent) {
    const function_object_t* expr = (const function_object_t*)node;
    string_builder_t header;
    init_string_builder(&header, 16);
    append_formatted_source(builder, generate_header(expr, &header));
    list_item_t *item = expr->body->statements->head;
    while (item) {
        statement_t *stmt = (statement_t*)item->value.ptr;
        generate_indented_goat_code_from_statement(stmt, builder, indent + 1);
        item = item->next;
    }
    add_static_source(builder, indent, L"}");
}

/**
 * @brief Generates the main bytecode for a function object expression.
 * 
 * This function emits the initial instruction(s) required to represent a function object
 * in the bytecode stream. It prepares a placeholder instruction (`ARG`) for body address
 * and emits a  `FUNC` instruction with encoded parameter information.
 * 
 * The actual function body is not generated here — that is handled by `generate_bytecode_deferred`.
 * 
 * @param node A pointer to the function object node.
 * @param code A pointer to the bytecode builder.
 * @param data A pointer to the static data segment builder.
 * @return The instruction index of the first emitted instruction.
 */
static instr_index_t fobj_generate_bytecode(node_t *node, code_builder_t *code,
        data_builder_t *data) {
    function_object_t* expr = (function_object_t*)node;
    instr_index_t first = expr->code_instr_index = add_instruction(
        code,
        (instruction_t){ .opcode = ARG, .arg1 = 0xFFFFFFFF } // placeholder
    );
    uint32_t arg_names_idx = 0;
    if (expr->arguments->arg_count > 0) {
        size_t arg_size = expr->arguments->arg_count * sizeof(uint32_t);
        uint32_t *arg_names = (uint32_t*)ALLOC(arg_size);
        for (size_t index = 0; index < expr->arguments->arg_count; index++) {
            arg_names[index] = add_string_to_data_segment_ex(
                data,
                expr->arguments->arg_list[index]->base.name
            );
        }
        arg_names_idx = add_data_to_data_segment(data, arg_names, arg_size);
        FREE(arg_names);
    }
    add_instruction(
        code,
        (instruction_t) {
            .opcode = FUNC,
            .arg0 = (uint16_t)expr->arguments->arg_count,
            .arg1 = arg_names_idx
        }
    );
    return first;
}

/**
 * @brief Attempts to generate deferred bytecode for a function object's body.
 *
 * Emits the bytecode instructions that implement the function body. This is
 * invoked separately from the main (non-deferred) codegen phase so that
 * function definitions can exist before their bodies are emitted.
 *
 * Readiness & multi-pass behavior:
 * - If the call site / jump placeholder for this function is not yet known
 *   (i.e., there is no valid instruction slot to patch with the entry address),
 *   the function returns `false` and emits nothing. The caller should
 *   schedule another pass later.
 * - When the placeholder is known, the function emits the body and patches
 *   the previously reserved instruction with the entry point; in this case
 *   it returns `true`.
 *
 * @param node Pointer to the function object node.
 * @param code Bytecode builder that receives emitted instructions.
 * @param data Static data (constant pool) builder.
 * @return `true` if the function body was emitted and the call site patched;
 *  `false` if required information is missing and another pass is needed.
 */
static bool fobj_generate_bytecode_deferred(const node_t *node, code_builder_t *code,
        data_builder_t *data) {
    function_object_t* expr = (function_object_t*)node;
    if (expr->code_instr_index == BAD_INSTR_INDEX) {
        return false;
    }
    instr_index_t first;
    if (expr->body->statements->size == 0) {
        first = add_instruction(code, (instruction_t){ .opcode = NIL });
        add_instruction(code, (instruction_t){ .opcode = RET });
    }
    else {
        list_item_t *item = expr->body->statements->head;
        statement_t *stmt = (statement_t*)item->value.ptr;
        first = generate_bytecode_from_statement(stmt, code, data);

        while (item->next) {
            item = item->next;
            stmt = (statement_t*)item->value.ptr;
            generate_bytecode_from_statement(stmt, code, data);
        }

        if (stmt->base.vtbl->type != NODE_RETURN) {
            add_instruction(code, (instruction_t){ .opcode = NIL });
            add_instruction(code, (instruction_t){ .opcode = RET });
        }
    }
    code->instructions[expr->code_instr_index].arg1 = (uint32_t)first;
    return true;
}

/**
 * @brief Virtual table for function object node operations.
 * 
 * This virtual table provides the implementation of operations specific to function object nodes
 * in the abstract syntax tree (AST).
 */
static node_vtbl_t fo_vtbl = {
    .type = NODE_FUNCTION_OBJECT,
    .type_name = L"function object",
    .get_data = no_data,
    .get_child_count = fobj_get_child_count,
    .get_child = fobj_get_child,
    .get_child_tag = fobj_get_child_tag,
    .insert_child_before = no_child_insertion,
    .replace_child = no_child_replacement,
    .get_related_count = no_related_nodes,
    .get_related = no_related_node,
    .get_relation_type = no_relation_type,
    .calculate = cannot_calculate,
    .generate_goat_code = fobj_generate_goat_code,
    .generate_indented_goat_code = fobj_generate_indented_goat_code,
    .generate_bytecode = fobj_generate_bytecode,
    .generate_bytecode_deferred = fobj_generate_bytecode_deferred
};

node_t *create_function_object_node(arena_t *arena, string_view_t *arg_list, size_t arg_count) {
    function_object_t *fobj = (function_object_t*)alloc_zeroed_from_arena(arena,
        sizeof(function_object_t));
    fobj->base.base.vtbl = &fo_vtbl;
    fobj->arguments = create_argument_list_node(arena, arg_list, arg_count);
    fobj->body = create_function_body_node(arena);
    return &fobj->base.base;
}

void fill_function_body(node_t *node, list_t *statements) {
    assert(node->vtbl->type == NODE_FUNCTION_OBJECT);
    function_object_t *fobj = (function_object_t *)node;
    fobj->body->statements = statements;
    fobj->code_instr_index = BAD_INSTR_INDEX;
}
