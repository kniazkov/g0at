/** @file child_process_internal.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Shared process capture helpers.
 */
#pragma once
#include "child_process.h"
void child_capture(child_output_t *output, const unsigned char *bytes, size_t size);
