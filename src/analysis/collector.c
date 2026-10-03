/**
 * @file collector.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Analysis event storage, queries, and text rendering.
 */
#include "collector.h"

#include "function_call_graph.h"
#include "function_summary.h"
#include "graph/declarations.h"
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

static analysis_event_t *append_event(analysis_collector_t *collector,
                                      analysis_event_kind_t kind,
                                      const node_t *node,
                                      const declarator_t *declarator,
                                      const lattice_element_t *value) {
    analysis_event_t *event = alloc_zeroed_from_arena(collector->arena, sizeof(*event));
    event->sequence = ++collector->count;
    event->kind = kind;
    event->node = node;
    event->declarator = declarator;
    event->value = value;
    if (kind == ANALYSIS_NODE_FLAGS)
        event->flags = node->flags;
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

const analysis_event_t *add_analysis_event(analysis_collector_t *collector,
                                           analysis_event_kind_t kind,
                                           const node_t *node,
                                           const declarator_t *declarator,
                                           const lattice_element_t *value) {
    if (!collector)
        return NULL;
    assert(kind >= ANALYSIS_VALUE_WRITE && kind <= ANALYSIS_NODE_FLAGS);
    assert(kind == ANALYSIS_UNREACHABLE || kind == ANALYSIS_NODE_FLAGS
               ? node && !declarator && !value
               : declarator && value);
    return append_event(collector, kind, node, declarator, value);
}

const analysis_event_t *add_function_summary_event(analysis_collector_t *collector,
                                                   const function_summary_t *summary) {
    if (!collector)
        return NULL;
    analysis_event_t *event =
        append_event(collector, ANALYSIS_FUNCTION_SUMMARY, summary->function, NULL, NULL);
    event->function_summary = snapshot_function_summary(collector->arena, summary);
    return event;
}

void add_call_graph_events(analysis_collector_t *collector, const function_call_graph_t *graph) {
    if (!collector)
        return;
    for (const function_call_graph_node_t *node = graph->head; node; node = node->next) {
        const function_summary_t *snapshot =
            snapshot_function_summary(collector->arena, node->summary);
        analysis_event_t *group =
            append_event(collector, ANALYSIS_CALL_GROUP, node->summary->function, NULL, NULL);
        group->function_summary = snapshot;
        group->caller_id = node->id;
        group->component = node->component;
        group->component_size = node->component_size;
        group->recursive = node->recursive;
        group->complete = node->complete && !graph->truncated;
        for (const function_call_edge_t *edge = node->edges; edge; edge = edge->next) {
            analysis_event_t *event =
                append_event(collector, ANALYSIS_CALL_EDGE, edge->site, NULL, NULL);
            event->function_summary = snapshot;
            event->caller_id = node->id;
            event->limited = edge->kind == CALL_TARGET_LIMIT;
            if (edge->target) {
                event->callee_id = edge->target->id;
                event->callee_summary =
                    snapshot_function_summary(collector->arena, edge->target->summary);
            }
        }
    }
}

/** @brief Formats graph-local identities together with their type signatures. */
static string_value_t graph_event_to_string(const analysis_event_t *event) {
    string_value_t caller = function_signature_to_string(event->function_summary);
    string_value_t text;
    if (event->kind == ANALYSIS_CALL_GROUP) {
        text = format_string(L"f%zu %s = g%zu %s size=%zu %s",
                             event->caller_id,
                             caller.data,
                             event->component,
                             event->recursive ? L"recursive" : L"acyclic",
                             event->component_size,
                             event->complete ? L"complete" : L"partial");
    } else if (event->callee_summary) {
        string_value_t target = function_signature_to_string(event->callee_summary);
        text = format_string(L"f%zu %s -> f%zu %s",
                             event->caller_id,
                             caller.data,
                             event->callee_id,
                             target.data);
        FREE_STRING(target);
    } else {
        text = format_string(L"f%zu %s -> %s",
                             event->caller_id,
                             caller.data,
                             event->limited ? L"limit" : L"unknown");
    }
    FREE_STRING(caller);
    return text;
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
                              : event->kind == ANALYSIS_CALL_EDGE        ? L"call-edge"
                              : event->kind == ANALYSIS_CALL_GROUP       ? L"call-group"
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
        if (event->kind == ANALYSIS_CALL_EDGE || event->kind == ANALYSIS_CALL_GROUP) {
            string_value_t graph = graph_event_to_string(event);
            append_string_value(&builder, graph);
            FREE_STRING(graph);
        } else if (event->kind == ANALYSIS_FUNCTION_SUMMARY) {
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
