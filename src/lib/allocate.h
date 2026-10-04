/**
 * @file allocate.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Memory allocation utility for the project.
 */

#pragma once

#include <stddef.h>

#ifdef MEMORY_DEBUG
void *_ALLOC(size_t size, const char *file_name, int line);
void *_CALLOC(size_t size, const char *file_name, int line);
#else
void *_ALLOC(size_t size);
void *_CALLOC(size_t size);
#endif
void _FREE(void *ptr);

/**
 * @brief Allocates fundamentally aligned storage; zero size reserves one byte.
 * Failure or size overflow terminates the process. MEMORY_DEBUG tracks allocation
 * sites and checks trailing guard bytes on FREE().
 */
#ifdef MEMORY_DEBUG
#    define ALLOC(size) _ALLOC(size, __FILE__, __LINE__)
#else
#    define ALLOC(size) _ALLOC(size)
#endif

/** @brief Like ALLOC(), with the requested bytes zeroed. */
#ifdef MEMORY_DEBUG
#    define CALLOC(size) _CALLOC(size, __FILE__, __LINE__)
#else
#    define CALLOC(size) _CALLOC(size)
#endif

/**
 * @brief Releases ALLOC()/CALLOC() storage; NULL is a no-op.
 * MEMORY_DEBUG guard corruption terminates the process.
 */
#define FREE _FREE

/** @brief Returns live payload bytes, excluding allocator overhead. */
size_t get_allocated_memory_size(void);

/** @brief Lists live allocations to stderr; a no-op without MEMORY_DEBUG. */
void print_list_of_memory_blocks(void);
