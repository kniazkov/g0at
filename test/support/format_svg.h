/** @file format_svg.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Test-only bounded XML reader for Graphviz SVG, without external entity loading.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char *text; /* Concatenated element character data, in document order. */
    bool green_stroke, green_fill, purple_pair;
} format_svg_t;

/* Accept UTF-8 XML 1.0 with ASCII unprefixed element/attribute names, the standard
 * SVG/xlink namespace declarations, comments, PIs, CDATA, and an external DOCTYPE.
 * DTD subsets and user entities are deliberately unsupported; no files/network are read.
 * Reject unsupported syntax rather than treating it as text. Maximum nesting is 256.
 * On success the caller owns text (free); on failure output is zeroed. */
bool format_svg_parse(const void *data,
                      size_t size,
                      format_svg_t *out,
                      char *error,
                      size_t error_size);
