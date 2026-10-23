/** @file file_system_internal.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Private filesystem platform adapters.
 */
#pragma once
#include "file_system.h"
#include "windows_target.h"
bool fs_fail(fs_error_t *error,
             fs_status_t status,
             unsigned long code,
             const char *operation,
             const char *path);
bool fs_native_error(fs_error_t *error, const char *operation, const char *path);
bool fs_valid(const char *path, fs_error_t *error);
#ifdef _WIN32
#    include <wchar.h>
wchar_t *fs_wide(const char *text);
char *fs_utf8(const wchar_t *text);
#endif
