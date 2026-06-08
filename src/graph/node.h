/**
 * @file node.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Definitions for the basic node structure of the abstract syntax tree (AST).
 * 
 * This file contains the definition of the basic node structure for the abstract syntax tree (AST).
 * The AST is a tree representation of the syntactic structure of source code, where each node
 * represents a language construct (e.g., expression, statement, or declaration).
 */

#pragma once

#include "node_type.h"
#include "relation_type.h"
#include "scope.h"
#include "lib/value.h"
#include "common/position.h"
#include "common/types.h"

/**
 * @typedef node_t
 * @brief Forward declaration for the node structure.
 */
typedef struct node_t node_t;

/**
 * @typedef statement_t
 * @brief Forward declaration for the statement structure.
 */
typedef struct statement_t statement_t;

/**
 * @typedef expression_t
 * @brief Forward declaration for the expression structure.
 */
typedef struct expression_t expression_t;

/**
 * @typedef source_builder_t
 * @brief Forward declaration for the source code builder structure.
 */
typedef struct source_builder_t source_builder_t;

/**
 * @typedef code_builder_t
 * @brief Forward declaration for the code builder structure.
 */
typedef struct code_builder_t code_builder_t;

/**
 * @typedef data_builder_t
 * @brief Forward declaration for the data builder structure.
 */
typedef struct data_builder_t data_builder_t;

/**
 * @typedef arena_t
 * @brief Forward declaration for the arena memory allocator.
 */
typedef struct arena_t arena_t;

/**
 * @typedef list_t
 * @brief Forward declaration for the linked list structure.
 */
typedef struct list_t list_t;

/**
 * @typedef lattice_element_t
 * @brief Forward declaration for an abstract-interpretation lattice element.
 */
typedef struct lattice_element_t lattice_element_t;

/**
 * @typedef abstract_state_t
 * @brief Forward declaration for an abstract interpreter state.
 */
typedef struct abstract_state_t abstract_state_t;

/**
 * @struct node_vtbl_t
 * @brief The virtual table structure for nodes in the syntax tree.
 * 
 * This structure contains pointers to functions that implement the behavior of different types
 * of nodes in the syntax tree. The virtual table allows polymorphism, enabling different node
 * types to have their own implementations for specific operations.
 */
