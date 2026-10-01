/**
 * @file test_abstract_state.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Abstract-state isolation and lifetime tests.
 */
#pragma once
#include <stdbool.h>

bool test_abstract_state_clone_lifetimes();
bool test_abstract_state_many_declarations();
bool test_abstract_state_join_isolation();
bool test_abstract_state_clone_metadata();
bool test_abstract_state_branch_program();
