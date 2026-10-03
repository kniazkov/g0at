/** @file test_c_module.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Module identities and generic dependency closure tests.
 */
#pragma once
#include <stdbool.h>

bool test_c_module_identity(void);
bool test_c_module_dependencies(void);
bool test_c_module_call_lifetime(void);
