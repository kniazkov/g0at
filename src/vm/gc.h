/**
 * @file gc.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Garbage Collection for the Goat Virtual Machine.
 * 
 * This file contains the necessary functions and definitions for garbage collection
 * in the Goat virtual machine. The garbage collection process ensures that unused objects are
 * properly cleaned up to avoid memory leaks.
 */

#pragma once

#include "model/process.h"

/**
 * @brief Performs garbage collection on the specified process.
 * 
 * The garbage collection process involves marking objects that are reachable and then sweeping
 * away the ones that are no longer referenced. The function will iterate through all objects in
 * the given process and clean up the memory by releasing unmarked objects.
 * 
 * @param proc A pointer to the process from which garbage will be collected.
 */
void collect_garbage(process_t *proc);
