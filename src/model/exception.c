/** @file exception.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Shared immutable exception origins independent of mutable thrown values.
 */
#include "exception.h"

#include "context.h"
#include "lib/allocate.h"
#include "module_files.h"

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
                                      ctx == context ? instruction : ctx->instruction,
                                      false};
    }
    return trace;
}

const exception_trace_t *capture_native_exception_trace(context_t *caller,
                                                        module_t *module,
                                                        uint32_t file,
                                                        instr_index_t body) {
    context_t origin = {.module = module, .access = {.file_id = file}, .previous = caller};
    exception_trace_t *trace = (exception_trace_t *)capture_exception_trace(&origin, body);
    trace->frames[0].native_entry = true;
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

/** Escape labels without adding quotation marks around ordinary logical paths. */
static void append_trace_label(string_builder_t *builder, string_view_t label) {
    string_value_t escaped =
        string_to_string_notation(U"", (string_value_t){label.data, label.length, false});
    for (size_t i = 1; i + 1 < escaped.length; ++i) {
        gchar_t scalar = escaped.data[i];
        if (scalar < 32 || (scalar >= 127 && scalar <= 159)) {
            static const gchar_t hex[] = U"0123456789abcdef";
            append_static_string(builder, U"\\u00");
            append_char(builder, hex[(scalar >> 4) & 15]);
            append_char(builder, hex[scalar & 15]);
        } else {
            append_char(builder, scalar);
        }
    }
    FREE_STRING(escaped);
}

string_value_t format_exception_trace(process_t *process, const exception_trace_t *trace) {
    string_builder_t builder;
    init_string_builder(&builder, 0);
    for (size_t i = 0; i < exception_trace_size(trace); ++i) {
        const exception_trace_frame_t *frame = exception_trace_frame(trace, i);
        /* The process bootstrap context has never executed a source instruction. */
        if (frame->instruction == BAD_INSTR_INDEX)
            continue;
        module_files_t *files = find_module_files(process, frame->module);
        const bytecode_t *code = module_files_bytecode(files);
        module_file_t *file = module_files_get(files, frame->file_id);
        append_static_string(&builder, U"  at ");
        if (!file || !code) {
            string_value_t unknown = format_string(U"<unknown module>: file #%u, instruction %u\n",
                                                   frame->file_id,
                                                   frame->instruction);
            append_string_value(&builder, unknown);
            FREE_STRING(unknown);
            continue;
        }
        append_trace_label(&builder, module_files_name(files));
        append_char(&builder, '@');
        append_trace_label(&builder, module_files_version(files));
        append_static_string(&builder, U": ");
        append_trace_label(&builder, module_file_name(file));
        if (module_file_external(file)) {
            string_value_t suffix = format_string(U" (external file #%u)", frame->file_id);
            append_string_value(&builder, suffix);
            FREE_STRING(suffix);
        }
        /* Checked location runs partition the image; do not guess a nearest line. */
        const bytecode_location_t *location = NULL;
        size_t low = 0, high = code->location_count;
        while (low < high) {
            size_t middle = low + (high - low) / 2;
            const bytecode_location_t *candidate = code->locations + middle;
            if (frame->instruction < candidate->first)
                high = middle;
            else if (frame->instruction - candidate->first >= candidate->count)
                low = middle + 1;
            else {
                if (candidate->file == frame->file_id)
                    location = candidate;
                break;
            }
        }
        if (!location) {
            string_value_t unknown =
                format_string(U": instruction %u (unknown location)\n", frame->instruction);
            append_string_value(&builder, unknown);
            FREE_STRING(unknown);
            continue;
        }
        string_value_t coordinates = format_string(U":%u:%u, ", location->line, location->column);
        append_string_value(&builder, coordinates);
        FREE_STRING(coordinates);
        const bytecode_function_t *function = NULL;
        low = 0;
        high = code->function_count;
        while (low < high) {
            size_t middle = low + (high - low) / 2;
            const bytecode_function_t *candidate = code->functions + middle;
            if (candidate->id < location->function)
                low = middle + 1;
            else if (candidate->id > location->function)
                high = middle;
            else {
                function = candidate;
                break;
            }
        }
        if (function && function->name != UINT32_MAX) {
            const data_descriptor_t *descriptor = code->data_descriptors + function->name;
            append_trace_label(&builder,
                               (string_view_t){(const gchar_t *)(code->data + descriptor->offset),
                                               descriptor->count});
        } else {
            append_string(&builder, location->function ? U"<anonymous>" : U"<top-level>");
        }
        if (frame->native_entry)
            append_static_string(&builder, U" [native entry]");
        append_char(&builder, '\n');
    }
    return builder.data ? (string_value_t){builder.data, builder.length, true} : EMPTY_STRING_VALUE;
}