typedef struct {
    /**
     * @brief The type of the node.
     * 
     * Identifies the specific type of the syntax tree node. Used for distinguishing between
     * different node implementations.
     */
    node_type_t type;

    /**
     * @brief Human-readable type name of the node.
     * 
     * A wide character string representing the type name of the node
     * (e.g., "function declaration", "addition", "variable").
     */
    const wchar_t* type_name;

    /**
     * @brief Flag indicating whether the node represents an assignable expression (lvalue).
     * 
     * When true, indicates that:
     * - The node can appear on the left-hand side of an assignment
     * - The `generate_bytecode_assign` method must be implemented
     * - The node represents a valid storage location (variable, array access, etc.)
     */
    bool is_assignable_expression;

    /**
     * @brief Gets the string representation of the node's data.
     * 
     * Returns the primary data associated with the node in string form.
     * For example:
     * - For an identifier node: the identifier name
     * - For a literal node: the literal value
     * 
     * @param node A pointer to the node.
     * @return A `string_value_t` containing the node's data or empty value if none.
     */
    string_value_t (*get_data)(const node_t *node);

    /**
     * @brief Gets the number of properties exposed by this node.
     *
     * Returns how many properties this node provides for debugging or visualization
     * purposes. A property is a named attribute represented as a key–value pair
     * (e.g., "name" → "foo", "type" → "int").
     * If zero, the node exposes no additional properties.
     *
     * @param node A pointer to the node.
     * @return Number of properties available for this node.
     */
    size_t (*get_property_count)(const node_t *node);

    /**
     * @brief Retrieves a property of this node by index.
     *
     * Provides the indexed property (key–value pair) from the node.
     *
     * @param node A pointer to the node.
     * @param index Zero-based index in range [0, get_property_count(node)).
     * @param out_key Output pointer to receive the property key (name).
     * @param out_value Output pointer to receive the property value.
     */
    void (*get_property)(const node_t *node, size_t index,
                         string_view_t *out_key, string_value_t *out_value);

    /**
     * @brief Gets the number of child nodes.
     * 
     * Returns the count of direct child nodes for this syntax tree node.
     * 
     * @param node A pointer to the node.
     * @return Number of child nodes (0 for leaf nodes).
     */
    size_t (*get_child_count)(const node_t *node);

    /**
     * @brief Gets a child node by index.
     * 
     * Retrieves a specific child node from the syntax tree.
     * 
     * @param node A pointer to the parent node.
     * @param index Zero-based index of the child node.
     * @return Pointer to the child node or NULL if index is out of range.
     */
    node_t* (*get_child)(const node_t *node, size_t index);

    /**
     * @brief Gets the tag/label for a child node.
     * 
     * Returns a descriptive wide character string that labels the relationship
     * between the parent node and the specified child node (e.g., "left", "right", 
     * "condition", "body", "parameters").
     * 
     * @param node A pointer to the parent node.
     * @param index Zero-based index of the child node.
     * @return Wide character string (const wchar_t*) with the child's tag or NULL
     * if not applicable.
     */
    const wchar_t* (*get_child_tag)(const node_t *node, size_t index);

    /**
     * @brief Inserts a child node before another child node.
     *
     * Attempts to insert `new_child` into this node before `before_child`.
     * The concrete node implementation decides whether insertion is supported,
     * which child types are valid, and where insertion is allowed.
     *
     * If the function returns `false`, the node must remain unchanged. After the
     * call, regardless of the result, the node must still be structurally valid.
     *
     * This method is used by static analysis to inject synthetic AST nodes.
     *
     * @param node A pointer to the parent node to modify.
     * @param new_child A pointer to the child node to insert.
     * @param before_child A pointer to the existing child before which
     *        `new_child` should be inserted.
     * @return `true` if the child was inserted, otherwise `false`.
     */
    bool (*insert_child_before)(node_t *node, node_t *new_child, node_t *before_child);

    /**
     * @brief Replaces one child node with another child node.
     *
     * Attempts to replace `old_child` with `new_child`. The concrete node
     * implementation decides whether replacement is supported and which node
     * types are valid for the replacement.
     *
     * If the function returns `false`, the node must remain unchanged. After the
     * call, regardless of the result, the node must still be structurally valid.
     *
     * This method is used by static analysis to replace AST nodes with simpler
     * equivalent nodes.
     *
     * @param node A pointer to the parent node to modify.
     * @param old_child A pointer to the existing child node to replace.
     * @param new_child A pointer to the replacement child node.
     * @return `true` if the child was replaced, otherwise `false`.
     */
    bool (*replace_child)(node_t *node, node_t *old_child, node_t *new_child);

    /**
     * @brief Gets the number of related nodes.
     *
     * Returns the number of nodes related to this node outside the direct AST
     * child hierarchy. Related nodes represent semantic or analysis-level links,
     * such as a variable usage referring to its declaration.
     *
     * @param node A pointer to the node.
     * @return Number of related nodes.
     */
    size_t (*get_related_count)(const node_t *node);

    /**
     * @brief Gets a related node by index.
     *
     * Retrieves a node related to this node outside the direct AST child
     * hierarchy.
     *
     * @param node A pointer to the node.
     * @param index Zero-based related-node index.
     * @return Pointer to the related node or NULL if index is out of range.
     */
    const node_t* (*get_related)(const node_t *node, size_t index);

    /**
     * @brief Gets the relation type for a related node.
     *
     * Returns the semantic relation type between this node and the related node
     * at the specified index.
     *
     * @param node A pointer to the node.
     * @param index Zero-based related-node index.
     * @return Type of relation, or RELATION_NONE if index is out of range.
     */
    relation_type_t (*get_relation_type)(const node_t *node, size_t index);

    /**
     * @brief Calculates the abstract lattice element represented by this node.
     *
     * Used by static analysis to infer the abstract value produced by a syntax-tree
     * node. Concrete node implementations may override this method.
     *
     * @param node A pointer to the node.
     * @param arena Memory arena for allocating lattice elements.
     * @return Constant pointer to the calculated lattice element.
     */
    const lattice_element_t *(*calculate)(const node_t *node, arena_t *arena);

    /**
     * @brief Executes abstract interpretation for this node.
     *
     * Takes the current abstract state, applies the node-specific abstract
     * semantics, and returns the resulting abstract state. The returned state
     * may be the same object or a newly allocated state.
     *
     * @param node A pointer to the node.
     * @param state Input abstract state.
     * @param arena Memory arena for allocating lattice elements.
     * @return Output abstract state after interpreting this node.
     */
    abstract_state_t *(*execute)(node_t *node, abstract_state_t *state, arena_t *arena);

    /**
     * @brief Generates a single-line Goat source code representation of the node.
     * 
     * This function returns a compact Goat source code representation of the node
     * without indentation.
     * While the generated code is functionally equivalent to the original source from which 
     * the syntax tree was created, it may not be a literal reproduction due to optimizations 
     * or transformations applied during parsing and processing.
     * 
     * @param node A pointer to the node.
     * @return A `string_value_t` containing the generated Goat code.
     */
    string_value_t (*generate_goat_code)(const node_t *node);

    /**
     * @brief Generates indented Goat source code for the node, if applicable.
     * 
     * This function produces formatted Goat source code with proper indentation.
     * It applies only to node types that logically support indentation (e.g., blocks, functions).
     * The generated code is functionally equivalent to the original, but may differ in formatting 
     * and structure due to optimizations or transformations.
     * 
     * @param node A pointer to the node.
     * @param builder A pointer to the `source_builder_t` to store the generated output.
     * @param indent The number of tabs used for indentation.
     */
    void (*generate_indented_goat_code)(const node_t *node, source_builder_t *builder,
            size_t indent);

    /**
     * @brief Checks if C code generation is possible for this node.
     * 
     * This function determines whether the node has a valid representation in C.
     * If `true`, `to_c()` and `to_c_indent()` will return meaningful output.
     * 
     * @param node A pointer to the node.
     * @return `true` if C generation is supported, `false` otherwise.
     */
    bool (*can_generate_c_code)(const node_t *node);

    /**
     * @brief Generates a single-line C source code representation of the node, if possible.
     * 
     * This function attempts to produce an equivalent C representation of the node without
     * indentation. If the node cannot be represented in C, it returns NULL string.
     * 
     * @param node A pointer to the node.
     * @return A `string_value_t` containing the generated C code or NULL string if conversion
     *  is not possible.
     */
    string_value_t (*generate_c_code)(const node_t *node);

    /**
     * @brief Generates indented C source code for the node, if applicable.
     * 
     * This function attempts to generate formatted C code with proper indentation.
     * It applies only to nodes that have a valid C equivalent and require indentation
     * (e.g., function bodies, loops).
     * 
     * @param node A pointer to the node.
     * @param builder A pointer to the `source_builder_t` to store the generated output.
     * @param indent The number of tabs used for indentation.
     */
    void (*generate_indented_c_code)(const node_t *node, source_builder_t *builder, size_t indent);

    /**
     * @brief Generates bytecode for the given node.
     * 
     * This function generates the bytecode instructions for a node in the syntax tree. It takes
     * two additional arguments, `code_builder_t` and `data_builder_t`, which are used to manage
     * the bytecode instructions and static data respectively. The node's specific implementation
     * will determine how it contributes to the final bytecode output.
     * 
     * @param node A pointer to the node for which bytecode is being generated.
     * @param code A pointer to the `code_builder_t` used to add bytecode instructions.
     * @param data A pointer to the `data_builder_t` used to manage static data.
     * @return The instruction index of the first emitted instruction.
     */
    instr_index_t (*generate_bytecode)(node_t *node, code_builder_t *code, data_builder_t *data);

    /**
     * @brief Generates bytecode for storing a value into this expression.
     * 
     * This method generates the bytecode needed to store a value (expected to be
     * on top of the stack) into the location represented by this expression.
     * Should only be implemented for assignable expressions (lvalues).
     * 
     * @param node A pointer to the target node.
     * @param code A pointer to the code builder for bytecode generation.
     * @param data A pointer to the data builder for static data management.
     * @return The instruction index of the first emitted instruction.
     */
    instr_index_t (*generate_bytecode_assign)(const node_t *node, code_builder_t *code,
        data_builder_t *data);

    /**
     * @brief Generates deferred bytecode for the given node.
     * 
     * This method generates "deferred" bytecode for a node — code that is not executed immediately
     * during the main flow, but instead triggered later, such as function bodies
     * or lazy expressions.
     * It returns the index of the first instruction of the generated code, which can later be used
     * to reference or jump to this code block.
     * 
     * @param node A pointer to the node for which deferred bytecode is being generated.
     * @param code A pointer to the code builder for bytecode generation.
     * @param data A pointer to the data builder for static data management.
     * @return `true` if the deferred bytecode was successfully generated
     *  in this pass; `false` if required information is still missing and another generation
     *  pass is needed.
     */
    bool (*generate_bytecode_deferred)(const node_t *node, code_builder_t *code,
        data_builder_t *data);
} node_vtbl_t;

