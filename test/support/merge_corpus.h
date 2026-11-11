/** @file merge_corpus.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Lossless, test-only storage of frozen merge scenario snapshots.
 */
#pragma once

#include "yyjson.h"

#include <stdbool.h>
#include <stddef.h>

/** Expand the v1 entry/snapshot tables into the original ordered {cases: [...]}.
 * References have exactly two levels (case -> snapshot -> literal entry), never
 * recursive templates. Every reference and definition, including unused ones,
 * is validated. The returned mutable document owns independent copies of every
 * value/string; neither different snapshots nor repeated entries share nodes.
 * Free with yyjson_mut_doc_free. NULL reports invalid input or allocation failure.
 */
yyjson_mut_doc *merge_corpus_expand(const char *data, size_t size, char *error, size_t error_size);

/** Reproducible first-occurrence interning, for developer conversion/auditing.
 * Consumes only frozen JSON, never a production merge/format writer or oracle.
 * The input and output documents own independent storage.
 */
yyjson_mut_doc *merge_corpus_pack(const char *data, size_t size, char *error, size_t error_size);

/** JSON equality that also preserves object key order and numeric types. */
bool merge_corpus_equal(yyjson_mut_val *a, yyjson_mut_val *b);

/** Developer modes and focused controls hosted by module_merge_scenarios. */
int merge_corpus_tool(int argc, char **argv);
