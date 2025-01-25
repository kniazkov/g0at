/**
 * @file arena.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Definitions of structures and function prototypes for a memory arena.
 *
 * A memory arena is a memory allocation scheme where memory is allocated in large chunks,
 * and small portions are then allocated from these chunks. Memory cannot be freed individually;
 * instead, all memory allocated within the arena is freed at once when the arena is destroyed.
 * This approach can lead to improved performance in cases where many small allocations are made
 * and freeing memory individually is not needed.
 */

#pragma once

#include <stddef.h>
#include <wchar.h>

/**
 * @struct arena_t
 * @brief Forward declaration for memory arena structure.
 */
typedef struct arena_t arena_t;

/**
 * @struct chunk_t
 * @brief Forward declaration for chunk of memory in the memory arena.
 */
typedef struct chunk_t chunk_t;

/**
 * @struct chunk_t
 * @brief The memory chunk structure.
 *
 * This structure holds information about a single chunk in the arena's memory pool.
 * Each chunk contains a pointer to the next chunk, its size, and the pointer to the allocated
 * memory block. Chunks are linked together to form a chain within the arena, allowing for
 * efficient memory allocation and deallocation in bulk.
 */
/**
 * @struct chunk_t
 * @brief The memory chunk structure.
 *
 * This structure holds information about a single chunk in the arena's memory pool.
 * Each chunk contains a pointer to the next chunk, and the `begin` and `end` pointers 
 * define the range of memory in this chunk. The chunks are linked together to form a chain 
 * within the arena, allowing for efficient memory allocation and deallocation in bulk.
 */
struct chunk_t {
    /**
     * @brief Pointer to the next chunk in the chain.
     * 
     * This pointer is used to link chunks together. If this chunk is the last in the chain,
     * this pointer will be `NULL`.
     */
    struct chunk_t* next;

    /**
     * @brief Pointer to the first byte of allocated memory in this chunk.
     * 
     * This pointer points to the beginning of the memory block allocated for this chunk.
     */
    char* begin;

    /**
     * @brief Remaining unused memory size in the current chunk.
     * 
     * This field tracks how much space is available in the current chunk. It is decremented
     * every time memory is allocated from the chunk. When `unized_size` reaches 0, it indicates
     * that the chunk is fully used, and a new chunk should be allocated.
     */
    size_t unized_size;
};

/**
 * @struct arena_t
 * @brief Represents the memory arena that manages memory allocation.
 *
 * The arena is a memory pool that consists of one or more chunks of memory. It manages memory
 * allocations by dividing each chunk into smaller pieces, allocating memory from these chunks
 * without freeing them individually. The arena will free all memory when it is destroyed.
 */
struct arena_t {
    /**
     * @brief Pointer to the first chunk in the memory arena.
     * 
     * This pointer points to the first chunk in the arena. The chunks are linked together
     * in a chain, and the arena allocates memory from these chunks. If the arena is empty,
     * this pointer will be NULL.
     */
    chunk_t* first_chunk;

    /**
     * @brief Pointer to the current position in the current chunk.
     * 
     * This pointer indicates the current position within the current chunk where the next 
     * memory allocation will occur. When the end of the current chunk is reached, the arena
     * will allocate a new chunk.
     */
    char* ptr;
};

/**
 * @def CHUNK_SIZE
 * @brief Size of a chunk in the memory arena.
 * 
 * The default chunk size is set to be slightly less than a 128k to account for memory
 * alignment and header overhead. This ensures that the chunk size fits more efficiently
 * within memory page boundaries, avoiding wasted space.
 */
#define CHUNK_SIZE (128 * 1024 - sizeof(chunk_t) - 64)

/**
 * @def BIG_OBJECT_SIZE
 * @brief The threshold size for a "big object" in the memory arena.
 * 
 * Objects larger than this size (in bytes) will cause the arena to allocate a separate chunk
 * for the object, instead of using the current chunk. This ensures that large objects are handled
 * correctly, even if they exceed the default chunk size.
 */
#define BIG_OBJECT_SIZE 256

/**
 * @brief Creates and initializes a memory arena with a default chunk size.
 * 
 * This function initializes a memory arena by allocating a large block of memory 
 * (a "chunk") using the `ALLOC` function. The arena structure is then initialized 
 * with the first chunk and the starting pointer (`ptr`), which will be used 
 * for future memory allocations within the arena.
 * 
 * If memory allocation fails, the program will terminate with a failure exit code.
 * 
 * @return A pointer to the initialized arena structure.
 */
