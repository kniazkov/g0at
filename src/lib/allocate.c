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
typedef struct memory_header_t memory_header_t;
struct memory_header_t {
    size_t size; /**< Size of the allocated memory block (in bytes). */
#ifdef MEMORY_DEBUG
    const char *file_name; /**< The name of the file where the memory is allocated. */
    int line; /**< Number of the line on which memory is allocated. */
    memory_header_t *previous; /**< Previous header. */
    memory_header_t *next; /**< Next header. */
#endif
};

#ifdef MEMORY_DEBUG
/**
 * @brief The first memory block in the linked block list
 */
static memory_header_t *first_block = NULL;

/**
 * @brief The last memory block in the linked block list
 */
static memory_header_t *last_block = NULL;
#endif

#ifdef MEMORY_DEBUG
void *_ALLOC(size_t size, const char *file_name, int line) {
#else
void *_ALLOC(size_t size) {
#endif
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
    header->file_name = file_name;
    header->line = line;
    if (last_block) {
        header->previous = last_block;
        last_block->next = header;
    } else {
        header->previous = NULL;
        first_block = header;
    }
    header->next = NULL;
    last_block = header;
    memset((char *)header + sizeof(memory_header_t) + size, 0xFF, EXTRA_DEBUG_BYTES);
#endif

    return (char *)header + sizeof(memory_header_t);
}

#ifdef MEMORY_DEBUG
void *_CALLOC(size_t size, const char *file_name, int line) {
    void *ptr = _ALLOC(size, file_name, line);
    memset(ptr, 0, size);
    return ptr;
}
#else
void *_CALLOC(size_t size) {
    void *ptr = _ALLOC(size);
    memset(ptr, 0, size);
    return ptr;
}
#endif

void _FREE(void *ptr) {
    if (!ptr) {
        return;
    }

    memory_header_t *header = (memory_header_t *)((char *)ptr - sizeof(memory_header_t));
    size_t size = header->size;

#ifdef MEMORY_DEBUG
    unsigned char *debug_bytes = (unsigned char *)ptr + size;
    for (int i = 0; i < EXTRA_DEBUG_BYTES; i++) {
        if (debug_bytes[i] != 0xFF) {
            fprintf(stderr, "\nMemory corruption detected! File: %s, line: %d\n",
                    header->file_name, header->line);
            exit(EXIT_FAILURE);
        }
    }

    if (header->previous) {
        header->previous->next = header->next;
    } else {
        first_block = header->next;
    }
    if (header->next) {
        header->next->previous = header->previous;
    } else {
        last_block = header->previous;
    }
#endif

    allocated_memory_size -= size;
    free(header);
}

size_t get_allocated_memory_size() {
    return allocated_memory_size;
}

void print_list_of_memory_blocks() {
#ifdef MEMORY_DEBUG
    memory_header_t *header = first_block;
    while(header) {
        fprintf(stderr, "%s, %d: %zu byte%s\n", header->file_name, header->line, header->size,
                header->size == 1 ? "" : "s");
        header = header->next;
    }
#endif
}
