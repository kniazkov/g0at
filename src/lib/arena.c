/**
 * @file arena.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of memory arena management functions.
 *
 * This file contains the implementation of a memory arena, a memory allocation scheme that manages
 * large blocks of memory (chunks) and allocates smaller portions from them.
 * The memory is allocated in chunks, and individual deallocations are not supported.
 * Instead, the entire memory arena is freed when it is no longer needed.
 * 
 * The functions in this file allow the initialization, allocation, and freeing of memory
 * within the arena. Memory is allocated in blocks, and each block can be used to serve
 * multiple allocation requests.
 */

#include <memory.h>
#include <stdarg.h>
#include <stdio.h>

#include "arena.h"
#include "allocate.h"
#include "string_ext.h"

arena_t *create_arena() {
    arena_t* arena = (arena_t*)ALLOC(sizeof(arena_t));
    chunk_t* chunk = (chunk_t*)ALLOC(sizeof(chunk_t) + CHUNK_SIZE);
    
    chunk->next = NULL;
    chunk->begin = (char*)chunk + sizeof(chunk_t);
    chunk->unized_size = CHUNK_SIZE;
    
    arena->first_chunk = chunk;
    arena->ptr = chunk->begin;
    
    return arena;
}

void *alloc_from_arena(arena_t* arena, size_t size) {
    if (size == 0) {
        size = 1;
    }

    if (size > BIG_OBJECT_SIZE) {
        chunk_t* chunk = (chunk_t*)ALLOC(sizeof(chunk_t) + size);
        chunk->next = arena->first_chunk->next;
        arena->first_chunk->next = chunk;
        chunk->begin = (char*)chunk + sizeof(chunk_t);
        chunk->unized_size = 0;
        return chunk->begin;
    }

    if (arena->first_chunk->unized_size > size) {
        void *ptr = arena->ptr;
        arena->ptr += size;
        arena->first_chunk->unized_size -= size;
        return ptr;
    } else {
        chunk_t* chunk = (chunk_t*)ALLOC(sizeof(chunk_t) + CHUNK_SIZE);
        chunk->next = arena->first_chunk;
        arena->first_chunk = chunk;
        chunk->begin = (char*)chunk + sizeof(chunk_t);
        chunk->unized_size = CHUNK_SIZE - size;
        arena->ptr = chunk->begin + size;
        return chunk->begin;
    }
}

void *alloc_zeroed_from_arena(arena_t* arena, size_t size) {
    void *ptr = alloc_from_arena(arena, size);
    memset(ptr, 0, size);
    return ptr;
}

void *copy_object_to_arena(arena_t* arena, const void* object, size_t size) {
    void* copied_object = alloc_from_arena(arena, size);
    memcpy(copied_object, object, size);
    return copied_object;
}

string_view_t copy_string_to_arena(arena_t* arena, const wchar_t* string, size_t length) {
    if (length == 0) {
        return (string_view_t){ L"", 0 };
    }
    wchar_t* copied_string = (wchar_t*)alloc_from_arena(arena, (length + 1) * sizeof(wchar_t));
    memcpy(copied_string, string, length * sizeof(wchar_t));
    copied_string[length] = L'\0';
    return (string_view_t){ copied_string, length };
}

string_view_t format_string_to_arena(arena_t *arena, const wchar_t *format, ...) {
    va_list args;
    va_start(args, format);
    string_value_t value = format_string_vargs(format, args);
    va_end(args);
    size_t data_length = (value.length + 1) * sizeof(wchar_t*);
    wchar_t *buffer = alloc_from_arena(arena, data_length);
    memcpy(buffer, value.data, data_length);
    FREE_STRING(value);
    return (string_view_t){ buffer, value.length };
}

void destroy_arena(arena_t* arena) {
    chunk_t* current_chunk = arena->first_chunk;
    while (current_chunk != NULL) {
        chunk_t* next_chunk = current_chunk->next;
        FREE(current_chunk);
        current_chunk = next_chunk;
    }
    FREE(arena);
}
