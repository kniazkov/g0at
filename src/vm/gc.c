/**
 * @file gc.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Garbage Collection for the Goat Virtual Machine.
 */

#include "gc.h"

#include "lib/allocate.h"
#include "model/context.h"
#include "model/object.h"
#include "model/thread.h"

#include <string.h>

/** @brief Marks the objects on a thread's data stack and context data. */
static void mark_objects_in_context_and_stack(thread_t *thread) {
    for (context_t *ctx = thread->context; ctx; ctx = ctx->previous) {
        mark_object(ctx->data);
        if (ctx->module)
            mark_function_access(&ctx->access);
        if (ctx->pending_value)
            mark_object(ctx->pending_value);
    }
    if (thread->exception.value)
        mark_object(thread->exception.value);
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
    for (size_t index = 0; index < proc->gc_roots_size; index++) {
        mark_object(proc->gc_roots[index]);
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

void add_gc_root(process_t *proc, object_t *obj) {
    for (size_t index = 0; index < proc->gc_roots_size; index++) {
        if (proc->gc_roots[index] == obj)
            return;
    }
    object_t **roots = ALLOC((proc->gc_roots_size + 1) * sizeof(object_t *));
    if (proc->gc_roots_size > 0) {
        memcpy(roots, proc->gc_roots, proc->gc_roots_size * sizeof(object_t *));
        FREE(proc->gc_roots);
    }
    roots[proc->gc_roots_size] = obj;
    proc->gc_roots = roots;
    proc->gc_roots_size++;
}

void remove_gc_root(process_t *proc, object_t *obj) {
    for (size_t index = 0; index < proc->gc_roots_size; index++) {
        if (proc->gc_roots[index] == obj) {
            if (proc->gc_roots_size == 1) {
                FREE(proc->gc_roots);
                proc->gc_roots = NULL;
                proc->gc_roots_size = 0;
            } else {
                object_t **roots = ALLOC((proc->gc_roots_size - 1) * sizeof(object_t *));
                memcpy(roots, proc->gc_roots, index * sizeof(object_t *));
                memcpy(roots + index,
                       proc->gc_roots + index + 1,
                       (proc->gc_roots_size - index - 1) * sizeof(object_t *));
                FREE(proc->gc_roots);
                proc->gc_roots = roots;
                proc->gc_roots_size--;
            }
            return;
        }
    }
}