/**
 * @struct node_t
 * @brief The base structure for nodes in the syntax tree.
 * 
 * This structure represents a base node in the syntax tree of the Goat language. All nodes,
 * regardless of their specific type, share this common structure which contains a reference to
 * their virtual table. The virtual table allows polymorphic behavior for nodes of different types,
 * enabling specific behavior for each node type in the tree.
 * 
 * Nodes in the syntax tree represent various constructs in the parsed source code, and can
 * be extended with additional fields for specific node types as necessary.
 */
struct node_t {
    /**
     * @brief Pointer to the node's virtual table.
     * 
     * This field holds a pointer to the virtual table that contains function pointers specific
     * to the type of node. It allows polymorphic behavior and provides type-specific
     * implementations for operations on different node types in the tree.
     * 
     * @note The virtual table is typically used to safely cast `node_t` to a more 
     *  specific node type.
     */
    node_vtbl_t *vtbl;

    /**
     * @brief A pointer to the node that contains this node as a child.
     * 
     * This field is populated during analysis and is used to navigate the syntax tree
     * toward the root.
     */
    node_t *parent;

    /**
     * @brief A pointer to the source range occupied by this node.
     *
     * This field stores the beginning and ending positions of the node in the
     * source code as a single structure.
     */
    position_range_t *position;

