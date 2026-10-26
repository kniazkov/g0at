#pragma once

#include "module_json.h"

#include <stdio.h>

/** Information policy only: no archive IO, hash/variant checks or publication.
 * Callers deduplicate exact source argument strings before preparing all inputs together. */
typedef struct goat_information_merge goat_information_merge;

typedef enum goat_information_merge_error {
    GOAT_INFORMATION_MERGE_OK,
    GOAT_INFORMATION_MERGE_INVALID_INPUT,
    GOAT_INFORMATION_MERGE_CONFLICT,
    GOAT_INFORMATION_MERGE_MEMORY,
    GOAT_INFORMATION_MERGE_ORDER_REQUIRED,
    GOAT_INFORMATION_MERGE_ORDER_EMPTY,
    GOAT_INFORMATION_MERGE_ORDER_MISSING,
    GOAT_INFORMATION_MERGE_ORDER_DUPLICATE,
    GOAT_INFORMATION_MERGE_ORDER_NUMBER,
    GOAT_INFORMATION_MERGE_ORDER_RANGE,
    GOAT_INFORMATION_MERGE_INPUT_ENDED,
    GOAT_INFORMATION_MERGE_IO
} goat_information_merge_error;

typedef struct goat_information_merge_diagnostic {
    size_t source_index; /**< Zero based failing source; SIZE_MAX if not applicable. */
    goat_module_json_error json_error;
    const char *field; /**< Static conflicting field name, or NULL. */
} goat_information_merge_diagnostic;

/** Validate complete module.json descriptors, normalize and union information.
 * Inputs are borrowed; the plan owns all its strings. Failure clears *out.
 * All descriptors are validated before conflict resolution. */
goat_information_merge_error
goat_information_merge_prepare(const yyjson_doc *const *inputs,
                               size_t count,
                               goat_information_merge **out,
                               goat_information_merge_diagnostic *diagnostic);
void goat_information_merge_free(goat_information_merge *plan);
/** Borrowed candidate array (NULL if empty), stable for the lifetime of plan. */
const yyjson_val *goat_information_merge_candidates(const goat_information_merge *plan);
bool goat_information_merge_needs_order(const goat_information_merge *plan);
/** Produce independent information-only document; free with yyjson_doc_free.
 * Required order is a complete zero-based permutation. No order is accepted for
 * automatic zero/one-list cases. The plan is unchanged on success or failure. */
goat_information_merge_error goat_information_merge_finish(const goat_information_merge *plan,
                                                           const size_t *order,
                                                           size_t count,
                                                           yyjson_doc **out);
/** Parse one line of space-separated 1-based decimal numbers into a zero-based
 * permutation. Only ASCII spaces/tabs and optional terminal CR/LF are accepted.
 * Output contents are unspecified on failure. */
goat_information_merge_error goat_information_merge_parse_order(const char *line,
                                                                size_t length,
                                                                size_t candidate_count,
                                                                size_t *order);
/** Show candidates once and retry invalid lines without changing numbering.
 * Never read input when !interactive or order is not needed. EOF fails without
 * producing a result. Strings are printed as escaped JSON, preserving NUL/UTF-8.
 * Caller establishes terminal eligibility; streams are borrowed, not closed. */
goat_information_merge_error goat_information_merge_interactive(const goat_information_merge *plan,
                                                                bool interactive,
                                                                FILE *input,
                                                                FILE *messages,
                                                                yyjson_doc **out);
const char *goat_information_merge_error_string(goat_information_merge_error error);
