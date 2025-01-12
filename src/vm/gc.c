/**
 * @file gc.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Garbage Collection for the Goat Virtual Machine.
 * 
 * This file contains the implementation of garbage collection functions that allow the
 * Goat virtual machine to manage memory efficiently by cleaning up unreachable objects.
 */

#include "gc.h"
#include "model/object.h"
#include "model/thread.h"
#include "model/context.h"

/**
 * @brief Marks the objects on a thread's data stack and context data.
 * 
 * This function marks the objects on the thread's data stack as reachable, as well as the 
 * object associated with the thread's execution context. This helps in identifying the objects
 * that are still in use and ensures they are not collected during garbage collection.
 * 
 * @param thread A pointer to the thread whose stack and context data are to be processed.
 */
static void mark_objects_in_context_and_stack(thread_t *thread) {
    thread->context->data->vtbl->mark(thread->context->data);
    for (size_t i = 0; i < thread->data_stack->size; i++) {
        object_t *obj = thread->data_stack->objects[i];
        obj->vtbl->mark(obj);
    }
}

/**
 * @brief Marks all reachable objects in the process.
 * 
 * This function iterates through the objects in the process and marks those that are reachable.
 * 
 * @param proc A pointer to the process whose objects will be marked.
 */
static void mark_reachable_objects(process_t *proc) {
    thread_t *thread = proc->main_thread;
    do {
        mark_objects_in_context_and_stack(thread);
        thread = thread->next;
    } while (thread != proc->main_thread);
}

/**
 * @brief Sweeps all unreachable objects in the process.
 * 
 * This function iterates through all objects in the process and frees those that are unmarked
 * (i.e., unreachable). These objects are no longer in use and can be safely deallocated.
 * 
 * @param proc A pointer to the process whose objects will be swept.
 */
static void sweep_unreachable_objects(process_t *proc) {
    object_t *obj = proc->objects.head;
    while (obj != NULL) {
        object_t *next = obj->next;
        obj->vtbl->sweep(obj);
        obj = next;
    }
}

/**
 * @brief Performs garbage collection on the specified process.
 * 
 * The garbage collection process involves marking objects that are reachable and then sweeping
 * away the ones that are no longer referenced. The function will iterate through all objects in
 * the given process and clean up the memory by releasing unmarked objects.
 * 
 * @param proc A pointer to the process from which garbage will be collected.
 */
void collect_garbage(process_t *proc) {
    mark_reachable_objects(proc);
    sweep_unreachable_objects(proc);
}
