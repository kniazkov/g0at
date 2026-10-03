/** @file c_contract.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Preconditions for the first numeric C subset; no body lowering yet.
 */
#pragma once

#include "function_summary.h"

/** @brief Unboxed representation; BOOL is an expression temporary, not an interface type. */
typedef enum {
    C_VALUE_UNKNOWN,
    C_VALUE_UNSUPPORTED,
    C_VALUE_INT64,
    C_VALUE_DOUBLE,
    C_VALUE_BOOL
} c_value_type_t;

/** @brief Failed or pending checks; BODY remains until an entire body is proven. */
typedef enum {
    C_BLOCKER_ANALYSIS = 1,
    C_BLOCKER_PARAMETERS = 2,
    C_BLOCKER_RETURN = 4,
    C_BLOCKER_EFFECTS = 8,
    C_BLOCKER_CAPTURES = 16,
    C_BLOCKER_BODY = 32
} c_blocker_t;

/** @brief Classifies the representation of an interface value, not an expression's semantics. */
c_value_type_t classify_c_value_type(lattice_type_t type);

/** @brief Checks one signature's preconditions; never claims body support. */
void check_function_c_contract(function_summary_t *summary);

/** @brief Checks all registered signatures without altering AST flags or value facts. */
void analyze_function_c_contracts(node_t *root);

/** @brief Formats cached failed/pending checks; release with FREE_STRING(). */
string_value_t c_blockers_to_string(uint32_t blockers);