    /**
     * @brief The lexical scope this node belongs to.
     *
     * Set by the semantic phase that wires nodes to scopes. May be `NULL`
     * for nodes that conceptually live outside any scope during early phases,
     * but should be assigned before codegen/analysis that depends on scope.
     */
    scope_t *scope;

    /**
     * @brief Per-scope execution/topology identifier.
     *
     * A unique, positive integer assigned by the first pass of the static
     * analyzer *within a single lexical scope*. The assignment guarantees that
     * a node with a larger id is never executed earlier than a node with a
     * smaller id in that scope.
     *
     * Initialization & invariants:
     * - `0` means "unassigned/invalid" (freshly constructed nodes MUST start at 0).
     * - The first valid id is `1`.
     * - Id numbering restarts for each new scope.
     */
    unsigned int id;
};

/**
 * @brief Gets the primary string data associated with a node.
 *
 * This helper dispatches to the node's virtual table and returns the main
 * data payload of the node in string form. For example, for identifier nodes
 * this may be the identifier name, and for literal nodes it may be the
 * literal text/value.
 *
 * @param node A pointer to the node.
 * @return A `string_value_t` containing the node's data or empty value if none.
 */
static inline string_value_t get_node_data(const node_t *node) {
    return node->vtbl->get_data(node);
}

/**
 * @brief Gets the number of properties exposed by a node.
 *
 * This helper dispatches to the node's virtual table and returns how many
 * key-value properties are available for debugging or visualization.
 *
 * @param node A pointer to the node.
 * @return Number of properties exposed by the node.
 */
static inline size_t get_node_property_count(const node_t *node) {
    return node->vtbl->get_property_count(node);
}

/**
 * @brief Retrieves a property of a node by index.
 *
 * This helper dispatches to the node's virtual table and retrieves the
 * indexed property exposed by the node.
 *
 * @param node A pointer to the node.
 * @param index Zero-based property index.
 * @param out_key Output pointer to receive the property key.
 * @param out_value Output pointer to receive the property value.
 */
static inline void get_node_property(const node_t *node, size_t index,
        string_view_t *out_key, string_value_t *out_value) {
    node->vtbl->get_property(node, index, out_key, out_value);
}

/**
 * @brief Gets the number of direct child nodes.
 *
 * This helper dispatches to the node's virtual table and returns the number
 * of direct children for the node.
 *
 * @param node A pointer to the node.
 * @return Number of child nodes.
 */
