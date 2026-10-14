/**
 * @file test_member_access.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Member access (the `.` operator) tests.
 */
#pragma once
#include <stdbool.h>

/** @brief Tests scanning of the member access operator. */
bool test_member_access_scanner(void);

/** @brief Tests parsing of member access expressions. */
bool test_member_access_parser(void);

/** @brief Tests the member access AST node. */
bool test_member_access_ast(void);

/** @brief Tests that a method call is represented as a function call on a member access. */
bool test_member_access_method_call_ast(void);

/** @brief Tests GETPROP and SETPROP opcodes in the virtual machine. */
bool test_member_access_vm(void);
