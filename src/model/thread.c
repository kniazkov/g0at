/**
 * @file thread.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implements functions for managing threads in the Goat programming language.
 */

#include "thread.h"

#include "context.h"
#include "lib/memory/allocate.h"
#include "object.h"
#include "object_stack.h"
#include "process.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/** @brief ID of the last created thread. */
static atomic_uint_fast64_t last_thread_id = 0;

thread_t *create_thread(process_t *process, context_t *context) {
    if (!process || !context || !object_belongs_to_process(process, context->data))
        return NULL;
    thread_t *thread = (thread_t *)CALLOC(sizeof(thread_t));
    thread->id = atomic_fetch_add(&last_thread_id, 1) + 1;
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
    thread->data_stack = create_object_stack(process);
    return thread;
}

void destroy_thread(thread_t *thread) {
    if (thread->previous == thread) {
        thread->process->main_thread = NULL;
    } else {
        thread->previous->next = thread->next;
        thread->next->previous = thread->previous;
        if (thread->process->main_thread == thread)
            thread->process->main_thread = thread->next;
    }
    discard_thread_work(thread);
    FREE(thread->native_diagnostic);
    clear_exception(&thread->exception);
    DECREFIF(thread->completion_value);
    FREE(thread);
}

void discard_thread_work(thread_t *thread) {
    context_t *root_context = get_root_context();
    while (thread->context && thread->context != root_context)
        thread->context = destroy_context(thread->context);
    thread->context = NULL;
    if (thread->data_stack && thread->data_stack != &thread->completion_stack) {
        while (thread->data_stack->size) {
            object_t *value = pop_object_from_stack(thread->data_stack);
            DECREF(value);
        }
        destroy_object_stack(thread->data_stack);
    }
    thread->data_stack = NULL;
    DECREFIF(thread->start_function);
    thread->start_function = NULL;
}
