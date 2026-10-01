/**
 * @file gc.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Garbage Collection for the Goat Virtual Machine.
 */

#pragma once

#include "model/process.h"

/** @brief Performs garbage collection on the specified process. */
void collect_garbage(process_t *proc);
