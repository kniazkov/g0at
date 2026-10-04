/** @file c_module_output.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Transactional definitions, dependency closure and deterministic module assembly.
 */
#include "c_module_output.h"

#include "c_adapter.h"
#include "c_call.h"
#include "c_lowering.h"
#include "lib/allocate.h"

c_module_output_t generate_c_module(arena_t *arena, const c_module_t *module) {
    c_module_output_t output = {0};
    size_t count = module ? module->count : 0;
    c_generation_result_t *results = count ? CALLOC(count * sizeof(*results)) : NULL;
    const c_module_function_t **blocked = count ? CALLOC(count * sizeof(*blocked)) : NULL;
    for (const c_module_function_t *entry = module ? module->head : NULL; entry;
         entry = entry->next) {
        results[entry->id] =
            entry->available ? c_generate_definition(entry->summary, entry->name, entry->callees)
                             : (c_generation_result_t){.status = C_GENERATION_NOT_PROVEN};
    }
    bool changed;
    do {
        changed = false;
        for (const c_module_function_t *entry = module ? module->head : NULL; entry;
             entry = entry->next) {
            c_generation_result_t *result = &results[entry->id];
            if (result->status != C_GENERATION_OK)
                continue;
            for (const c_module_dependency_t *edge = entry->dependencies; edge; edge = edge->next) {
                if (!edge->target || results[edge->target->id].status != C_GENERATION_OK) {
                    blocked[entry->id] = edge->target;
                    result->status = C_GENERATION_UNSUPPORTED;
                    result->failed_node = edge->site;
                    changed = true;
                    break;
                }
            }
        }
    } while (changed);
    source_builder_t *builder = create_source_builder();
    add_static_source(builder,
                      0,
                      L"/* Generated Goat numeric specializations; no program entry point. */");
    unsigned helpers = 0;
    for (size_t i = 0; i < count; i++)
        if (results[i].status == C_GENERATION_OK)
            helpers |= results[i].helper_flags;
    c_emit_native_abi(builder);
    c_emit_headers(builder, helpers);
    if (count)
        c_emit_native_guard(builder);
    c_module_failure_t **tail = &output.failures;
    for (const c_module_function_t *entry = module ? module->head : NULL; entry;
         entry = entry->next) {
        c_generation_result_t *result = &results[entry->id];
        if (result->status == C_GENERATION_OK) {
            c_emit_prototype(entry->summary, entry->name, builder);
            output.generated_count++;
        } else {
            c_module_failure_t *failure = alloc_zeroed_from_arena(arena, sizeof(*failure));
            *failure = (c_module_failure_t){.entry = entry,
                                            .dependency = blocked[entry->id],
                                            .status = result->status,
                                            .failed_node = result->failed_node};
            *tail = failure;
            tail = &failure->next;
            output.omitted_count++;
        }
    }
    add_source(builder,
               0,
               L"/* Specializations: %zu emitted, %zu omitted. */",
               output.generated_count,
               output.omitted_count);
    for (const c_module_function_t *entry = module ? module->head : NULL; entry;
         entry = entry->next) {
        c_generation_result_t *result = &results[entry->id];
        if (result->status == C_GENERATION_OK) {
            add_formatted_source(builder, 0, result->source);
            c_emit_adapter(builder, entry);
        } else {
            FREE_STRING(result->source);
        }
    }
    c_emit_native_module(builder, module, results, output.generated_count);
    output.source = build_source(builder);
    destroy_source_builder(builder);
    FREE(results);
    FREE(blocked);
    return output;
}
