/** @file exception.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Shared immutable exception origins independent of mutable thrown values.
 */
#include "exception.h"

#include "context.h"
#include "lib/allocate.h"

#include <stdatomic.h>

struct exception_trace_t {
    atomic_size_t references;
    size_t count;
    exception_trace_frame_t frames[];
};

exception_t retain_exception(exception_t exception) {
    if (exception.value)
        INCREF(exception.value);
    retain_exception_trace(exception.trace);
    return exception;
}

void clear_exception(exception_t *exception) {
    if (!exception)
        return;
    DECREFIF(exception->value);
    release_exception_trace(exception->trace);
    *exception = (exception_t){0};
}

const exception_trace_t *capture_exception_trace(context_t *context, instr_index_t instruction) {
    size_t count = 0;
    context_t *root = get_root_context();
    for (context_t *ctx = context; ctx && ctx != root; ctx = ctx->previous)
        count = allocation_size_add(count, 1);
    exception_trace_t *trace =
        ALLOC(allocation_size_add(sizeof(*trace),
                                  allocation_size_multiply(count, sizeof(*trace->frames))));
    atomic_init(&trace->references, 1);
    trace->count = count;
    size_t index = 0;
    for (context_t *ctx = context; ctx && ctx != root; ctx = ctx->previous) {
        trace->frames[index++] =
            (exception_trace_frame_t){retain_module_identity(ctx->module),
                                      ctx->access.file_id,
                                      ctx == context ? instruction : ctx->instruction};
    }
    return trace;
}

const exception_trace_t *retain_exception_trace(const exception_trace_t *trace) {
    if (trace)
        atomic_fetch_add_explicit(&((exception_trace_t *)trace)->references,
                                  1,
                                  memory_order_relaxed);
    return trace;
}

void release_exception_trace(const exception_trace_t *trace) {
    if (!trace
        || atomic_fetch_sub_explicit(&((exception_trace_t *)trace)->references,
                                     1,
                                     memory_order_acq_rel)
               != 1)
        return;
    for (size_t i = 0; i < trace->count; ++i)
        release_module_identity(trace->frames[i].module);
    FREE((void *)trace);
}

size_t exception_trace_size(const exception_trace_t *trace) {
    return trace ? trace->count : 0;
}

const exception_trace_frame_t *exception_trace_frame(const exception_trace_t *trace, size_t index) {
    return trace && index < trace->count ? trace->frames + index : NULL;
}
