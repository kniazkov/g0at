/**
 * @file allocate.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Memory allocation utility for the project.
 */

#include "allocate.h"

#include "alignment.h"
#include "file_system.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief The extra number of debug bytes added to the allocated block size when MEMORY_DEBUG is
 * enabled. */
#ifdef MEMORY_DEBUG
#    define EXTRA_DEBUG_BYTES 8
#else
#    define EXTRA_DEBUG_BYTES 0
#endif

/** @brief Tracks the total allocated memory size. */
static size_t allocated_memory_size = 0;

/** @brief Protects accounting and the debug list across independent native contexts. */
static atomic_flag accounting_lock = ATOMIC_FLAG_INIT;

static void lock_accounting(void) {
    while (atomic_flag_test_and_set_explicit(&accounting_lock, memory_order_acquire)) {
    }
}

static void unlock_accounting(void) {
    atomic_flag_clear_explicit(&accounting_lock, memory_order_release);
}

/** @brief Structure used for storing the size of allocated memory block. */
typedef struct memory_header_t memory_header_t;

struct memory_header_t {
    void *allocation; /**< Original malloc pointer, before alignment padding. */
    _Alignas(memory_alignment_t) size_t size; /**< Payload size; preserve malloc alignment. */
#ifdef MEMORY_DEBUG
    const char *file_name;     /**< The name of the file where the memory is allocated. */
    int line;                  /**< Number of the line on which memory is allocated. */
    memory_header_t *previous; /**< Previous header. */
    memory_header_t *next;     /**< Next header. */
#endif
};

#ifdef MEMORY_DEBUG
/** @brief The first memory block in the linked block list */
static memory_header_t *first_block = NULL;

/** @brief The last memory block in the linked block list */
static memory_header_t *last_block = NULL;
#endif

size_t allocation_size_add(size_t left, size_t right) {
    if (right > SIZE_MAX - left) {
        FPRINTF(stderr, "\nAllocation size overflow.\n");
        exit(EXIT_FAILURE);
    }
    return left + right;
}

size_t allocation_size_multiply(size_t count, size_t size) {
    if (size && count > SIZE_MAX / size) {
        FPRINTF(stderr, "\nAllocation size overflow.\n");
        exit(EXIT_FAILURE);
    }
    return count * size;
}

#ifdef MEMORY_DEBUG
void *_ALLOC(size_t size, const char *file_name, int line) {
#else
void *_ALLOC(size_t size) {
#endif
    if (size < 1) {
        size = 1;
    }
    const size_t padding = _Alignof(memory_alignment_t) - 1;
    if (size > SIZE_MAX - sizeof(memory_header_t) - EXTRA_DEBUG_BYTES - padding) {
        FPRINTF(stderr, "\nAllocation size overflow.\n");
        exit(EXIT_FAILURE);
    }
    size_t total_size = sizeof(memory_header_t) + size + EXTRA_DEBUG_BYTES + padding;
    void *allocation = malloc(total_size);

    if (!allocation) {
        FPRINTF(stderr, "\nOut of memory.\n");
        exit(EXIT_FAILURE);
    }

    /* The CRT heap may have weaker alignment than the compiler's memory_alignment_t. */
    size_t offset =
        (_Alignof(memory_alignment_t) - (uintptr_t)allocation % _Alignof(memory_alignment_t))
        % _Alignof(memory_alignment_t);
    memory_header_t *header = (memory_header_t *)((char *)allocation + offset);
    header->allocation = allocation;
    header->size = size;
    lock_accounting();
    if (size > SIZE_MAX - allocated_memory_size) {
        unlock_accounting();
        FPRINTF(stderr, "\nAllocation size overflow.\n");
        exit(EXIT_FAILURE);
    }
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

    unlock_accounting();
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
            FPRINTF(stderr,
                    "\nMemory corruption detected! File: %s, line: %d\n",
                    header->file_name,
                    header->line);
            exit(EXIT_FAILURE);
        }
    }

    lock_accounting();
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

#ifndef MEMORY_DEBUG
    lock_accounting();
#endif
    allocated_memory_size -= size;
    unlock_accounting();
    free(header->allocation);
}

size_t get_allocated_memory_size(void) {
    lock_accounting();
    size_t size = allocated_memory_size;
    unlock_accounting();
    return size;
}

void print_list_of_memory_blocks(void) {
#ifdef MEMORY_DEBUG
    lock_accounting();
    memory_header_t *header = first_block;
    while (header) {
        FPRINTF(stderr,
                "%s, %d: %zu byte%s\n",
                header->file_name,
                header->line,
                header->size,
                header->size == 1 ? "" : "s");
        header = header->next;
    }
    unlock_accounting();
#endif
}
