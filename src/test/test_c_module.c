/** @file test_c_module.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Determinism, aliases, recursion and conservative dependency rejection.
 */
#include "test_c_module.h"

#include "analysis/analysis.h"
#include "analysis/c_body.h"
#include "analysis_test_support.h"
#include "cli/options.h"
#include "codegen/c_module.h"
#include "graph/replacement.h"
#include "test_macro.h"

#include <stdio.h>
#include <wchar.h>

static node_t *analyzed(arena_t *arena, string_value_t text) {
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(&memory, text);
    if (!root)
        return NULL;
    options_t *options = create_options();
    compilation_error_t *error = analyze(root, &memory, options, NULL);
    destroy_options(options);
    return error ? NULL : root;
}

bool test_c_module_identity(void) {
    arena_t *arena = create_arena(32);
    node_t *root =
        analyzed(arena,
                 STATIC_STRING(L"const f=func(n){return n+1;}; const alias=f;"
                               L"const g=func(n){return f(n);}; const z=func(){return 0;};"
                               L"const uncalled=func(n){return n;};"
                               L"alias(1.0); f(1); f(2); g(1); g(1.0); z();"));
    ASSERT(root);
    c_module_t *module = create_c_module(arena, root);
    ASSERT(module->count == 5 && module->available_count == 5);
    size_t id = 0;
    for (c_module_function_t *entry = module->head; entry; entry = entry->next) {
        ASSERT(entry->id == id++ && entry->available);
        ASSERT(c_module_find(module, entry->summary) == entry);
        if (entry->function_id == 2) {
            ASSERT(entry->dependencies && !entry->dependencies->next);
            const function_summary_t *target = entry->dependencies->target->summary;
            ASSERT(target->function != entry->summary->function);
            ASSERT(target->parameter_types[0] == entry->summary->parameter_types[0]);
        }
        const function_summary_t *copy = snapshot_function_summary(arena, entry->summary);
        ASSERT(c_module_find(module, copy) == entry);
        for (c_module_function_t *other = entry->next; other; other = other->next)
            ASSERT(wcscmp(entry->name.data, other->name.data));
        /* Node indexes are deliberately made identical: names must not depend on them. */
        ((node_t *)entry->summary->function)->id = 7;
    }
    function_summary_set_t *set = get_function_summaries(module->head->summary->function);
    ASSERT(set->head && set->head->next && !set->head->next->next);
    function_summary_t *first = set->head, *second = first->next;
    set->head = second;
    second->next = first;
    first->next = NULL;
    set->tail = first;
    c_module_t *again = create_c_module(arena, root);
    ASSERT(again->count == module->count);
    for (c_module_function_t *entry = module->head; entry; entry = entry->next) {
        const c_module_function_t *other = c_module_find(again, entry->summary);
        ASSERT(other && entry->id == other->id && entry->function_id == other->function_id);
        ASSERT(!wcscmp(entry->name.data, other->name.data));
    }
    ASSERT(!create_c_module(arena, NULL)->count);
    ASSERT(!c_module_find(NULL, module->head->summary));
    destroy_arena(arena);
    return true;
}

bool test_c_module_dependencies(void) {
    arena_t *arena = create_arena(32);
    node_t *root =
        analyzed(arena,
                 STATIC_STRING(L"const even=func(n){if(n<1)return 1;return odd(n-1);};"
                               L"const odd=func(n){if(n<1)return 0;return even(n-1);};"
                               L"const alias=even;"
                               L"const caller=func(n){return alias(n)+alias(n);};"
                               L"const alone=func(n){return n+1;}; alone(1); caller(2);"));
    ASSERT(root);
    c_module_t *module = create_c_module(arena, root);
    ASSERT(module->count == 4 && module->available_count == 4);
    c_module_function_t *even = module->head, *odd = even->next;
    c_module_function_t *caller = odd->next, *alone = caller->next;
    ASSERT(even->dependencies && even->dependencies->target == odd);
    ASSERT(odd->dependencies && odd->dependencies->target == even);
    ASSERT(caller->dependencies && caller->dependencies->next);
    ASSERT(caller->dependencies->target == even && caller->dependencies->next->target == even);
    ASSERT(caller->callees && !caller->callees->next);
    ASSERT(caller->callees->summary == even->summary);
    ASSERT(!wcscmp(caller->callees->name.data, even->name.data));
    ASSERT(!alone->dependencies);
    function_summary_t *removed = (function_summary_t *)odd->summary;
    removed->c_support = FUNCTION_C_UNKNOWN;
    c_module_t *blocked = create_c_module(arena, root);
    ASSERT(blocked->count == 3 && blocked->available_count == 1);
    ASSERT(!c_module_find(blocked, removed));
    ASSERT(!c_module_find(blocked, even->summary)->available);
    ASSERT(!c_module_find(blocked, caller->summary)->available);
    ASSERT(c_module_find(blocked, alone->summary)->available);
    removed->c_support = FUNCTION_C_SUPPORTED;
    ASSERT(create_c_module(arena, root)->available_count == 4);
    /* A second target at one generic call site cannot select one fixed C callee. */
    function_summary_t *ambiguous = (function_summary_t *)caller->summary;
    c_call_t extra = {.next = ambiguous->c_calls,
                      .site = ambiguous->c_calls->site,
                      .target = alone->summary};
    ambiguous->c_calls = &extra;
    blocked = create_c_module(arena, root);
    ASSERT(blocked->available_count == 3 && !c_module_find(blocked, ambiguous)->available);
    ambiguous->c_calls = extra.next;
    destroy_arena(arena);
    return true;
}

bool test_c_module_call_lifetime(void) {
    arena_t *arena = create_arena(32);
    node_t *root =
        analyzed(arena, STATIC_STRING(L"const f=func(n){if(n<1)return 0;return f(n-1);};f(2);"));
    ASSERT(root);
    c_module_t *module = create_c_module(arena, root);
    ASSERT(module->count == 1 && module->available_count == 1);
    c_module_function_t *entry = module->head;
    ASSERT(entry->dependencies && entry->dependencies->target == entry);
    function_summary_t *summary = (function_summary_t *)entry->summary;
    ASSERT(summary->c_calls && summary->c_calls->target == summary);
    const function_summary_t *snapshot = snapshot_function_summary(arena, summary);
    ASSERT(snapshot->c_calls && snapshot->c_calls != summary->c_calls);
    ASSERT(snapshot->c_calls->target == summary);
    const node_t *site = snapshot->c_calls->site;
    summary->c_calls->site = NULL;
    ASSERT(snapshot->c_calls->site == site);
    reset_function_summary(summary);
    ASSERT(!summary->c_calls && snapshot->c_calls->site == site);
    ASSERT(!create_c_module(arena, root)->count);
    /* An exhausted proof pass must not retain previously published call records. */
    parser_memory_t memory = {arena, arena, arena, arena};
    options_t *options = create_options();
    ASSERT(!analyze(root, &memory, options, NULL));
    function_call_graph_t *graph = build_function_call_graph(root, arena, 1024);
    analyze_function_c_bodies(graph, 0);
    for (function_call_graph_node_t *node = graph->head; node; node = node->next)
        ASSERT(!node->summary->c_calls && node->summary->c_support != FUNCTION_C_SUPPORTED);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}
