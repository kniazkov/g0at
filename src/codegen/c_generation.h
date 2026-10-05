/** @file c_generation.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Whole-function C emission and specialization-scoped lowering state.
 */
#pragma once

#include "analysis/c_expression.h"
#include "source_builder.h"

/** @brief Backend outcomes, independent of analyzer eligibility. */
typedef enum {
    C_GENERATION_OK,
    C_GENERATION_INVALID_REQUEST,
    C_GENERATION_NOT_PROVEN,
    C_GENERATION_UNSUPPORTED
} c_generation_status_t;

/** @brief Borrowed C binding; declaration identity distinguishes shadowed names. */
typedef struct c_generation_binding_t {
    const struct c_generation_binding_t *next;
    const node_t *declaration;
    string_view_t name;
    c_value_type_t type;
} c_generation_binding_t;

/** @brief Borrowed name of an exact callee specialization in the generated module. */
typedef struct c_generation_callee_t {
    const struct c_generation_callee_t *next;
    const function_summary_t *summary;
    string_view_t name;
} c_generation_callee_t;

enum {
    C_HELPER_I64_ADD = 1,
    C_HELPER_I64_SUB = 2,
    C_HELPER_I64_MUL = 4,
    C_HELPER_I64_NEG = 8,
    C_HELPER_COMPARISON = 16,
    C_HELPER_INTEGER = C_HELPER_I64_ADD | C_HELPER_I64_SUB | C_HELPER_I64_MUL | C_HELPER_I64_NEG
};

/** @brief One generation attempt; all input records are borrowed and immutable. */
typedef struct c_generation_context_t {
    const function_summary_t *summary;
    const c_expression_proof_t *replacement_proof; /**< Scoped literal proof transfer. */
    string_view_t function_name;
    const c_generation_binding_t *bindings;
    const c_generation_callee_t *callees;
    unsigned helper_flags;  /**< Helpers referenced by successfully lowered operations. */
    bool module_definition; /**< Headers and prototypes are supplied by the module. */
    bool terminates;        /**< Whether the last emitted statement returns on every path. */
    size_t local_count;     /**< Unique local names across nested scopes. */
    size_t temporary_count; /**< Unique within one emitted function. */
    c_generation_status_t status;
    const node_t *failed_node; /**< First failure, retained while unwinding. */
} c_generation_context_t;

/** @brief Internal expression result; release value and optional prelude with destroy_c_expression.
 */
typedef struct c_generated_expression_t {
    bool success;
    c_value_type_t type;
    abstract_truth_t literal_truth; /**< Optional literal truth; zero means not supplied. */
    string_value_t value;
    source_builder_t *prelude; /**< Ordered statements, relative indentation; NULL if empty. */
} c_generated_expression_t;

/** @brief Complete function source, or an explicit failure with no partial source. */
typedef struct c_generation_result_t {
    c_generation_status_t status;
    const node_t *failed_node;
    string_value_t source; /**< Release with FREE_STRING(). */
    unsigned helper_flags;
} c_generation_result_t;

/** @brief External entry point; inputs are borrowed. Names must be ASCII identifiers starting with
 * g_ (or the legacy goat_ prefix). */
c_generation_result_t generate_c_function(const function_summary_t *summary,
                                          string_view_t name,
                                          const c_generation_binding_t *bindings,
                                          const c_generation_callee_t *callees);

/** @brief Internal lowering helpers; use generate_c_function to obtain executable source. */
c_value_type_t c_generation_parameter_type(const c_generation_context_t *context, size_t index);
c_value_type_t c_generation_return_type(const c_generation_context_t *context);
/** @brief Uses original proofs, with a scoped transfer for an equivalent replacement literal. */
c_value_type_t c_generation_expression_type(const c_generation_context_t *context,
                                            const node_t *node);

/** @brief Records the first backend failure without modifying analysis facts. */
bool fail_c_generation(c_generation_context_t *context,
                       const node_t *node,
                       c_generation_status_t status);

/** @brief Releases expression output, including partially generated output after failure. */
void destroy_c_expression(c_generated_expression_t *expression);

/** @brief Checks backend-owned ASCII identifiers before emitting definitions or references. */
bool c_function_name_is_valid(string_view_t name);

/** @brief Selects a proven replacement, otherwise retains the original subtree. */
const node_t *c_generation_replacement(const c_generation_context_t *context, const node_t *node);

/** @brief Signature-wide, discardable condition truth; shared AST flags are ignored. */
abstract_truth_t c_generation_condition_truth(const c_generation_context_t *context,
                                              const node_t *condition);
