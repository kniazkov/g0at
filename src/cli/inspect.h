/** @file inspect.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Metadata-only inspector command; never executes module code.
 */
#pragma once
#include <stdbool.h>
bool inspect_wants_json(int argc, char **argv);
int inspect_module_command(int argc, char **argv);
