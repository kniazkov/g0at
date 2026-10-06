/** @file test_c_locals.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Local identity, sequencing, scope and numeric storage regressions.
 */
#include "test_c_locals.h"

#include "analysis/analysis.h"
#include "analysis_test_support.h"
#include "cli/options.h"
#include "codegen/c_module.h"
#include "graph/common_methods.h"
#include "graph/replacement.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"
#include "test_macro.h"

#include <stdio.h>
#include <wchar.h>

/** @brief One AST produces different local representations for its numeric signatures. */
static bool append_profiles(source_builder_t *output, source_builder_t *checks) {
    arena_t *arena = create_arena(32);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(
        &memory,
        STATIC_STRING(L"const f=func(n){var x=n;x=x+x;return x;};f(1);f(1.5);"));
    options_t *options = create_options();
    bool success = root && !analyze(root, &memory, options, NULL);
    c_module_t *module = success ? create_c_module(arena, root) : NULL;
    success = module && module->available_count == 2;
    for (const c_module_function_t *entry = success ? module->head : NULL; entry && success;
         entry = entry->next) {
        bool integer = entry->summary->parameter_types[0]->type == LATTICE_INTEGER;
        const wchar_t *name = integer ? L"goat_local_integer" : L"goat_local_real";
        c_generation_result_t result =
            generate_c_function(entry->summary, (string_view_t){name, wcslen(name)}, NULL, NULL);
        success =
            result.status == C_GENERATION_OK
            && wcsstr(result.source.data, integer ? L"int64_t g_l0;" : L"volatile double g_l0;");
        if (success) {
            add_formatted_source(output, 0, result.source);
            add_source(checks,
                       1,
                       L"if (!(%s)) return 106;",
                       integer
                           ? L"goat_local_integer(INT64_MAX)==INT64_MAX && "
                             L"goat_local_integer(INT64_MIN)==INT64_MIN"
                           : L"goat_local_real(1.5)==3.0 && goat_local_real(INFINITY)==INFINITY && "
                             L"isnan(goat_local_real(NAN))");
        } else
            FREE_STRING(result.source);
    }
    destroy_options(options);
    destroy_arena(arena);
    return success;
}

