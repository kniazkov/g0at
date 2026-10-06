/** @file test_c_module_output.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Stable source, omitted dependency closure and output filenames.
 */
#include "test_c_module_output.h"

#include "analysis/analysis.h"
#include "analysis_test_support.h"
#include "cli/c_output.h"
#include "codegen/c_module_output.h"
#include "graph/replacement.h"
#include "lib/allocate.h"
#include "test_macro.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

static node_t *analyzed(arena_t *arena, const wchar_t *source) {
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root =
        parse_analysis_test_program(&memory, (string_value_t){source, wcslen(source), false});
    options_t *options = create_options();
    bool success = root && !analyze(root, &memory, options, NULL);
    destroy_options(options);
    return success ? root : NULL;
}

static bool reject_definition(const node_t *node,
                              c_generation_context_t *context,
                              source_builder_t *builder,
                              size_t indent) {
    add_static_source(builder, 0, L"/* partial definition must be discarded */");
    return fail_c_generation(context, node, C_GENERATION_UNSUPPORTED);
}

bool test_c_module_output(void) {
    arena_t *arena = create_arena(32);
    node_t *root = analyzed(arena, L"const f=func(n){if(n<1)return n;return f(n-1);};f(0);f(0.5);");
    ASSERT(root);
    c_module_t *module = create_c_module(arena, root);
    ASSERT(module->count == 2 && module->available_count == 2);
    c_module_output_t first = generate_c_module(arena, module);
    ASSERT(first.generated_count == 2 && !first.omitted_count && !first.failures);
    ASSERT(wcsstr(first.source.data, L"int64_t g_f1_i_(int64_t);"));
    ASSERT(wcsstr(first.source.data, L"double g_f1_r_(double);"));
    ASSERT(wcsstr(first.source.data, L"/* Goat function: f(n)"));
    ASSERT(wcsstr(first.source.data, L"\n\n/* Goat function:"));
    ASSERT(wcsstr(first.source.data, L"\n\n/* Native ABI adapter for g_f1_i_. */"));
    ASSERT(wcsstr(first.source.data, L"goat_native_query_v1(uint32_t version)"));
    ASSERT(!wcsstr(first.source.data, L"goat_guard") && !wcsstr(first.source.data, L"goat_t0"));
    const wchar_t *header = wcsstr(first.source.data, L"#include <stdint.h>");
    ASSERT(header && !wcsstr(header + 1, L"#include <stdint.h>"));
    const wchar_t *helper = wcsstr(first.source.data, L"static inline int64_t g_i64_sub");
    ASSERT(helper && !wcsstr(helper + 1, L"static inline int64_t g_i64_sub"));
    function_summary_set_t *set = get_function_summaries(module->head->summary->function);
    function_summary_t *a = set->head, *b = a->next;
    ASSERT(b && !b->next);
    set->head = b;
    b->next = a;
    a->next = NULL;
    set->tail = a;
    ((node_t *)a->function)->id = 9999;
    c_module_output_t second = generate_c_module(arena, create_c_module(arena, root));
    ASSERT(!wcscmp(first.source.data, second.source.data));
    FREE_STRING(first.source);
    FREE_STRING(second.source);
    root = analyzed(arena,
                    L"const a=func(n){if(n<1)return 0;return b(n-1);};"
                    L"const b=func(n){if(n<1)return 0;return a(n-1);};"
                    L"const caller=func(n){return b(n);};const independent=func(n){return n+1;};"
                    L"a(0);b(0);caller(0);independent(1);");
    ASSERT(root);
    module = create_c_module(arena, root);
    ASSERT(module->count == 4 && module->available_count == 4);
    /* Simulate a node whose analysis is supported but whose emitter is not implemented. */
    node_t *function = (node_t *)module->head->summary->function;
    node_vtbl_t *original = function->vtbl;
    node_vtbl_t unsupported = *original;
    unsupported.generate_indented_c_code = reject_definition;
    function->vtbl = &unsupported;
    first = generate_c_module(arena, module);
    ASSERT(first.generated_count == 1 && first.omitted_count == 3);
    ASSERT(!wcsstr(first.source.data, L"partial definition"));
    ASSERT(!wcsstr(first.source.data, L"g_adapter_0(")
           && !wcsstr(first.source.data, L"g_adapter_1(")
           && !wcsstr(first.source.data, L"g_adapter_2(")
           && wcsstr(first.source.data, L"g_adapter_3("));
    ASSERT(!wcsstr(first.source.data, L"g_f1_") && !wcsstr(first.source.data, L"g_f2_")
           && !wcsstr(first.source.data, L"g_f3_") && wcsstr(first.source.data, L"g_f4_i_"));
    size_t failures = 0, propagated = 0;
    for (const c_module_failure_t *failure = first.failures; failure; failure = failure->next) {
        ASSERT(failure->status != C_GENERATION_OK && failure->failed_node);
        propagated += failure->dependency != NULL;
        failures++;
    }
    ASSERT(failures == 3 && propagated == 2 && module->available_count == 4);
    for (const c_module_function_t *entry = module->head; entry; entry = entry->next)
        ASSERT(entry->available && entry->summary->c_support == FUNCTION_C_SUPPORTED);
    second = generate_c_module(arena, module);
    ASSERT(!wcscmp(first.source.data, second.source.data));
    FREE_STRING(first.source);
    FREE_STRING(second.source);
    function->vtbl = original;
    first = generate_c_module(arena, NULL);
    ASSERT(!first.generated_count && !first.omitted_count && first.source.data);
    ASSERT(!wcsstr(first.source.data, L"static inline"));
    FREE_STRING(first.source);
    destroy_arena(arena);
    return true;
}

bool test_c_output_options(void) {
    char *args[] = {"goat", "--print-c", "--save-c", "dir/example.goat"};
    options_t *options = parse_options(4, args);
    ASSERT(options && options->print_c && options->save_c);
    path_t *path = c_output_path(options->input_file);
    ASSERT(path && !strcmp(path->file_name, "example.c"));
    free_path(path);
    destroy_options(options);
    const char *inputs[] = {"folder.with.dot/example", "folder/example.part.goat", ".hidden"};
    const char *outputs[] = {"example.c", "example.part.c", ".hidden.c"};
    for (size_t i = 0; i < 3; i++) {
        path_t *input = create_path(inputs[i]);
        path = c_output_path(input);
        ASSERT(path && !strcmp(path->file_name, outputs[i]));
        free_path(path);
        free_path(input);
    }
    path = c_output_path(NULL);
    ASSERT(!strcmp(path->file_name, "generated.c"));
    free_path(path);
    path_t *input = create_path("source.C");
    ASSERT(!c_output_path(input));
    free_path(input);
    return true;
}
