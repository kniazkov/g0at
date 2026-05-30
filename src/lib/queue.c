/**
 * @file queue.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of functions for a FIFO queue.
 *
 * This source file contains the implementation of a simple queue data structure.
 * The queue stores pointers to arbitrary data and supports appending elements to
 * the end and removing elements from the beginning.
 */

#include "queue.h"
#include "allocate.h"

queue_t *create_queue() {
    queue_t *queue = (queue_t *)CALLOC(sizeof(queue_t));
    return queue;
}

void destroy_queue(queue_t *queue) {
    queue_item_t *item = queue->first;
    while (item != NULL) {
        queue_item_t *next = item->next;
        FREE(item);
        item = next;
    }
    FREE(queue);
}

void enqueue(queue_t *queue, void *data) {
    queue_item_t *item = (queue_item_t *)ALLOC(sizeof(queue_item_t));
    item->data = data;
    item->next = NULL;
    if (queue->last == NULL) {
        queue->first = item;
        queue->last = item;
    }
    else {
        queue->last->next = item;
        queue->last = item;
    }
}

void *dequeue(queue_t *queue) {
    if (queue->first == NULL) {
        return NULL;
    }
    queue_item_t *item = queue->first;
    void *data = item->data;
    queue->first = item->next;
    if (queue->first == NULL) {
        queue->last = NULL;
    }
    FREE(item);
    return data;
}
