/**
 * @file test_analysis.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Tests for analysis observations.
 */
#pragma once
#include <stdbool.h>

bool test_analysis_collector();
bool test_analysis_collector_text();
bool test_analysis_observations();
bool test_analysis_branch_observations();
bool test_analysis_options();

bool test_unknown_expression_values();
bool test_unknown_values_in_analysis();
bool test_valueless_nodes_and_known_values();

bool test_abstract_truthiness();
bool test_if_dispatch();
bool test_if_return_values();
