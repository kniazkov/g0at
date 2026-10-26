/** @file c_module_v1.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Module-v1 exports over the shared proven native lowering.
 */
#pragma once
#include "c_module_output.h"
#include "module/module_json.h"

typedef struct {
    string_value_t source;
    yyjson_doc *specializations;
    size_t generated_count, omitted_count;
} c_module_v1_output_t;

/** Roots must have their final linked bytecode function IDs. The result owns all
 * storage and survives analysis destruction. No compiler, loader or filesystem I/O.
 */
c_module_v1_output_t generate_c_module_v1(arena_t *arena, const node_t *const *roots, size_t count);
void destroy_c_module_v1_output(c_module_v1_output_t *output);
/** Append a validated canonical descriptor to a borrowed generated translation unit.
 * Metadata supplies name/version/mainHash; target and specialization array are borrowed.
 * No library hash is embedded. Empty result denotes invalid descriptor input.
 */
string_value_t append_c_module_v1_descriptor(string_view_t source,
                                             const yyjson_doc *metadata,
                                             const yyjson_val *target,
                                             const yyjson_doc *specializations);
