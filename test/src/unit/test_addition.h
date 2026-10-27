/** @file test_addition.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Addition across the abstract domain, object models and VM.
 */
#pragma once
#include <stdbool.h>
bool test_addition_models_and_constants(void);
bool test_addition_domains(void);
bool test_addition_ranges(void);
bool test_addition_vm_errors(void);
