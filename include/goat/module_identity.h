/**
 * @file module_identity.h
 * @brief Portable module identifiers and import specification parsing.
 */
#pragma once

#include "unicode.h"

#include <stdbool.h>
#include <stddef.h>

/** Borrowed byte slice; no terminating zero is required. */
typedef struct {
    const char *data;
    size_t length;
} goat_module_slice_t;

/** Borrowed wide-character slice; no terminating zero is required. */
typedef struct {
    const gchar_t *data;
    size_t length;
} goat_module_wide_slice_t;

/** Parsed import. The slices refer to the caller's original buffer. */
typedef struct {
    goat_module_slice_t name;
    goat_module_slice_t version;
    bool has_version;
} goat_module_import_t;

/** Parsed wide-character import, borrowing the original input. */
typedef struct {
    goat_module_wide_slice_t name;
    goat_module_wide_slice_t version;
    bool has_version;
} goat_module_import_wide_t;

/** Check a module name (1..255 ASCII bytes). */
bool goat_module_name_valid(const char *text, size_t length);

/** Check a module version (1..250 ASCII bytes). */
bool goat_module_version_valid(const char *text, size_t length);

/** Parse "name" or "name@version" without allocation; clear result on failure. */
bool goat_module_parse_import(const char *text, size_t length, goat_module_import_t *result);

/** Parse a wide-character import without allocation or conversion. */
bool goat_module_parse_import_wide(const gchar_t *text,
                                   size_t length,
                                   goat_module_import_wide_t *result);
