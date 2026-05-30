/**
 * @file queue.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Definitions of structures and function prototypes for a queue.
 *
 * This header file provides the interface for a simple FIFO queue that stores
 * pointers to arbitrary data. Elements are appended to the end of the queue and
 * removed from the beginning.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

/**
 * @struct queue_item_t
 * @brief A single linked-list item stored inside a queue.
 */
typedef struct queue_item_t {
    /**
     * @brief Pointer stored in the queue item.
     */
    void *data;

    /**
     * @brief Pointer to the next queue item.
     */
    struct queue_item_t *next;
} queue_item_t;

/**
 * @struct queue_t
 * @brief A FIFO queue of pointers.
 *
 * Stores pointers to arbitrary data. The queue owns only its internal item
 * wrappers and does not own the stored pointers.
 */
typedef struct {
    /**
     * @brief First item in the queue.
     */
    queue_item_t *first;

    /**
     * @brief Last item in the queue.
     */
    queue_item_t *last;
} queue_t;

/**
 * @brief Creates a new empty queue.
 *
 * @return A pointer to the created queue.
 */
queue_t *create_queue();

/**
 * @brief Destroys a queue and frees all internal queue items.
 *
 * This function frees the queue structure and its internal item wrappers, but
 * does not free the pointers stored in the queue.
 *
 * @param queue A pointer to the queue to destroy.
 */
void destroy_queue(queue_t *queue);

/**
 * @brief Adds an item to the end of a queue.
 *
 * @param queue A pointer to the queue.
 * @param item Pointer to add to the queue.
 */
void enqueue(queue_t *queue, void *item);

/**
 * @brief Removes and returns the first item from a queue.
 *
 * If the queue is empty, returns NULL.
 *
 * @param queue A pointer to the queue.
 * @return The first stored pointer, or NULL if the queue is empty.
 */
void *dequeue(queue_t *queue);

/**
 * @brief Checks whether a queue is empty.
 *
 * @param queue A pointer to the queue.
 * @return `true` if the queue is empty, otherwise `false`.
 */
static inline bool is_queue_empty(const queue_t *queue) {
    return queue->first == NULL;
}
