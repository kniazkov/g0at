/** @file windows_command_line.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Quoting for the Windows C runtime argument parser, not for cmd.exe.
 */
#pragma once
/** @brief Quotes a NULL-terminated argument array; caller frees, NULL means too long. */
char *create_windows_command_line(const char *const *arguments);
