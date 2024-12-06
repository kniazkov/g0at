/**
 * @file allocate.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Memory allocation utility for the project.
 */

#pragma once

#include <stdlib.h>

/**
 * @brief Allocates memory of the given size and handles out-of-memory situations.
 *
 * This function attempts to allocate a block of memory of the specified size using 
 * `malloc()`. If the allocation fails (i.e., the system runs out of memory), an error 
 * message is printed to the error stream, and the program terminates with a failure exit code 
 * (EXIT_FAILURE). In our project, running out of memory is considered a critical error 
 * and no recovery is possible, so the program is forced to exit immediately.
 * 
 * The key benefit of this approach is that the function will never return `NULL`, 
 * eliminating the need for null pointer checks in the rest of the code. This simplifies 
 * the code, making it more readable and maintainable, as there is no need to handle memory 
 * allocation failures at every call site. It allows developers to focus on actual logic 
 * without worrying about potential memory allocation issues.
 * 
 * @param size The size of the memory block to allocate in bytes.
 * @return A pointer to the allocated memory block. This pointer is guaranteed to be non-NULL.
 */
void *allocate(int size);
