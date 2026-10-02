/**
 * @file collector.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Analysis event storage, queries, and text rendering.
 */
#include "collector.h"

#include "function_summary.h"
#include "graph/declarations.h"
#include "graph/expression.h"
#include "lattice.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"

#include <assert.h>
#include <string.h>

analysis_collector_t *create_analysis_collector(arena_t *arena) {
    assert(arena);
    analysis_collector_t *collector = alloc_zeroed_from_arena(arena, sizeof(*collector));
    collector->arena = arena;
    return collector;
}

const analysis_event_t *add_analysis_event(analysis_collector_t *collector,
                                           analysis_event_kind_t kind,
                                           const node_t *node,
                                           const declarator_t *declarator,
                                           const lattice_element_t *value) {
    if (!collector) {
        return NULL;
    }
    assert(kind >= ANALYSIS_VALUE_WRITE && kind <= ANALYSIS_FUNCTION_SUMMARY);
    assert(kind == ANALYSIS_UNREACHABLE || kind == ANALYSIS_NODE_FLAGS
                   || kind == ANALYSIS_FUNCTION_SUMMARY
               ? node && !declarator && !value
               : declarator && value);
    analysis_event_t *event = alloc_zeroed_from_arena(collector->arena, sizeof(*event));
    event->sequence = ++collector->count;
    event->kind = kind;
    event->node = node;
    event->declarator = declarator;
    event->value = value;
    if (kind == ANALYSIS_NODE_FLAGS)
        event->flags = node->flags;
    if (kind == ANALYSIS_FUNCTION_SUMMARY)
        event->function_summary =
            snapshot_function_summary(collector->arena, get_function_summary(node));
    const node_t *located = node;
    while (located && (!located->position || !located->position->begin)) {
        located = located->parent;
    }
    if (located) {
        const full_position_t *position = located->position->begin;
        event->file_name = position->file_name;
        event->row = position->row;
        event->column = position->column;
    }
    if (collector->tail) {
        collector->tail->next = event;
    } else {
        collector->head = event;
    }
    collector->tail = event;
    return event;
}

static bool matches(const analysis_event_t *event, const analysis_event_query_t *query) {
    return !query
           || ((!query->kind || event->kind == query->kind)
               && (!query->node || event->node == query->node)
               && (!query->declarator || event->declarator == query->declarator)
               && (!query->file_name
                   || (event->file_name && strcmp(event->file_name, query->file_name) == 0))
               && (!query->row || event->row == query->row)
               && (!query->column || event->column == query->column)
               && (!query->flags_mask
                   || (event->kind == ANALYSIS_NODE_FLAGS
                       && (event->flags & query->flags_mask)
                              == (query->flags & query->flags_mask))));
}

const analysis_event_t *find_analysis_event(const analysis_collector_t *collector,
                                            const analysis_event_t *after,
                                            const analysis_event_query_t *query) {
    if (!collector) {
        return NULL;
    }
    const analysis_event_t *event = after ? after->next : collector->head;
    while (event && !matches(event, query)) {
        event = event->next;
    }
    return event;
}

const analysis_event_t *find_last_analysis_event(const analysis_collector_t *collector,
                                                 const analysis_event_query_t *query) {
    const analysis_event_t *last = NULL;
    for (const analysis_event_t *event = collector ? collector->head : NULL; event;
         event = event->next) {
        if (matches(event, query)) {
            last = event;
        }
    }
    return last;
}

string_value_t analysis_collector_to_text(const analysis_collector_t *collector) {
    string_builder_t builder;
    init_string_builder(&builder, 0);
    string_value_t result = EMPTY_STRING_VALUE;
    for (const analysis_event_t *event = collector ? collector->head : NULL; event;
         event = event->next) {
        const wchar_t *kind = event->kind == ANALYSIS_VALUE_WRITE        ? L"write"
                              : event->kind == ANALYSIS_STATE_JOIN       ? L"join"
                              : event->kind == ANALYSIS_UNREACHABLE      ? L"unreachable"
                              : event->kind == ANALYSIS_NODE_FLAGS       ? L"flags"
                              : event->kind == ANALYSIS_FUNCTION_SUMMARY ? L"function-summary"
                                                                         : L"summary";
        string_value_t filename =
            event->file_name ? decode_utf8(event->file_name) : STATIC_STRING(L"<unknown>");
        string_value_t value = event->value ? lattice_to_string(event->value) : EMPTY_STRING_VALUE;
        string_value_t line = format_string(L"#%zu %s, %zu.%zu: %s ",
                                            event->sequence,
                                            filename.data ? filename.data : L"<unknown>",
                                            event->row,
                                            event->column,
                                            kind);
        append_string_value(&builder, line);
        if (event->kind == ANALYSIS_FUNCTION_SUMMARY) {
            string_value_t summary = function_summary_to_string(event->function_summary);
            append_string_value(&builder, summary);
            FREE_STRING(summary);
        } else if (event->kind == ANALYSIS_NODE_FLAGS) {
            append_string(&builder, event->node->vtbl->type_name);
            append_static_string(&builder, L" = ");
            if (!event->flags)
                append_static_string(&builder, L"none");
            else {
                string_value_t bits = format_string(L"%u", (unsigned)event->flags);
                append_string_value(&builder, bits);
                FREE_STRING(bits);
                if (event->flags & NODE_FLAG_UNREACHABLE)
                    append_static_string(&builder, L" unreachable");
                if (event->flags & NODE_FLAG_PURE)
                    append_static_string(&builder, L" pure");
                if (event->flags & NODE_FLAG_C_COMPATIBLE)
                    append_static_string(&builder, L" c-compatible");
            }
        } else if (event->kind == ANALYSIS_UNREACHABLE) {
            append_string(&builder, event->node->vtbl->type_name);
        } else {
            append_string_view(&builder, event->declarator->name);
            append_static_string(&builder, L" = ");
            append_string_value(&builder, value);
        }
        result = append_char(&builder, L'\n');
        FREE_STRING(line);
        FREE_STRING(value);
        FREE_STRING(filename);
    }
    return result;
}