arena_t *create_arena();

/**
 * @brief Allocates memory from the specified memory arena, handling small and large objects.
 * 
 * This function attempts to allocate memory from the specified arena. It handles three
 * cases:
 * 1. If the requested memory size exceeds the `BIG_OBJECT_SIZE` threshold, a new chunk is created
 *    specifically for this allocation, and it is inserted into the chain of chunks.
 * 2. If the requested size is less than 1 byte, 1 byte is allocated to ensure the pointer returned
 *    is valid.
 * 3. If there is insufficient space left in the current chunk, a new chunk of the default size
 *    is allocated, and the memory is taken from there.
 * 4. Otherwise, memory is allocated from the current chunk if there is enough space left.
 * 
 * The allocated memory will be aligned and returned as a pointer to the beginning of the allocated 
 * block. The function ensures that the memory is properly managed and the arena remains
 * in a consistent state.
 * 
 * @param arena A pointer to the arena from which memory should be allocated.
 * @param size The size of the memory block to allocate (in bytes).
 * @return A pointer to the allocated memory block.
 */
void *alloc_from_arena(arena_t* arena, size_t size);

/**
 * @brief Allocates a zero-initialized memory block from the arena.
 * 
 * This function combines memory allocation with zeroing the allocated memory, which is useful
 * when you need to ensure that the allocated block starts in a clean state.
 * 
 * @param arena A pointer to the arena from which memory should be allocated.
 * @param size The size of the memory block to allocate (in bytes).
 * @return A pointer to the zero-initialized memory block.
 */
void *alloc_zeroed_from_arena(arena_t* arena, size_t size);

/**
 * @brief Copies an object to the specified memory arena.
 * 
 * This function creates a copy of the given object by allocating memory for it in the specified
 * arena and copying the contents of the object into the newly allocated memory block.
 * 
 * @param arena A pointer to the memory arena where the object will be copied.
 * @param object A pointer to the object to be copied.
 * @param size The size of the object in bytes.
 * @return A pointer to the copied object in the arena's memory.
 */
void *copy_object_to_arena(arena_t* arena, const void* object, size_t size);

/**
 * @brief Copies a wide-character string to the specified memory arena.
 * 
 * This function creates a copy of the given wide-character string by allocating memory for it
 * in the specified arena and copying the contents of the string into the newly allocated
 * memory block. The resulting string is null-terminated. If the length of the string is zero,
 * the function returns a constant pointer to an empty string (L"").
 * 
 * @param arena A pointer to the memory arena where the string will be copied.
 * @param string A pointer to the wide-character string to be copied.
 * @param length The length of the string (excluding the null terminator).
 * 
 * @return A pointer to the copied string in the arena's memory, or a constant empty string
 *  if length is 0.
 */
wchar_t *copy_string_to_arena(arena_t* arena, const wchar_t* string, size_t length);

/**
 * @brief Formats a string using the memory arena and returns it.
 * 
 * This function allocates the required amount of memory from the arena, formats the string
 * using the provided arguments, and returns a pointer to the resulting string. If the
 * `size_ptr` is not `NULL`, it will store the size of the formatted string
 * (excluding the null terminator).
 * 
 * @param arena A pointer to the arena from which memory should be allocated.
 * @param size_ptr A pointer to a `size_t` variable that will store the size of the formatted
 *  string (excluding the null terminator). If `NULL`, the size is not returned.
 * @param format A format string.
 * @param ... Arguments for formatting.
 * @return A pointer to the formatted string allocated from the arena. 
 *  The caller is responsible for freeing the memory when no longer needed.
 */
wchar_t *format_string_to_arena(arena_t *arena, size_t *size_ptr, const wchar_t *format, ...);

/**
 * @brief Destroys the memory arena and frees all allocated memory.
 * 
 * This function deallocates all memory chunks associated with the arena. It traverses through
 * the list of chunks and frees each chunk's memory, including the arena structure itself.
 * After calling this function, the arena and its memory are no longer valid, and all memory
 * is returned to the system.
 * 
 * If the pointer to the arena is `NULL`, the function does nothing.
 * 
 * @param arena A pointer to the arena to be destroyed.
 */
void destroy_arena(arena_t* arena);
