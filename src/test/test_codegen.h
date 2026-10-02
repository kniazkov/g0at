/**
 * @file test_codegen.h
 * @copyright 2026 Ivan Kniazkov
 * @brief A set of tests for testing code generator.
 */

#pragma once

#include <stdbool.h>

/**
 * @brief Tests the functionality of the data builder.
 * @return True if the test passes, false otherwise.
 */
bool test_data_builder();

/**
 * @brief Tests the functionality of the linker.
 * @return True if the test passes, false otherwise.
 */
bool test_linker();

/** @brief Tests unsupported code generation through the node virtual table. */
bool test_node_codegen_stubs();

/** @brief Tests try/catch AST structure, bytecode and conservative analysis. */
bool test_try_catch_structure(void);
bool test_try_catch_bytecode(void);
bool test_try_catch_analysis_placeholder(void);
