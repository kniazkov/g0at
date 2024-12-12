/**
 * @file allocate.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Memory allocation utility for the project.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "allocate.h"

/**
 * @brief The extra number of debug bytes added to the allocated block size 
 *  when MEMORY_DEBUG is enabled.
 */
#ifdef MEMORY_DEBUG
    #define EXTRA_DEBUG_BYTES 8
#else
    #define EXTRA_DEBUG_BYTES 0
#endif

/**
 * @brief Tracks the total allocated memory size.
 */
static size_t allocated_memory_size = 0;

/**
 * @brief Structure used for storing the size of allocated memory block.
 */
typedef struct {
    size_t size; /**< Size of the allocated memory block (in bytes). */
} memory_header_t;

void *ALLOC(size_t size) {
    if (size < 1) {
        size = 1;
    }
    size_t total_size = sizeof(memory_header_t) + size + EXTRA_DEBUG_BYTES;
    memory_header_t *header = (memory_header_t *)malloc(total_size);

    if (!header) {
        fprintf(stderr, "\nOut of memory.\n");
        exit(EXIT_FAILURE);
    }

    header->size = size;
    allocated_memory_size += size;

#ifdef MEMORY_DEBUG
    memset((char *)header + sizeof(memory_header_t) + size, 0xFF, EXTRA_DEBUG_BYTES);
#endif

    return (char *)header + sizeof(memory_header_t);
}

void *CALLOC(size_t size) {
    void *ptr = ALLOC(size);
    memset(ptr, 0, size);
    return ptr;
}

void FREE(void *ptr) {
    if (!ptr) {
        return;
    }

    memory_header_t *header = (memory_header_t *)((char *)ptr - sizeof(memory_header_t));
    size_t size = header->size;

#ifdef MEMORY_DEBUG
    unsigned char *debug_bytes = (unsigned char *)ptr + size;
    for (int i = 0; i < EXTRA_DEBUG_BYTES; i++) {
        if (debug_bytes[i] != 0xFF) {
            fprintf(stderr, "\nMemory corruption detected!\n");
            exit(EXIT_FAILURE);
        }
    }
#endif

    allocated_memory_size -= size;
    free(header);
}

size_t get_allocated_memory_size() {
    return allocated_memory_size;
}
