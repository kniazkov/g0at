/**
 * @file thread.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implements functions for managing threads in the Goat programming language.
 * 
 * This file contains the implementation of functions for creating and destroying threads
 * within a Goat process.
 */

#include <stdlib.h>
#include <string.h>

#include "thread.h"
#include "process.h"
#include "context.h"
#include "object_stack.h"
#include "lib/allocate.h"

/**
 * @brief ID of the last created thread.
 */
static uint64_t last_thread_id = 0;

thread_t *create_thread(process_t *process, context_t *context) {
    thread_t *thread = (thread_t *)CALLOC(sizeof(thread_t));
    thread->id = ++last_thread_id;
    thread->process = process;
    if (process->main_thread == NULL) {
        thread->previous = thread;
        thread->next = thread;
        process->main_thread = thread;
    } else {
        thread_t *prev_thread = process->main_thread->previous;
        prev_thread->next = thread;
        thread->previous = prev_thread;
        thread->next = process->main_thread;
        process->main_thread->previous = thread;
    }
    thread->context = context;
    thread->data_stack = create_object_stack();
    return thread;
}

void destroy_thread(thread_t *thread) {
    if (thread->previous == thread) {
        thread->process->main_thread = NULL;
    } else {
        thread->previous->next = thread->next;
        thread->next->previous = thread->previous;
    }
    context_t *root_context = get_root_context();
    context_t *context = thread->context;
    while (context != NULL && context != root_context) {
        context = destroy_context(context);
    }
    destroy_object_stack(thread->data_stack);
    FREE(thread);
}
