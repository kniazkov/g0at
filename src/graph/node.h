/**
 * @file node.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Definitions for the basic node structure of the abstract syntax tree (AST).
 */

#pragma once

#include "common/position.h"
#include "common/types.h"
#include "lib/value.h"
#include "node_type.h"
#include "relation_type.h"
#include "scope.h"

#include <stdint.h>

/** @brief Proven properties; an absent bit means unknown or unsupported. */
typedef enum {
    NODE_FLAG_UNREACHABLE = UINT32_C(1) << 0,
    NODE_FLAG_PURE = UINT32_C(1) << 1,        /**< No state writes or I/O; may still throw. */
    NODE_FLAG_C_COMPATIBLE = UINT32_C(1) << 2 /**< C eligibility across registered signatures. */
} node_flag_t;

typedef struct node_t node_t;
typedef struct statement_t statement_t;
typedef struct expression_t expression_t;
typedef struct source_builder_t source_builder_t;
typedef struct code_builder_t code_builder_t;
typedef struct data_builder_t data_builder_t;
typedef struct arena_t arena_t;
typedef struct list_t list_t;
typedef struct lattice_element_t lattice_element_t;
typedef struct abstract_state_t abstract_state_t;
typedef struct analysis_collector_t analysis_collector_t;
typedef struct function_summary_t function_summary_t;
typedef struct c_expression_context_t c_expression_context_t;

/**
 * @brief Classification hint for node data rendered in graph visualization.
 *
 * It does not affect parsing, semantic analysis, bytecode generation, or source-code regeneration.
 */
typedef enum {
    /** @brief No special display classification. */
    NODE_DISPLAY_VALUE_PLAIN = 0,

    /** @brief Predefined constant, built-in object, or language keyword. */
    NODE_DISPLAY_VALUE_PREDEFINED,

    /** @brief String literal value. */
    NODE_DISPLAY_VALUE_STRING_LITERAL
} node_display_value_kind_t;

/**
 * @brief String value with an additional graph-display classification.
 *
 * The string ownership rules are the same as for the embedded @ref string_value_t.
 */
typedef struct {
    /** @brief Textual value exposed by the node. */
    string_value_t text;

    /** @brief Display classification for the value. */
    node_display_value_kind_t kind;
} node_display_value_t;