bool append_c_local_tests(source_builder_t *output, source_builder_t *checks) {
    if (!append_profiles(output, checks))
        return false;

    const struct {
        const wchar_t *source, *check;
    } fixtures[] = {
        {L"const f=func(n){var x=n;return x;};f(1);", L"(INT64_MIN)==INT64_MIN"},
        {L"const f=func(n){var x=n;return x;};f(1.0);",
         L"(-0.0)==0 && signbit(goat_local1(-0.0)) && isnan(goat_local1(NAN))"},
        {L"const f=func(n){const x=n+1;return x*2;};f(1);", L"(4)==10"},
        {L"const f=func(n){var x=n;x=x+1;return x;};f(1);", L"(INT64_MAX)==INT64_MAX"},
        {L"const f=func(n){n=n+2;return n;};f(1);", L"(5)==7"},
        {L"const f=func(n){var x=n,y=x+1;return x+y;};f(1);", L"(3)==7"},
        {L"const f=func(n){var x=n;const y=(x=x+1),z=(x=x+2);return y*10+z;};f(1);", L"(3)==46"},
        {L"const f=func(n){var x=n,y=n;x=y=n+3;return x*10+y;};f(1);", L"(2)==55"},
        {L"const f=func(n){var x=n;return (x=1)+(x=2);};f(1);", L"(9)==3"},
        {L"const f=func(n){var x=n;return x+(x=2);};f(1);", L"(9)==11"},
        {L"const f=func(n){var x=n;return (x=2)+x;};f(1);", L"(9)==4"},
        {L"const f=func(n){var x=n;return (x=x+1)*(x=x+2);};f(1);", L"(3)==24"},
        {L"const f=func(n){var x=n;if((x=x+1)<(x=x+2))return x;return 0;};f(1);", L"(3)==6"},
        {L"const f=func(n){var x=1;if(n){x=2;}else{x=3;}return x;};f(1);",
         L"(1)==2 && goat_local13(0)==3"},
        {L"const f=func(n){var x=1;if(n){var x=2;x=x+1;}else{var x=4;x=x+1;}return x;};f(1);",
         L"(1)==1 && goat_local14(0)==1"},
        {L"const f=func(n){var x=n;{var x=1.5;x=x+0.5;}return x;};f(1);", L"(7)==7"},
        {L"const f=func(n){var x=n;{var y=x+1;x=y*2;}return x;};f(1);", L"(3)==8"},
        {L"const f=func(n){{var n=5;return n;}};f(1);", L"(2)==5"},
        {L"const f=func(n){var test=n<0;if(test)return -1;test=n>0;if(test)return 1;return "
         L"0;};f(1);",
         L"(-2)==-1 && goat_local18(2)==1 && goat_local18(0)==0"},
        {L"const f=func(n){const test=n==0;if(test)return 1;return 0;};f(1.0);",
         L"(-0.0)==1 && goat_local19(NAN)==0"},
        {L"const f=func(n){var x=n;if(n<0){var x=2;return x;}return x;};f(1);",
         L"(-1)==2 && goat_local20(8)==8"},
        {L"const f=func(n){var x=n;{var x=2;{var x=3;x=x+1;}x=x+1;}return x;};f(1);", L"(7)==7"},
        {L"const f=func(n){var x=n;{const first=x;var x=first+1;x=x+2;}return x;};f(1);",
         L"(7)==7"},
        {L"const f=func(n){var unused=n;unused=n+2;return n;};f(1);", L"(7)==7"},
        {L"const f=func(n){var g_l0=n,g_t0=n+1;return g_l0+g_t0;};f(1);", L"(3)==7"},
        {L"const f=func(n){var значение=n;значение=значение+1;return значение;};f(1);", L"(3)==4"},
        {L"const f=func(n){var x=n;x=x+1.0;return x-n;};f(1.0);", L"(0x1p53)==0.0"},
        {L"const f=func(n){var x=n,y=0.0;return (y=x)+((x=2.0)-y);};f(1.0);", L"(0x1p53)==2.0"},
        {L"const f=func(n){var x=n;x=-x;return x;};f(1.0);",
         L"(0.0)==0 && signbit(goat_local28(0.0)) && isnan(goat_local28(NAN)) && "
         L"goat_local28(INFINITY)==-INFINITY"},
        {L"const f=func(n){var x=n;return x=(x=1)+2;};f(1);", L"(9)==3"},
        {L"const f=func(n){var x=n;if((x=n-1))return x;return x;};f(1);",
         L"(1)==0 && goat_local30(9)==8"},
        {L"const f=func(n){if(n)var x=1;x=2;return x;};f(1);", L"(0)==2 && goat_local31(1)==2"},
        {L"const f=func(n){if(true)var x=n;return x;};f(1);", L"(7)==7"},
        {L"const f=func(n){var x=n;if(n){const x=3;return x;}else{const x=4;return x;}};f(1);",
         L"(1)==3 && goat_local33(0)==4"},
        {L"const f=func(n){var x=n;{var y=x;{var x=y+2;y=x;}x=y;}return x;};f(1);", L"(7)==9"},
        {L"const f=func(n){var x=n,b=n<0;if((b=n>0)){x=1;}return x;};f(1);",
         L"(-7)==-7 && goat_local35(7)==1"}};

    for (size_t i = 0; i < sizeof(fixtures) / sizeof(*fixtures); i++) {
        arena_t *arena = create_arena(32);
        parser_memory_t memory = {arena, arena, arena, arena};
        const wchar_t *source = fixtures[i].source;
        node_t *root =
            parse_analysis_test_program(&memory, (string_value_t){source, wcslen(source), false});
        options_t *options = create_options();
        bool success = root && !analyze(root, &memory, options, NULL);
        c_module_t *module = success ? create_c_module(arena, root) : NULL;
        success = module && module->available_count == 1;
        string_value_t name = format_string(L"goat_local%zu", i);
        if (success) {
            c_generation_result_t result =
                generate_c_function(module->head->summary,
                                    (string_view_t){name.data, name.length},
                                    NULL,
                                    NULL);
            success = result.status == C_GENERATION_OK;
            if (success)
                add_formatted_source(output, 0, result.source);
            else {
                fprintf(stderr,
                        "Local emission status %d at %ls\n",
                        result.status,
                        result.failed_node ? result.failed_node->vtbl->type_name : L"none");
                FREE_STRING(result.source);
            }
        }
        if (success)
            add_source(
                checks,
                1,
                L"if (!(%s%s)) { fprintf(stderr, \"local fixture %zu failed\\n\"); return 105; }",
                name.data,
                fixtures[i].check,
                i);
        else
            fprintf(stderr, "Local fixture %zu failed: %ls\n", i, source);
        FREE_STRING(name);
        destroy_options(options);
        destroy_arena(arena);
        if (!success)
            return false;
    }
    return true;
}

