/**
 * @file module_identity.h
 * @brief Portable module identifiers and import specification parsing.
 */
#pragma once

#include "unicode.h"

#include <stdbool.h>
#include <stddef.h>

/** Maximum module name length, in ASCII bytes or gchar_t elements. */
#define GOAT_MODULE_NAME_MAX_LENGTH 255
/** Maximum exact version length, in ASCII bytes or gchar_t elements. */
#define GOAT_MODULE_VERSION_MAX_LENGTH 250

/** Borrowed byte slice; no terminating zero is required. */
typedef struct {
    const char *data;
    size_t length;
} goat_module_slice_t;

/** Borrowed portable UTF-32 slice; no terminating zero is required. */
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

/** Parsed portable UTF-32 import, borrowing the original input. */
typedef struct {
    goat_module_wide_slice_t name;
    goat_module_wide_slice_t version;
    bool has_version;
} goat_module_import_wide_t;

/** Check a module name (1..255 ASCII bytes).
 * Grammar: [a-z0-9][a-z0-9._-]*, with no trailing dot or DOS device stem.
 * No trimming, case folding, Unicode normalization or allocation is performed.
 * NULL, empty input and embedded NUL are invalid. No terminator is required.
 */
bool goat_module_name_valid(const char *text, size_t length);

/** Check a module version (1..250 ASCII bytes). */
bool goat_module_version_valid(const char *text, size_t length);

/** Check a module name stored in portable UTF-32, with the same ASCII rules. */
bool goat_module_name_valid_wide(const gchar_t *text, size_t length);

/** Check an exact version stored in portable UTF-32, with the same ASCII rules. */
bool goat_module_version_valid_wide(const gchar_t *text, size_t length);

/** Parse "name" or "name@version" without allocation; clear result on failure.
 * Slices borrow the unchanged input. A missing version has a NULL, empty slice.
 * Result may be NULL (returns false); input and result storage must not overlap.
 */
bool goat_module_parse_import(const char *text, size_t length, goat_module_import_t *result);

/** Parse a wide-character import without allocation or conversion. */
bool goat_module_parse_import_wide(const gchar_t *text,
                                   size_t length,
                                   goat_module_import_wide_t *result);
