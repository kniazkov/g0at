/** @file c_module.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Stable C names and conservative closure over generic call proofs.
 */
#include "c_module.h"

#include "graph/replacement.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"

#include <string.h>

static bool eligible(const function_summary_t *summary) {
    if (summary->status != FUNCTION_ANALYZED || summary->c_support != FUNCTION_C_SUPPORTED
        || summary->c_blockers || !function_summary_is_pure(summary))
        return false;
    c_generation_context_t context = {.summary = summary};
    c_value_type_t type = c_generation_return_type(&context);
    if (type != C_VALUE_INT64 && type != C_VALUE_DOUBLE)
        return false;
    for (size_t i = 0; i < summary->parameter_count; i++) {
        type = c_generation_parameter_type(&context, i);
        if (type != C_VALUE_INT64 && type != C_VALUE_DOUBLE)
            return false;
    }
    return true;
}

/** @brief Lexicographic signature order, independent of observation order or addresses. */
static int compare_signature(const function_summary_t *a, const function_summary_t *b) {
    if (a->parameter_count != b->parameter_count)
        return a->parameter_count < b->parameter_count ? -1 : 1;
    for (size_t i = 0; i < a->parameter_count; i++) {
        lattice_type_t x = a->parameter_types[i]->type, y = b->parameter_types[i]->type;
        if (x != y)
            return x < y ? -1 : 1;
    }
    return 0;
}

const c_module_function_t *c_module_find(const c_module_t *module,
                                         const function_summary_t *summary) {
    if (!module || !summary)
        return NULL;
    for (const c_module_function_t *entry = module->head; entry; entry = entry->next) {
        if (entry->summary->function == summary->function
            && compare_signature(entry->summary, summary) == 0)
            return entry;
    }
    return NULL;
}

static string_view_t
make_name(arena_t *arena, size_t function_id, const function_summary_t *summary) {
    string_builder_t builder;
    init_string_builder(&builder, 32);
    string_value_t prefix = format_string(L"g_f%zu", function_id);
    append_string_value(&builder, prefix);
    FREE_STRING(prefix);
    for (size_t i = 0; i < summary->parameter_count; i++)
        append_string(&builder,
                      summary->parameter_types[i]->type == LATTICE_INTEGER ? L"_i" : L"_r");
    string_value_t text = append_char(&builder, L'_');
    wchar_t *name = alloc_from_arena(arena, (text.length + 1) * sizeof(*name));
    memcpy(name, text.data, (text.length + 1) * sizeof(*name));
    string_view_t result = {name, text.length};
    FREE_STRING(text);
    return result;
}

static void collect(c_module_t *module, arena_t *arena, const node_t *node, size_t *ordinal) {
    node = replacement_original(node);
    if (node->vtbl->type == NODE_FUNCTION_OBJECT) {
        size_t function_id = ++*ordinal;
        const function_summary_set_t *set = get_function_summaries(node);
        const function_summary_t *previous = NULL;
        for (;;) {
            const function_summary_t *best = NULL;
            for (const function_summary_t *s = set ? set->head : NULL; s; s = s->next) {
                if (eligible(s) && (!previous || compare_signature(s, previous) > 0)
                    && (!best || compare_signature(s, best) < 0))
                    best = s;
            }
            if (!best)
                break;
            c_module_function_t *entry = alloc_zeroed_from_arena(arena, sizeof(*entry));
            entry->id = module->count++;
            entry->function_id = function_id;
            entry->summary = best;
            entry->name = make_name(arena, function_id, best);
            entry->available = true;
            if (module->tail)
                module->tail->next = entry;
            else
                module->head = entry;
            module->tail = entry;
            previous = best;
        }
    }
    for (size_t i = 0; i < get_node_child_count(node); i++)
        collect(module, arena, get_node_child(node, i), ordinal);
}

static void dependencies(c_module_t *module, arena_t *arena, c_module_function_t *entry) {
    c_module_dependency_t **tail = &entry->dependencies;
    c_generation_callee_t *callee_tail = NULL;
    for (const c_call_t *call = entry->summary->c_calls; call; call = call->next) {
        const c_module_function_t *target = c_module_find(module, call->target);
        bool duplicate = false;
        for (const c_module_dependency_t *old = entry->dependencies; old; old = old->next) {
            if (old->site == call->site) {
                duplicate = true;
                if (old->target != target)
                    entry->available = false;
            }
        }
        if (!target)
            entry->available = false;
        if (duplicate)
            continue;
        c_module_dependency_t *edge = alloc_zeroed_from_arena(arena, sizeof(*edge));
        edge->site = call->site;
        edge->target = target;
        *tail = edge;
        tail = &edge->next;
        if (!target)
            continue;
        bool known = false;
        for (const c_generation_callee_t *callee = entry->callees; callee; callee = callee->next)
            known |= callee->summary == target->summary;
        if (!known) {
            c_generation_callee_t *callee = alloc_zeroed_from_arena(arena, sizeof(*callee));
            callee->summary = target->summary;
            callee->name = target->name;
            if (callee_tail)
                callee_tail->next = callee;
            else
                entry->callees = callee;
            callee_tail = callee;
        }
    }
}

c_module_t *create_c_module(arena_t *arena, const node_t *root) {
    c_module_t *module = alloc_zeroed_from_arena(arena, sizeof(*module));
    size_t ordinal = 0;
    if (root)
        collect(module, arena, root, &ordinal);
    for (c_module_function_t *entry = module->head; entry; entry = entry->next)
        dependencies(module, arena, entry);
    bool changed;
    do {
        changed = false;
        for (c_module_function_t *entry = module->head; entry; entry = entry->next) {
            if (!entry->available)
                continue;
            for (const c_module_dependency_t *edge = entry->dependencies; edge; edge = edge->next) {
                if (!edge->target || !edge->target->available) {
                    entry->available = false;
                    changed = true;
                    break;
                }
            }
        }
    } while (changed);
    for (const c_module_function_t *entry = module->head; entry; entry = entry->next)
        module->available_count += entry->available;
    return module;
}