static inline size_t get_node_child_count(const node_t *node) {
    return node->vtbl->get_child_count(node);
}

/**
 * @brief Gets a child node by index.
 *
 * This helper dispatches to the node's virtual table and retrieves the
 * indexed child node.
 *
 * @param node A pointer to the parent node.
 * @param index Zero-based child index.
 * @return Pointer to the child node or NULL if index is out of range.
 */
static inline node_t* get_node_child(const node_t *node, size_t index) {
    return node->vtbl->get_child(node, index);
}

/**
 * @brief Gets the tag/label for a child node.
 *
 * This helper dispatches to the node's virtual table and returns the
 * descriptive tag associated with the indexed child.
 *
 * @param node A pointer to the parent node.
 * @param index Zero-based child index.
 * @return Wide character string with the child tag or NULL if not applicable.
 */
static inline const wchar_t* get_node_child_tag(const node_t *node, size_t index) {
    return node->vtbl->get_child_tag(node, index);
}

/**
 * @brief Inserts a child node before another child node.
 *
 * This helper dispatches to the node's virtual table and attempts to insert
 * `new_child` before `before_child`.
 *
 * @param node A pointer to the parent node to modify.
 * @param new_child A pointer to the child node to insert.
 * @param before_child A pointer to the existing child before which insertion
 *        should happen.
 * @return `true` if insertion succeeded, otherwise `false`.
 */
static inline bool insert_child_node_before(node_t *node, node_t *new_child,
        node_t *before_child) {
    return node->vtbl->insert_child_before(node, new_child, before_child);
}

/**
 * @brief Replaces one child node with another child node.
 *
 * This helper dispatches to the node's virtual table and attempts to replace
 * `old_child` with `new_child`.
 *
 * @param node A pointer to the parent node to modify.
 * @param old_child A pointer to the existing child node to replace.
 * @param new_child A pointer to the replacement child node.
 * @return `true` if replacement succeeded, otherwise `false`.
 */
static inline bool replace_child_node(node_t *node, node_t *old_child,
        node_t *new_child) {
    return node->vtbl->replace_child(node, old_child, new_child);
}

/**
 * @brief Gets the number of related nodes.
 *
 * This helper dispatches to the node's virtual table and returns the number
 * of non-child nodes related to the given node.
 *
 * @param node A pointer to the node.
 * @return Number of related nodes.
 */
static inline size_t get_node_related_count(const node_t *node) {
    return node->vtbl->get_related_count(node);
}

/**
 * @brief Gets a related node by index.
 *
 * This helper dispatches to the node's virtual table and retrieves the indexed
 * related node.
 *
 * @param node A pointer to the node.
 * @param index Zero-based related-node index.
 * @return Pointer to the related node or NULL if index is out of range.
 */
static inline const node_t* get_node_related(const node_t *node, size_t index) {
    return node->vtbl->get_related(node, index);
}

/**
 * @brief Gets the relation type for a related node.
 *
 * This helper dispatches to the node's virtual table and retrieves the relation
 * type for the indexed related node.
 *
 * @param node A pointer to the node.
 * @param index Zero-based related-node index.
 * @return Type of relation, or RELATION_NONE if index is out of range.
 */
static inline relation_type_t get_node_relation_type(const node_t *node, size_t index) {
    return node->vtbl->get_relation_type(node, index);
}

/**
 * @brief Calculates the abstract lattice element represented by a node.
 *
 * This helper dispatches to the node's virtual table.
 *
 * @param node A pointer to the node.
 * @param arena Memory arena for allocating lattice elements.
 * @return Constant pointer to the calculated lattice element.
 */
static inline const lattice_element_t *calculate_node(const node_t *node, arena_t *arena) {
    return node->vtbl->calculate(node, arena);
}

/**
 * @brief Executes abstract interpretation for a node.
 *
 * This helper dispatches to the node's virtual table.
 *
 * @param node A pointer to the node.
 * @param state Input abstract state.
 * @param arena Memory arena for allocating lattice elements.
 * @return Output abstract state after interpreting this node.
 */
static inline abstract_state_t *execute_node(node_t *node, abstract_state_t *state,
        arena_t *arena) {
    return node->vtbl->execute(node, state, arena);
}

