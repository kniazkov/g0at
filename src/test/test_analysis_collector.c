/**
 * @file test_analysis_collector.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Collector storage, queries, and text output.
 */
#include <stdio.h>
#include <string.h>
#include "test_analysis.h"
#include "test_macro.h"
#include "analysis/collector.h"
#include "analysis/lattice.h"
#include "graph/declarations.h"
#include "lib/allocate.h"

bool test_analysis_collector() {
    arena_t *arena = create_arena(1);
    analysis_collector_t *collector = create_analysis_collector(arena);
    ASSERT(!collector->head && !collector->tail && collector->count == 0);
    ASSERT(!find_analysis_event(collector, NULL, NULL));
    ASSERT(!find_last_analysis_event(collector, NULL));
    ASSERT(!add_analysis_event(NULL, ANALYSIS_VALUE_WRITE, NULL, NULL, NULL));
    ASSERT(!find_analysis_event(NULL, NULL, NULL));
    ASSERT(!find_last_analysis_event(NULL, NULL));

    full_position_t position = { .file_name = "test.goat", .row = 7, .column = 3 };
    position_range_t range = { .begin = &position };
    node_t parent = { .position = &range };
    node_t node = { .parent = &parent };
    declarator_t first = { .name = { L"x", 1 } };
    declarator_t second = { .name = { L"x", 1 } };
    const lattice_element_t *one = make_integer_constant_element(arena, 1);
    const analysis_event_t *a = add_analysis_event(collector, ANALYSIS_VALUE_WRITE,
        &node, &first, one);
    const analysis_event_t *b = add_analysis_event(collector, ANALYSIS_VALUE_WRITE,
        &node, &second, make_top_element());
    const analysis_event_t *c = add_analysis_event(collector, ANALYSIS_DECLARATION_SUMMARY,
        &node, &first, make_integer_element());
    position.row = 99;
    node.parent = NULL;
    ASSERT(a->row == 7 && a->column == 3 && a->value == one);
    ASSERT(a->next == b && b->next == c && !c->next);
    ASSERT(collector->head == a && collector->tail == c && collector->count == 3);

    char filename[] = "test.goat";
    analysis_event_query_t query = { .kind = ANALYSIS_VALUE_WRITE, .node = &node,
        .declarator = &first, .file_name = filename, .row = 7, .column = 3 };
    ASSERT(find_analysis_event(collector, NULL, &query) == a);
    ASSERT(!find_analysis_event(collector, a, &query));
    query.kind = ANALYSIS_EVENT_ANY;
    ASSERT(find_analysis_event(collector, a, &query) == c);
    ASSERT(find_last_analysis_event(collector, &query) == c);
    query.declarator = &second;
    ASSERT(find_last_analysis_event(collector, &query) == b);
    query.column = 4;
    ASSERT(!find_analysis_event(collector, NULL, &query));
    query.column = 3;
    query.row = 8;
    ASSERT(!find_analysis_event(collector, NULL, &query));
    query.row = 7;
    query.file_name = "other.goat";
    ASSERT(!find_analysis_event(collector, NULL, &query));
    query.file_name = NULL;
    query.node = &parent;
    ASSERT(!find_analysis_event(collector, NULL, &query));

    for (size_t i = 0; i < 1000; i++) {
        add_analysis_event(collector, ANALYSIS_VALUE_WRITE, NULL, &first, one);
    }
    ASSERT(a == collector->head && a->value == one && a->next == b);
    size_t count = 0;
    for (const analysis_event_t *event = collector->head; event; event = event->next) {
        ASSERT(event->sequence == ++count);
    }
    ASSERT(count == 1003 && collector->tail->sequence == count && !collector->tail->next);
    destroy_arena(arena);
    return true;
}

bool test_analysis_collector_text() {
    arena_t *arena = create_arena(1);
    analysis_collector_t *collector = create_analysis_collector(arena);
    string_value_t text = analysis_collector_to_text(collector);
    ASSERT(text.data && text.length == 0);
    FREE_STRING(text);
    text = analysis_collector_to_text(NULL);
    ASSERT(text.data && text.length == 0);
    FREE_STRING(text);
    full_position_t position = { .file_name = "тест%.goat", .row = 2, .column = 4 };
    position_range_t range = { .begin = &position };
    node_t node = { .position = &range };
    declarator_t decl = { .name = { L"число", 5 } };
    add_analysis_event(collector, ANALYSIS_VALUE_WRITE, &node, &decl,
        make_integer_constant_element(arena, 1));
    add_analysis_event(collector, ANALYSIS_STATE_JOIN, &node, &decl,
        make_integer_range_element(arena, 1, 3));
    add_analysis_event(collector, ANALYSIS_DECLARATION_SUMMARY, NULL, &decl,
        make_top_element());
    text = analysis_collector_to_text(collector);
    ASSERT(wcscmp(text.data,
        L"#1 тест%.goat, 2.4: write число = 1\n"
        L"#2 тест%.goat, 2.4: join число = [1..3]\n"
        L"#3 <unknown>, 0.0: summary число = ⊤\n") == 0);
    ASSERT(text.length == wcslen(text.data));
    FREE_STRING(text);
    destroy_arena(arena);
    return true;
}

