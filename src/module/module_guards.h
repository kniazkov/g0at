/** @file module_guards.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Side-effect-free scalar specialization predicates for module ABI v1.
 */
#pragma once

#include "yyjson.h"

#include <goat/module_abi.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Return the ABI scalar tag, or UINT32_MAX for an unknown type. */
uint32_t goat_module_scalar_tag(const yyjson_val *type);
/** Validate strict-schema specialization records, symbol identities and disjoint guards.
 * Does not resolve function IDs/arity against bytecode or load native code.
 */
bool goat_module_guards_validate(const yyjson_val *specializations);
/** Compare two strict-schema guards by finding a witness in every scalar domain. */
bool goat_module_guards_overlap(const yyjson_val *first, const yyjson_val *second);
/** Test a validated specialization using borrowed, validated ABI values.
 * Missing actual arguments are null; extra arguments do not add formal slots.
 * Never invokes user code, a property accessor or a comparison overload.
 */
bool goat_module_guard_matches(const yyjson_val *specialization,
                               size_t argument_count,
                               const goat_module_value_v1 *arguments);