/** @brief The virtual table structure for nodes in the syntax tree. */
typedef struct {
    /** @brief The type of the node. */
    node_type_t type;

    /** @brief Human-readable type name of the node. */
    const wchar_t *type_name;

    /** @brief Marks an lvalue; generate_bytecode_assign must be implemented. */
    bool is_assignable_expression;

    /** @brief Gets the display representation of the node's primary data. */
    node_display_value_t (*get_data)(const node_t *node);

    /** @brief Gets the number of properties exposed by this node. */
    size_t (*get_property_count)(const node_t *node);

    /**
     * @brief Retrieves a property of this node by index.
     * `index`: Zero-based index in range [0, get_property_count(node)).
     * @return Property key as a constant wide string, or NULL if the property is not available.
     */
    const wchar_t *(*get_property)(const node_t *node,
                                   size_t index,
                                   node_display_value_t *out_value);

    /** @brief Returns the count of direct child nodes for this syntax tree node. */
    size_t (*get_child_count)(const node_t *node);

    /**
     * @brief Gets a child node by index.
     * `index`: Zero-based index of the child node.
     * @return Pointer to the child node or NULL if index is out of range.
     */
    node_t *(*get_child)(const node_t *node, size_t index);

    /**
     * @brief Gets the tag/label for a child node.
     * `index`: Zero-based index of the child node.
     * @return Wide character string (const wchar_t*) with the child's tag or NULL if not
     * applicable.
     */
    const wchar_t *(*get_child_tag)(const node_t *node, size_t index);

    /**
     * @brief Inserts new_child before before_child.
     * Returns false without changing the node if unsupported; preserves structural validity.
     */
    bool (*insert_child_before)(node_t *node, node_t *new_child, node_t *before_child);

    /**
     * @brief Replaces one child node with another child node.
     *
     * If the function returns `false`, the node must remain unchanged. After the call, regardless
     * of the result, the node must still be structurally valid.
     * @return `true` if the child was replaced, otherwise `false`.
     */
    bool (*replace_child)(node_t *node, node_t *old_child, node_t *new_child);

    /** @brief Gets the number of related nodes. */
    size_t (*get_related_count)(const node_t *node);

    /**
     * @brief Gets a related node by index.
     * `index`: Zero-based related-node index.
     * @return Pointer to the related node or NULL if index is out of range.
     */
    const node_t *(*get_related)(const node_t *node, size_t index);

    /**
     * @brief Gets the relation type for a related node.
     * `index`: Zero-based related-node index.
     * @return Type of relation, or RELATION_NONE if index is out of range.
     */
    relation_type_t (*get_relation_type)(const node_t *node, size_t index);

    /**
     * @brief Calculates the abstract lattice element represented by this node.
     *
     * The method must always return a lattice element, but it does not have to modify the state.
     */
    const lattice_element_t *(*calculate)(node_t *node, abstract_state_t *state, arena_t *arena);

    /**
     * @brief Executes abstract interpretation for this node.
     * @return Output abstract state after interpreting this node.
     */
    abstract_state_t *(*execute)(node_t *node, abstract_state_t *state, arena_t *arena);

    /** @brief Immediate-execution proof; may replace *state, never specializes deferred bodies. */
    const lattice_element_t *(*analyze_reachability)(node_t *node,
                                                     abstract_state_t **state,
                                                     analysis_collector_t *collector);

    /** @brief Proves no writes/I/O using child caches; a function object describes its body. */
    bool (*is_pure)(const node_t *node);

    /** @brief Collects direct body effects, including the appropriate children. */
    void (*collect_direct_effects)(const node_t *node, function_summary_t *summary, arena_t *arena);

    /** @brief Returns a simpler equivalent node, or the original node. */
    node_t *(*simplify)(node_t *node, arena_t *arena);

    /** @brief Generates a single-line Goat source code representation of the node. */
    string_value_t (*generate_goat_code)(const node_t *node);

    /** @brief Generates indented Goat source code for the node, if applicable. */
    void (*generate_indented_goat_code)(const node_t *node,
                                        source_builder_t *builder,
                                        size_t indent);

    /**
     * @brief Proves membership in the current C subset, not availability of a C emitter.
     * value is an optional pointwise fact; NULL must not infer a variable type from summaries.
     * context supplies per-signature operand proofs; NULL uses only legacy node caches.
     */
    bool (*can_generate_c_code)(const node_t *node,
                                const lattice_element_t *value,
                                const c_expression_context_t *context);

    /**
     * @brief Generates a single-line C source code representation of the node, if possible.
     *
     * If the node cannot be represented in C, it returns NULL string.
     * @return A `string_value_t` containing the generated C code or NULL string if conversion is
     * not possible.
     */
    string_value_t (*generate_c_code)(const node_t *node);

    /** @brief Generates indented C source code for the node, if applicable. */
    void (*generate_indented_c_code)(const node_t *node, source_builder_t *builder, size_t indent);

    /** @brief Generates bytecode for the given node. */
    instr_index_t (*generate_bytecode)(node_t *node, code_builder_t *code, data_builder_t *data);

    /** @brief Generates bytecode for storing a value into this expression. */
    instr_index_t (*generate_bytecode_assign)(const node_t *node,
                                              code_builder_t *code,
                                              data_builder_t *data);

    /**
     * @brief Emits deferred code such as a function body.
     * @return false if dependencies are unresolved and another pass is needed.
     */
    bool (*generate_bytecode_deferred)(const node_t *node,
                                       code_builder_t *code,
                                       data_builder_t *data);
} node_vtbl_t;

/**
 * @brief The base structure for nodes in the syntax tree.
 *
 * All nodes, regardless of their specific type, share this common structure which contains a
 * reference to their virtual table.
 */
struct node_t {
    /** @brief Pointer to the node's virtual table. */
    node_vtbl_t *vtbl;

    /** @brief Parent in the executable tree; archived replacement edges may share children. */
    node_t *parent;

    /** @brief A pointer to the source range occupied by this node. */
    position_range_t *position;

    /**
     * @brief The lexical scope this node belongs to.
     *
     * May be `NULL` for nodes that conceptually live outside any scope during early phases, but
     * should be assigned before codegen/analysis that depends on scope.
     */
    scope_t *scope;

    /**
     * @brief Traversal ID assigned by analysis; zero means unassigned.
     * Numbering restarts at function boundaries, but continues through ordinary blocks.
     */
    unsigned int id;

    /** @brief Analysis proofs; function-object purity describes its body, not allocation. */
    uint32_t flags;
};

/** @brief Checks a single analysis flag. */
static inline bool node_has_flag(const node_t *node, node_flag_t flag) {
    return (node->flags & flag) != 0;
}

/** @brief Gets the primary display data associated with a node. */
static inline node_display_value_t get_node_data(const node_t *node) {
    return node->vtbl->get_data(node);
}

/** @brief Gets the number of properties exposed by a node. */
static inline size_t get_node_property_count(const node_t *node) {
    return node->vtbl->get_property_count(node);
}

/**
 * @brief Retrieves a property of a node by index.
 * `index`: Zero-based property index.
 * @return Property key as a constant wide string, or NULL if unavailable.
 */
static inline const wchar_t *
get_node_property(const node_t *node, size_t index, node_display_value_t *out_value) {
    return node->vtbl->get_property(node, index, out_value);
}

/** @brief Gets the number of direct child nodes. */
static inline size_t get_node_child_count(const node_t *node) {
    return node->vtbl->get_child_count(node);
}