/**
 * @brief Generates a single-line Goat source code representation from a node.
 *
 * This helper dispatches to the node's virtual table and returns compact Goat
 * source code corresponding to the node.
 *
 * @param node A pointer to the node.
 * @return A `string_value_t` containing the generated Goat code.
 */
static inline string_value_t generate_goat_code_from_node(const node_t *node) {
    return node->vtbl->generate_goat_code(node);
}

/**
 * @brief Generates indented Goat source code from a node.
 *
 * This helper dispatches to the node's virtual table and appends formatted
 * Goat source code for the node to the provided builder.
 *
 * @param node A pointer to the node.
 * @param builder A pointer to the source builder.
 * @param indent The number of tabs used for indentation.
 */
static inline void generate_indented_goat_code_from_node(const node_t *node,
        source_builder_t *builder, size_t indent) {
    node->vtbl->generate_indented_goat_code(node, builder, indent);
}

/**
 * @brief Checks whether C code can be generated from a node.
 *
 * This helper dispatches to the node's virtual table and reports whether
 * the node has a valid representation in C.
 *
 * @param node A pointer to the node.
 * @return `true` if C code generation is supported, `false` otherwise.
 */
static inline bool can_generate_c_code_from_node(const node_t *node) {
    return node->vtbl->can_generate_c_code(node);
}

/**
 * @brief Generates a single-line C source code representation from a node.
 *
 * This helper dispatches to the node's virtual table and attempts to produce
 * compact C source code corresponding to the node.
 *
 * @param node A pointer to the node.
 * @return A `string_value_t` containing the generated C code or NULL string if
 *  conversion is not possible.
 */
static inline string_value_t generate_c_code_from_node(const node_t *node) {
    return node->vtbl->generate_c_code(node);
}

/**
 * @brief Generates indented C source code from a node.
 *
 * This helper dispatches to the node's virtual table and appends formatted
 * C source code for the node to the provided builder.
 *
 * @param node A pointer to the node.
 * @param builder A pointer to the source builder.
 * @param indent The number of tabs used for indentation.
 */
static inline void generate_indented_c_code_from_node(const node_t *node,
        source_builder_t *builder, size_t indent) {
    node->vtbl->generate_indented_c_code(node, builder, indent);
}

/**
 * @brief Generates bytecode from a node.
 *
 * This helper dispatches to the node's virtual table and generates bytecode
 * instructions for the given node.
 *
 * @param node A pointer to the node.
 * @param code A pointer to the code builder used for instruction emission.
 * @param data A pointer to the data builder used for static data management.
 * @return The instruction index of the first emitted instruction.
 */
static inline instr_index_t generate_bytecode_from_node(node_t *node,
        code_builder_t *code, data_builder_t *data) {
    return node->vtbl->generate_bytecode(node, code, data);
}

/**
 * @brief Generates bytecode for storing a value into a node.
 *
 * This helper dispatches to the node's virtual table and generates bytecode
 * for assignment into an assignable expression.
 *
 * @param node A pointer to the target node.
 * @param code A pointer to the code builder.
 * @param data A pointer to the data builder.
 * @return The instruction index of the first emitted instruction.
 */
static inline instr_index_t generate_bytecode_assign_from_node(const node_t *node,
        code_builder_t *code, data_builder_t *data) {
    return node->vtbl->generate_bytecode_assign(node, code, data);
}

/**
 * @brief Generates deferred bytecode from a node.
 *
 * This helper dispatches to the node's virtual table and generates deferred
 * bytecode for nodes such as function bodies or other delayed code blocks.
 *
 * @param node A pointer to the node.
 * @param code A pointer to the code builder.
 * @param data A pointer to the data builder.
 * @return `true` if deferred bytecode was successfully generated in this pass;
 *  `false` otherwise.
 */
static inline bool generate_deferred_bytecode_from_node(const node_t *node,
        code_builder_t *code, data_builder_t *data) {
    return node->vtbl->generate_bytecode_deferred(node, code, data);
}

/**
 * @brief Creates a new root node.
 * 
 * This function allocates memory for a new root node, copies the provided statement list into
 * the node, and returns a pointer to the newly created node.
 * 
 * @param arena A pointer to the memory arena for memory allocation.
 * @param statements A list of statements to be included in the root node.
 * @return A pointer to the newly created root node.
 */
node_t *create_root_node(arena_t *arena, list_t *statements);
