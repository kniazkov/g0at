/** @file test_c_calls.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Static specialization selection and right-to-left call arguments.
 */
#include "test_c_calls.h"

#include "analysis/analysis.h"
#include "analysis_test_support.h"
#include "cli/options.h"
#include "codegen/c_control.h"
#include "codegen/c_module.h"
#include "graph/replacement.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"
#include "test_macro.h"

#include <stdio.h>
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

static c_generated_expression_t (*probe_original)(const node_t *, c_generation_context_t *);

/** @brief Counts actual generated calls without changing the analyzer's fixture. */
static c_generated_expression_t counted_return(const node_t *node,
                                               c_generation_context_t *context) {
    c_generated_expression_t result = probe_original(node, context);
    if (!result.success)
        return result;
    source_builder_t *prelude = create_source_builder();
    add_static_source(prelude, 0, L"goat_call_trace++;");
    c_emit_prelude(result.prelude, prelude, 0);
    if (result.prelude)
        destroy_source_builder(result.prelude);
    result.prelude = prelude;
    return result;
}

bool append_c_call_tests(source_builder_t *output, source_builder_t *checks) {
    add_static_source(output, 0, L"static volatile int goat_call_trace;");

    const struct {
        const wchar_t *source, *check;
        size_t function_id;
        bool real;
    } fixtures[] = {
        {L"const leaf=func(n){return n+1;};const f=func(n){return leaf(n)*2;};f(1);",
         L"(4)==10 && goat_call_case0(INT64_MAX)==0",
         2},
        {L"const f=func(n){return leaf(n);};const leaf=func(n){return n+2;};f(1);", L"(5)==7", 1},
        {L"const leaf=func(a,b){return a*10+b;};const f=func(n){var x=n;return "
         L"leaf((x=x+1),(x=x+2));};f(1);",
         L"(3)==65",
         2},
        {L"const leaf=func(a,b){return a*10+b;};const f=func(n){var x=n;return "
         L"leaf(x,(x=x+1));};f(1);",
         L"(3)==44",
         2},
        {L"const leaf=func(a){return a;};const f=func(n){var x=n;var v=leaf(x,(x=x+2));return "
         L"v*10+x;};f(1);",
         L"(3)==55",
         2},
        {L"const leaf=func(){return 7;};const f=func(n){var x=n;var v=leaf((x=x+1),(x=x+2));return "
         L"v*10+x;};f(1);",
         L"(3)==76",
         2},
        {L"const leaf=func(n){return n*2;};const pair=func(a,b){return a*10+b;};const "
         L"f=func(n){var x=n;return pair(leaf(x=x+1),leaf(x=x+2));};f(1);",
         L"(3)==130",
         3},
        {L"const leaf=func(n){return n+0.5;};const f=func(n){return leaf(n)*2.0;};f(1);",
         L"(2)==5.0",
         2},
        {L"const leaf=func(n){return n+n;};const f=func(n){return leaf(n);};f(1);f(1.5);",
         L"(INT64_MAX)==-2",
         2},
        {L"const leaf=func(n){return n+n;};const f=func(n){return leaf(n);};f(1);f(1.5);",
         L"(1.5)==3.0 && signbit(goat_call_case9(-0.0)) && isnan(goat_call_case9(NAN)) && "
         L"goat_call_case9(INFINITY)==INFINITY",
         2,
         true},
        {L"const leaf=func(a,b){return a-b;};const f=func(n){return leaf(n,0.5);};f(1);",
         L"(2)==1.5",
         2},
        {L"const leaf=func(a,b){return a-b;};const f=func(n){return leaf(0.5,n);};f(1);",
         L"(2)==-1.5",
         2},
        {L"const leaf=func(n){return n+3;};const alias=leaf;const another=alias;const "
         L"f=func(n){return another(n);};f(1);",
         L"(2)==5",
         2},
        {L"const print=func(n){return n+1;};const f=func(n){return print(n);};f(1);", L"(3)==4", 2},
        {L"const leaf=func(n){return n*2;};const f=func(n){var "
         L"x=n;if(n<0){x=leaf(x=x-1);}else{x=leaf(x=x+1);}return x;};f(1);",
         L"(-3)==-8 && goat_call_case14(3)==8",
         2},
        {L"const leaf=func(n){return n;};const f=func(n){var x=n;leaf(x=x+2);return x;};f(1);",
         L"(3)==5",
         2},
        {L"const abs=func(n){return n-1;};const f=func(n){return abs(n);};f(1);", L"(-2)==-3", 2},
        {L"const leaf=func(n){return n;};const f=func(n){return leaf(n,n>0);};f(1);",
         L"(-7)==-7",
         2},
        {L"const leaf=func(){return 42;};const f=func(){return leaf();};f();", L"()==42", 2},
        {L"const leaf=func(n){return n;};const f=func(n){var x=n;return "
         L"leaf(x=x+1)+leaf(x=x+2);};f(1);",
         L"(3)==10",
         2},
        {L"const leaf=func(n){return n+2;};const middle=func(n){return leaf(n)*3;};const "
         L"f=func(n){var x=middle(n);return leaf(x);};f(1);",
         L"(2)==14",
         3},
        {L"const leaf=func(n){return n+1.0;};const f=func(n){return leaf(n)-n;};f(1.0);",
         L"(0x1p53)==0.0",
         2,
         true},
        {L"const leaf=func(n){return n+1;};const f=func(n){var x=n;if(n<0){return "
         L"leaf(x=x-1);}return leaf(x=x+1)+leaf(x=x+2);};f(1);",
         L"(3)==12 && goat_call_trace==2",
         2}};

    for (size_t i = 0; i < sizeof(fixtures) / sizeof(*fixtures); i++) {
        arena_t *arena = create_arena(32);
        node_t *root = analyzed(arena, fixtures[i].source);
        c_module_t *module = root ? create_c_module(arena, root) : NULL;
        bool success = module && module->count && module->count == module->available_count;
        c_module_function_t *selected = NULL;
        for (c_module_function_t *entry = success ? module->head : NULL; entry;
             entry = entry->next) {
            bool real = entry->summary->parameter_count
                        && entry->summary->parameter_types[0]->type == LATTICE_REAL;
            bool choose = entry->function_id == fixtures[i].function_id && real == fixtures[i].real;
            string_value_t name = choose ? format_string(L"goat_call_case%zu", i)
                                         : format_string(L"goat_calls%zu_%s", i, entry->name.data);
            entry->name = copy_string_to_arena(arena, name.data, name.length);
            FREE_STRING(name);
            if (choose)
                selected = entry;
        }
        success &= selected != NULL;
        for (c_module_function_t *entry = success ? module->head : NULL; entry && success;
             entry = entry->next) {
            for (c_generation_callee_t *callee = entry->callees; callee;
                 callee = (c_generation_callee_t *)callee->next)
                callee->name = c_module_find(module, callee->summary)->name;
            node_t *probe =
                i == 22 && entry->function_id == 1
                    ? (node_t *)replacement_original(get_node_child(
                          get_node_child(get_node_child(entry->summary->function, 1), 0),
                          0))
                    : NULL;
            node_vtbl_t *original = probe ? probe->vtbl : NULL;
            node_vtbl_t overridden;
            if (probe) {
                overridden = *original;
                probe_original = original->generate_c_code;
                overridden.generate_c_code = counted_return;
                probe->vtbl = &overridden;
            }
            c_generation_result_t result =
                generate_c_function(entry->summary, entry->name, NULL, entry->callees);
            if (probe)
                probe->vtbl = original;
            success = result.status == C_GENERATION_OK;
            if (success)
                add_formatted_source(output, 0, result.source);
            else {
                fprintf(stderr,
                        "Call emission status %d at %ls\n",
                        result.status,
                        result.failed_node ? result.failed_node->vtbl->type_name : L"none");
                FREE_STRING(result.source);
            }
        }
        if (i == 22)
            add_static_source(checks, 1, L"goat_call_trace=0;");
        if (success)
            add_source(
                checks,
                1,
                L"if (!(%s%s)) { fprintf(stderr, \"call fixture %zu failed\\n\"); return 107; }",
                selected->name.data,
                fixtures[i].check,
                i);
        else
            fprintf(stderr, "Call fixture %zu failed: %ls\n", i, fixtures[i].source);
        if (success && i == 22) {
            add_static_source(checks, 1, L"goat_call_trace=0;");
            add_static_source(checks,
                              1,
                              L"if (goat_call_case22(-3)!=-3 || goat_call_trace!=1) return 108;");
        }
        destroy_arena(arena);
        if (!success)
            return false;
    }
    return true;
}