/**
 * @brief Gets a child node by index.
 * `index`: Zero-based child index.
 * @return Pointer to the child node or NULL if index is out of range.
 */
static inline node_t *get_node_child(const node_t *node, size_t index) {
    return node->vtbl->get_child(node, index);
}

/**
 * @brief Gets the tag/label for a child node.
 * `index`: Zero-based child index.
 * @return Wide character string with the child tag or NULL if not applicable.
 */
static inline const wchar_t *get_node_child_tag(const node_t *node, size_t index) {
    return node->vtbl->get_child_tag(node, index);
}

/**
 * @brief Inserts new_child before before_child.
 * Returns false without changing the node if unsupported; preserves structural validity.
 */
static inline bool insert_child_node_before(node_t *node, node_t *new_child, node_t *before_child) {
    return node->vtbl->insert_child_before(node, new_child, before_child);
}

/**
 * @brief Replaces one child node with another child node.
 * @return `true` if replacement succeeded, otherwise `false`.
 */
static inline bool replace_child_node(node_t *node, node_t *old_child, node_t *new_child) {
    return node->vtbl->replace_child(node, old_child, new_child);
}

/** @brief Gets the number of related nodes. */
static inline size_t get_node_related_count(const node_t *node) {
    return node->vtbl->get_related_count(node);
}

/**
 * @brief Gets a related node by index.
 * `index`: Zero-based related-node index.
 * @return Pointer to the related node or NULL if index is out of range.
 */
static inline const node_t *get_node_related(const node_t *node, size_t index) {
    return node->vtbl->get_related(node, index);
}

/**
 * @brief Gets the relation type for a related node.
 * `index`: Zero-based related-node index.
 * @return Type of relation, or RELATION_NONE if index is out of range.
 */
static inline relation_type_t get_node_relation_type(const node_t *node, size_t index) {
    return node->vtbl->get_relation_type(node, index);
}

/** @brief Calculates the abstract lattice element represented by a node. */
static inline const lattice_element_t *
calculate_node(node_t *node, abstract_state_t *state, arena_t *arena) {
    return node->vtbl->calculate(node, state, arena);
}

/**
 * @brief Executes abstract interpretation for a node.
 * @return Output abstract state after interpreting this node.
 */
static inline abstract_state_t *
execute_node(node_t *node, abstract_state_t *state, arena_t *arena) {
    return node->vtbl->execute(node, state, arena);
}

/** @brief Generates a single-line Goat source code representation from a node. */
static inline string_value_t generate_goat_code_from_node(const node_t *node) {
    return node->vtbl->generate_goat_code(node);
}

/** @brief Generates indented Goat source code from a node. */
static inline void generate_indented_goat_code_from_node(const node_t *node,
                                                         source_builder_t *builder,
                                                         size_t indent) {
    node->vtbl->generate_indented_goat_code(node, builder, indent);
}

/** @brief Checks membership in the current C subset using optional pointwise facts. */
static inline bool can_generate_c_code_from_node(const node_t *node,
                                                 const lattice_element_t *value) {
    return node->vtbl->can_generate_c_code(node, value, NULL);
}

/**
 * @brief Generates a single-line C source code representation from a node.
 * @return A `string_value_t` containing the generated C code or NULL string if conversion is not
 * possible.
 */
static inline string_value_t generate_c_code_from_node(const node_t *node) {
    return node->vtbl->generate_c_code(node);
}

/** @brief Generates indented C source code from a node. */
static inline void
generate_indented_c_code_from_node(const node_t *node, source_builder_t *builder, size_t indent) {
    node->vtbl->generate_indented_c_code(node, builder, indent);
}

/** @brief Generates bytecode from a node. */
static inline instr_index_t
generate_bytecode_from_node(node_t *node, code_builder_t *code, data_builder_t *data) {
    if (node_has_flag(node, NODE_FLAG_UNREACHABLE))
        return BAD_INSTR_INDEX;
    return node->vtbl->generate_bytecode(node, code, data);
}

/** @brief Generates bytecode for storing a value into a node. */
static inline instr_index_t
generate_bytecode_assign_from_node(const node_t *node, code_builder_t *code, data_builder_t *data) {
    return node->vtbl->generate_bytecode_assign(node, code, data);
}

/**
 * @brief Generates deferred bytecode from a node.
 *
 * This helper dispatches to the node's virtual table and generates deferred bytecode for nodes such
 * as function bodies or other delayed code blocks.
 * @return `true` if deferred bytecode was successfully generated in this pass; `false` otherwise.
 */
static inline bool generate_deferred_bytecode_from_node(const node_t *node,
                                                        code_builder_t *code,
                                                        data_builder_t *data) {
    return node->vtbl->generate_bytecode_deferred(node, code, data);
}

/** @brief Creates a new root node. */
node_t *create_root_node(arena_t *arena, list_t *statements);
