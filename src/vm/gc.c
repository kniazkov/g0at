/**
 * @file gc.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Garbage Collection for the Goat Virtual Machine.
 */

#include "gc.h"
#include "model/object.h"
#include "model/thread.h"
#include "model/context.h"

/** @brief Marks the objects on a thread's data stack and context data. */
static void mark_objects_in_context_and_stack(thread_t *thread) {
    mark_object(thread->context->data);
    for (size_t index = 0; index < thread->data_stack->size; index++) {
        object_t *obj = thread->data_stack->objects[index];
        mark_object(obj);
    }
}

/** @brief Marks all reachable objects in the process. */
static void mark_reachable_objects(process_t *proc) {
    for (size_t index = 0; index < proc->string_cache_size; index++) {
        mark_object(proc->string_cache[index]);
    }
    thread_t *thread = proc->main_thread;
    do {
        mark_objects_in_context_and_stack(thread);
        thread = thread->next;
    } while (thread != proc->main_thread);
}

/**
 * @brief Sweeps all unreachable objects in the process.
 *
 * Iterates through all objects in the process and frees those that are unmarked (i.e.,
 * unreachable).
 */
static void sweep_unreachable_objects(process_t *proc) {
    object_t *last_live = NULL;
    object_t *obj = proc->objects.head;
    while (obj != NULL) {
        if (sweep_object(obj)) {
            obj = last_live ? last_live->next : proc->objects.head;
        } else {
            last_live = obj;
            obj = obj->next;
        }
    }
}

/** @brief Performs garbage collection on the specified process. */
void collect_garbage(process_t *proc) {
    mark_reachable_objects(proc);
    sweep_unreachable_objects(proc);
}
