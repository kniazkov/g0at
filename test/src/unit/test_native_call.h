/** @file test_native_call.h
 * @copyright 2026 Ivan Kniazkov
 * @brief CALL integration tests with loaded providers.
 */
#pragma once
#include <stdbool.h>
bool test_native_calls(const char *provider, const char *generated, const char *unload_marker);
