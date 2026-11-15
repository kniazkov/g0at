/**
 * @file gc.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Garbage Collection for the Goat Virtual Machine.
 */

#include "gc.h"

#include "lib/memory/allocate.h"
#include "model/context.h"
#include "model/module_files.h"
#include "model/object.h"
#include "model/thread.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct gc_root_slot_t {
    object_t *object;
    uint64_t serial;
    size_t next_free;
};

/** @brief Process-local tracing cursor; links are borrowed while the heap is stable. */
typedef struct {
    process_t *process;
    object_t *pending;
} mark_worklist_t;

/** @brief Marks once and enqueues non-leaves; even weak edges are visible to this visitor. */
static void enqueue_mark(object_t *obj, bool owned, void *data) {
    (void)owned;
    mark_worklist_t *work = data;
    if (obj && obj->process == work->process && obj->vtbl->mark)
        obj->vtbl->mark(obj, &work->pending);
}

/** @brief Marks the thread's owned values and execution state. */
static void mark_thread_roots(thread_t *thread, mark_worklist_t *work) {
    /* The spawned callable is not on the operand stack. The worker takes ownership
       from this slot only after entering the process execution lock. */
    if (thread->start_function)
        enqueue_mark(thread->start_function, true, work);
    for (context_t *ctx = thread->context; ctx; ctx = ctx->previous) {
        enqueue_mark(ctx->data, true, work);
        if (ctx->call_receiver)
            enqueue_mark(ctx->call_receiver, true, work);
        if (ctx->arguments)
            enqueue_mark(ctx->arguments, true, work);
        if (ctx->module)
            visit_function_access_gc_edges(&ctx->access, enqueue_mark, work);
        if (ctx->pending_value)
            enqueue_mark(ctx->pending_value, true, work);
    }
    if (thread->exception.value)
        enqueue_mark(thread->exception.value, true, work);
    for (size_t index = 0; index < thread->data_stack->size; index++) {
        object_t *obj = thread->data_stack->objects[index];
        enqueue_mark(obj, true, work);
    }
}

/** @brief Marks all reachable objects in the process. */
static void mark_reachable_objects(process_t *proc) {
    mark_worklist_t work = {.process = proc, .pending = NULL};
    visit_module_file_gc_roots(proc, enqueue_mark, &work);
    if (proc->launch_directory)
        enqueue_mark(proc->launch_directory, true, &work);
    for (size_t index = 0; index < proc->string_cache_size; index++) {
        if (proc->string_cache[index])
            enqueue_mark(proc->string_cache[index], true, &work);
    }
    for (size_t index = 0; index < proc->gc_roots_used; index++) {
        enqueue_mark(proc->gc_roots[index].object, true, &work);
    }
    thread_t *thread = proc->main_thread;
    if (thread) {
        do {
            mark_thread_roots(thread, &work);
            thread = thread->next;
        } while (thread != proc->main_thread);
    }
    while (work.pending) {
        object_t *obj = work.pending;
        object_t **link = obj->vtbl->gc_link(obj);
        work.pending = *link;
        *link = NULL;
        obj->vtbl->visit_gc_edges(obj, enqueue_mark, &work);
    }
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
    /* A reentrant request cannot inspect a partly finalized object graph or reuse its links.
       An ordinary collection can only start at a stable boundary. */
    if (proc->draining_releases || proc->destroying || proc->gc_collecting)
        return;
    proc->gc_collecting = true;
    mark_reachable_objects(proc);
    /* Correct counts before sweep can free any edge target or clear its mark.
       Reconciliation must not run ordinary last-reference destruction. */
    for (object_t *obj = proc->objects.head; obj; obj = obj->next)
        reconcile_swept_edges(obj);
    sweep_unreachable_objects(proc);
    proc->gc_collecting = false;
}

gc_root_handle_t register_gc_root(process_t *proc, object_t *obj) {
    if (!proc || !obj || (obj->process && obj->process != proc) || proc->destroying
        || proc->draining_releases || proc->gc_collecting)
        return (gc_root_handle_t){0};
    /* Never allow a stale token to alias a slot reused after serial wrap. */
    if (proc->gc_root_serial == UINT64_MAX)
        abort();
    /* Validate the strong count before changing any slot or free-list bookkeeping. */
    INCREF(obj);
    size_t index;
    if (proc->gc_roots_free) {
        index = proc->gc_roots_free - 1;
        proc->gc_roots_free = proc->gc_roots[index].next_free;
    } else {
        if (proc->gc_roots_used == proc->gc_roots_capacity) {
            size_t capacity =
                proc->gc_roots_capacity ? allocation_size_multiply(proc->gc_roots_capacity, 2) : 8;
            gc_root_slot_t *slots = ALLOC(allocation_size_multiply(capacity, sizeof(*slots)));
            if (proc->gc_roots_used)
                memcpy(slots, proc->gc_roots, proc->gc_roots_used * sizeof(*slots));
            FREE(proc->gc_roots);
            proc->gc_roots = slots;
            proc->gc_roots_capacity = capacity;
        }
        index = proc->gc_roots_used++;
    }
    uint64_t serial = ++proc->gc_root_serial;
    proc->gc_roots[index] = (gc_root_slot_t){obj, serial, 0};
    ++proc->gc_roots_size;
    return (gc_root_handle_t){proc->id, serial, index};
}

/** @brief Invalidate first: release callbacks cannot remove the same slot twice. */
static void drop_root_slot(process_t *proc, size_t index) {
    object_t *obj = proc->gc_roots[index].object;
    proc->gc_roots[index] = (gc_root_slot_t){.next_free = proc->gc_roots_free};
    proc->gc_roots_free = index + 1;
    --proc->gc_roots_size;
    DECREF(obj);
}

bool unregister_gc_root(process_t *proc, gc_root_handle_t handle) {
    if (!proc || proc->gc_collecting || !handle.serial || proc->id != handle.process_id
        || handle.index >= proc->gc_roots_used)
        return false;
    gc_root_slot_t *slot = &proc->gc_roots[handle.index];
    if (!slot->object || slot->serial != handle.serial)
        return false;
    drop_root_slot(proc, handle.index);
    return true;
}

void clear_gc_roots(process_t *proc) {
    assert(proc->destroying);
    for (size_t index = 0; index < proc->gc_roots_used; ++index)
        if (proc->gc_roots[index].object)
            drop_root_slot(proc, index);
    assert(!proc->gc_roots_size);
    FREE(proc->gc_roots);
    proc->gc_roots = NULL;
    proc->gc_roots_used = proc->gc_roots_capacity = proc->gc_roots_free = 0;
}