bool test_c_local_rejections(void) {
    const wchar_t *sources[] = {L"const f=func(n){var x=1;x=1.5;return x;};f(1);",
                                L"const f=func(n){n=1.5;return n;};f(1);",
                                L"const f=func(n){var x=1;if(n){x=1.5;}return n;};f(1);",
                                L"const f=func(n){var x=n<0;x=1;return n;};f(1);",
                                L"const f=func(n){var x;x=n;return x;};f(1);",
                                L"const f=func(n){if(n)var x=1;return x;};f(1);",
                                L"var x=1;const f=func(n){x=n;return x;};f(1);",
                                L"const f=func(n){const x=n;x=2;return x;};f(1);"};
    for (size_t i = 0; i < sizeof(sources) / sizeof(*sources); i++) {
        arena_t *arena = create_arena(32);
        parser_memory_t memory = {arena, arena, arena, arena};
        node_t *root =
            parse_analysis_test_program(&memory,
                                        (string_value_t){sources[i], wcslen(sources[i]), false});
        ASSERT(root);
        options_t *options = create_options();
        ASSERT(!analyze(root, &memory, options, NULL));
        ASSERT(create_c_module(arena, root)->available_count == 0);
        destroy_options(options);
        destroy_arena(arena);
    }
    arena_t *arena = create_arena(32);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(
        &memory,
        STATIC_STRING(L"const f=func(n){var x=n;{var y=x;x=y+1;}return x;};f(1);"));
    ASSERT(root);
    options_t *options = create_options();
    ASSERT(!analyze(root, &memory, options, NULL));
    c_module_t *module = create_c_module(arena, root);
    ASSERT(module->available_count == 1);
    const function_summary_t *summary = module->head->summary;
    node_t *body = get_node_child(summary->function, 1);
    node_t *declaration = get_node_child(get_node_child(body, 0), 0);
    node_t *initial = (node_t *)replacement_original(get_node_child(declaration, 0));
    node_vtbl_t *original = initial->vtbl;
    node_vtbl_t overridden = *original;
    overridden.generate_c_code = no_c_code;
    initial->vtbl = &overridden;
    c_generation_result_t result = generate_c_function(summary, module->head->name, NULL, NULL);
    initial->vtbl = original;
    ASSERT(result.status == C_GENERATION_UNSUPPORTED && !result.source.data);
    ASSERT(result.failed_node == initial);
    /* The selected proof must still agree with the actual storage at every write. */
    original = declaration->vtbl;
    overridden = *original;
    overridden.type = NODE_CONSTANT_DECLARATOR;
    declaration->vtbl = &overridden;
    result = generate_c_function(summary, module->head->name, NULL, NULL);
    declaration->vtbl = original;
    ASSERT(result.status == C_GENERATION_NOT_PROVEN && !result.source.data);
    result = generate_c_function(summary, module->head->name, NULL, NULL);
    ASSERT(result.status == C_GENERATION_OK);
    FREE_STRING(result.source);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}
