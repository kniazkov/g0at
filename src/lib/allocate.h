/**
 * @file allocate.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Memory allocation utility for the project.
 */

#pragma once

#include <stddef.h>

/**
 * @brief Allocates memory of the given size and handles out-of-memory situations.
 * 
 * In debugging mode, extra bytes are added at the end of the allocated block to detect
 * memory corruption.
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
 * allocation failures at every call site.
 * 
 * @param size The size of the memory block to allocate in bytes.
 * @return A pointer to the allocated memory block. This pointer is guaranteed to be non-NULL.
 */
void *ALLOC(size_t size);

/**
 * @brief Allocates a block of memory and initializes it to zero.
 * 
 * This function behaves like `ALLOC`, but in addition, it zeroes out the memory 
 * that is allocated. It is useful when you need to ensure that the memory is 
 * clean before using it.
 * 
 * @param size The size of the memory block to allocate and zero out (in bytes).
 * @return A pointer to the allocated and zero-initialized memory block. 
 *         This pointer is guaranteed to be non-NULL.
 */
void *CALLOC(size_t size);

/**
 * @brief Frees the previously allocated memory block.
 * 
 * This function deallocates the memory block previously allocated by `ALLOC` or `CALLOC`. 
 * It also performs a check for memory corruption if debugging is enabled.
 * 
 * @param ptr A pointer to the memory block to be freed. If the pointer is NULL, 
 *            the function will do nothing.
 * 
 * @note If the memory block has been corrupted (in debugging mode), the program 
 *       will print an error message and exit.
 */
void FREE(void *ptr);

/**
 * @brief Returns the total amount of memory allocated by the memory manager.
 * 
 * This function returns the total size of memory that has been allocated using 
 * `ALLOC` and `CALLOC` (excluding memory freed using `FREE`).
 * 
 * @return The total allocated memory size (in bytes).
 */
size_t get_allocated_memory_size();