bool test_c_call_rejections(void) {
    arena_t *arena = create_arena(32);
    node_t *root = analyzed(arena,
                            L"const leaf=func(n){return n+1;};const other=func(n){return n-1;};"
                            L"const f=func(n){return leaf(n);};f(1);leaf(1.5);other(1);");
    ASSERT(root);
    c_module_t *module = create_c_module(arena, root);
    ASSERT(module->available_count == 4);
    const c_module_function_t *caller = NULL, *leaf = NULL, *real = NULL, *other = NULL;
    for (const c_module_function_t *entry = module->head; entry; entry = entry->next) {
        if (entry->function_id == 3)
            caller = entry;
        else if (entry->function_id == 2)
            other = entry;
        else if (entry->summary->parameter_types[0]->type == LATTICE_INTEGER)
            leaf = entry;
        else
            real = entry;
    }
    ASSERT(caller && leaf && real && other);
    function_summary_t summary = *caller->summary;
    c_generation_callee_t binding = {.summary = leaf->summary, .name = leaf->name};
    c_generation_result_t result = generate_c_function(&summary, caller->name, NULL, NULL);
    ASSERT(result.status == C_GENERATION_UNSUPPORTED && !result.source.data);
    binding.summary = real->summary;
    binding.name = real->name;
    result = generate_c_function(&summary, caller->name, NULL, &binding);
    ASSERT(result.status == C_GENERATION_UNSUPPORTED && !result.source.data);
    binding.summary = snapshot_function_summary(arena, leaf->summary);
    binding.name = leaf->name;
    result = generate_c_function(&summary, caller->name, NULL, &binding);
    ASSERT(result.status == C_GENERATION_OK);
    FREE_STRING(result.source);
    summary.c_calls = NULL;
    result = generate_c_function(&summary, caller->name, NULL, &binding);
    ASSERT(result.status == C_GENERATION_NOT_PROVEN && !result.source.data);
    summary = *caller->summary;
    c_call_t duplicate = {.next = summary.c_calls,
                          .site = summary.c_calls->site,
                          .target = other->summary};
    summary.c_calls = &duplicate;
    result = generate_c_function(&summary, caller->name, NULL, &binding);
    ASSERT(result.status == C_GENERATION_NOT_PROVEN && !result.source.data);
    duplicate.target = NULL;
    result = generate_c_function(&summary, caller->name, NULL, &binding);
    ASSERT(result.status == C_GENERATION_NOT_PROVEN && !result.source.data);
    duplicate.target = binding.summary;
    result = generate_c_function(&summary, caller->name, NULL, &binding);
    ASSERT(result.status == C_GENERATION_OK);
    FREE_STRING(result.source);
    summary = *caller->summary;
    binding.name = (string_view_t){L"goat_x;", 7};
    result = generate_c_function(&summary, caller->name, NULL, &binding);
    ASSERT(result.status == C_GENERATION_INVALID_REQUEST && !result.source.data);
    binding.name = caller->name;
    result = generate_c_function(&summary, caller->name, NULL, &binding);
    ASSERT(result.status == C_GENERATION_INVALID_REQUEST && !result.source.data);
    binding.name = leaf->name;
    c_generation_callee_t collision = {.summary = other->summary, .name = leaf->name};
    binding.next = &collision;
    result = generate_c_function(&summary, caller->name, NULL, &binding);
    ASSERT(result.status == C_GENERATION_INVALID_REQUEST && !result.source.data);
    collision.summary = leaf->summary;
    collision.name = other->name;
    result = generate_c_function(&summary, caller->name, NULL, &binding);
    ASSERT(result.status == C_GENERATION_INVALID_REQUEST && !result.source.data);
    collision.name = leaf->name;
    result = generate_c_function(&summary, caller->name, NULL, &binding);
    ASSERT(result.status == C_GENERATION_OK);
    FREE_STRING(result.source);
    binding.next = NULL;
    function_summary_t changed = *leaf->summary;
    changed.status = FUNCTION_INCONCLUSIVE;
    binding.summary = &changed;
    result = generate_c_function(&summary, caller->name, NULL, &binding);
    ASSERT(result.status == C_GENERATION_NOT_PROVEN && !result.source.data);
    /* Even a matching target record must not cause implicit argument coercion. */
    changed = *leaf->summary;
    const lattice_element_t *parameter = make_real_element();
    changed.parameter_types = &parameter;
    c_call_t proof = {.site = summary.c_calls->site, .target = &changed};
    summary.c_calls = &proof;
    result = generate_c_function(&summary, caller->name, NULL, &binding);
    ASSERT(result.status == C_GENERATION_NOT_PROVEN && !result.source.data);
    destroy_arena(arena);

    const wchar_t *unsupported[] = {
        L"var leaf=func(n){return n;};const f=func(n){return leaf(n);};f(1);",
        L"const leaf=func(a,b){return a+b;};const f=func(n){return leaf(n);};f(1);",
        L"const leaf=func(n){return n;};const f=func(n){return leaf(n,1/n);};f(1);",
        L"const f=func(n){return abs(n);};f(1);"};
    for (size_t i = 0; i < sizeof(unsupported) / sizeof(*unsupported); i++) {
        arena = create_arena(32);
        root = analyzed(arena, unsupported[i]);
        ASSERT(root);
        module = create_c_module(arena, root);
        for (const c_module_function_t *entry = module->head; entry; entry = entry->next)
            ASSERT(entry->function_id != (i == 3 ? 1 : 2));
        destroy_arena(arena);
    }
    return true;
}
