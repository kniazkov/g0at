/** @file module_native_merge.h
 * @brief Nonexecuting, deterministic union of validated native variants.
 */
#pragma once
#include "module_container.h"

typedef enum {
    GOAT_NATIVE_MERGE_OK,
    GOAT_NATIVE_MERGE_ARGUMENT,
    GOAT_NATIVE_MERGE_MEMORY,
    GOAT_NATIVE_MERGE_CONFLICT,
    GOAT_NATIVE_MERGE_COMPILER,
    GOAT_NATIVE_MERGE_AMBIGUOUS
} goat_native_merge_status;

typedef struct {
    yyjson_doc *metadata;            /**< Owned root array of merged variant records. */
    goat_container_native *payloads; /**< Owned mappings; bytes borrow input containers. */
    size_t count;
} goat_native_merge_result;

/** Inputs must have passed full module validation. Compares normalized requirements,
 * hashes, specialization declarations and optional compiler information, then rejects
 * simultaneously compatible variants using the production selector's requirements.
 * Equality uses normalized declared target fields, not invented CPU prerequisite
 * expansion: different overlapping declarations remain an ambiguity. Unknown
 * CPU requirements and unsupported ABIs cannot establish a compatibility witness.
 * No I/O, loading, host detection or execution. Result names are unique and stable;
 * inputs must remain alive until publication. Failure clears an initially empty result.
 */
goat_native_merge_status goat_native_merge(const goat_module_container *const *inputs,
                                           size_t count,
                                           goat_native_merge_result *out);
void goat_native_merge_clear(goat_native_merge_result *result);
const char *goat_native_merge_status_string(goat_native_merge_status status);
