/** @file test_access_cells.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Declaration identity, field storage and capture lifetime regressions.
 */
#pragma once
#include <stdbool.h>
bool test_access_identities(void);
bool test_access_cells(void);
bool test_access_captures(void);
bool test_access_cycles(void);
bool test_access_contexts(void);
bool test_access_clone_metadata(void);
bool test_access_clone_methods(void);
bool test_access_flat_cells(void);
