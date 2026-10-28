/** @file format_fixtures.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent semantic fixture constructors (not production writers).
 */
#pragma once
#include "format_json.h"

/** Append real declarations without moving original instruction/function indices.
 * A single-file image with main=0 is required when externals are present.
 * The returned image is malloc-owned, including when there are no externals.
 * Only on success, metadata.files[0].entry is replaced with the appended entry.
 * Input bytes are never modified. NULL reports malformed input/allocation failure.
 */
unsigned char *format_fixture_external_declarations(yyjson_mut_doc *doc,
                                                    yyjson_mut_val *metadata,
                                                    const unsigned char *image,
                                                    size_t image_size,
                                                    size_t *result_size,
                                                    char *error,
                                                    size_t error_size);
