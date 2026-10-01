/**
 * @file analysis_test_support.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Shared analysis test helpers.
 */
#pragma once
#include "graph/node.h"
#include "parser/parser.h"

/** @brief Parses into the supplied arenas; returns NULL on a parse error. Source is borrowed. */
node_t *parse_analysis_test_program(parser_memory_t *memory, string_value_t source);
