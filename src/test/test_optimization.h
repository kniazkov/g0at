/**
 * @file test_optimization.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Optimization mode regression tests.
 */
#pragma once
#include <stdbool.h>
bool test_optimization_options();
bool test_optimization_modes();
bool test_optimized_if_bytecode();

/** @brief Dead storage, retained effects, visual history, and restoration. */
bool test_unused_bindings();
