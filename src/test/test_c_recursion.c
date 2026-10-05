/** @file test_c_recursion.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Recursive calls across functions and numeric specializations.
 */
#include "test_c_recursion.h"

#include "analysis/analysis.h"
#include "analysis_test_support.h"
#include "cli/options.h"
#include "codegen/c_module.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"
#include "test_macro.h"

#include <stdio.h>
#include <wchar.h>

static c_module_t *analyzed(arena_t *arena, const wchar_t *source) {
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root =
        parse_analysis_test_program(&memory, (string_value_t){source, wcslen(source), false});
    options_t *options = create_options();
    bool success = root && !analyze(root, &memory, options, NULL);
    destroy_options(options);
    return success ? create_c_module(arena, root) : NULL;
}

bool append_c_recursion_tests(source_builder_t *output, source_builder_t *checks) {
    const struct {
        const wchar_t *source, *check;
        size_t count;
    } fixtures[] = {
        {L"const f=func(n){if(n<1)return 0;if(n==1)return 1;return f(n-1)+f(n-2);};f(2);",
         L"for(int i=0;i<21;i++){int64_t a=0,b=1;for(int j=0;j<i;j++){int64_t c=a+b;a=b;b=c;}"
         L"if(goat_rec0_g_f1_i_(i)!=a)return 110;}"
         L"if(goat_rec0_g_f1_i_(-1)!=0)return 110;",
         1},
        {L"const f=func(n){if(n<2)return 1;var saved=n;var child=f(n-1);return saved*child;};f(3);",
         L"if(goat_rec1_g_f1_i_(0)!=1 || goat_rec1_g_f1_i_(10)!=3628800 || "
         L"goat_rec1_g_f1_i_(21)!=-INT64_C(4249290049419214848))return 111;",
         1},
        {L"const even=func(n){if(n<1)return 1;return odd(n-1);};"
         L"const odd=func(n){if(n<1)return 0;return even(n-1);};even(2);",
         L"for(int i=0;i<100;i++){if(goat_rec2_g_f1_i_(i)!=(1-i%2) || "
         L"goat_rec2_g_f2_i_(i)!=i%2)return 112;}",
         2},
        {L"const first=func(n){if(n<1)return 10;return second(n-1)+1;};"
         L"const second=func(n){if(n<1)return 20;return third(n-1)+2;};"
         L"const third=func(n){if(n<1)return 30;return first(n-1)+3;};first(3);",
         L"if(goat_rec3_g_f1_i_(0)!=10 || goat_rec3_g_f1_i_(1)!=21 || "
         L"goat_rec3_g_f1_i_(2)!=33 || goat_rec3_g_f1_i_(3)!=16 || "
         L"goat_rec3_g_f3_i_(4)!=19)return 113;",
         3},
        {L"const f=func(n,x){if(n<1)return x;return f(n-1,x+x);};f(0,1);f(0,1.5);",
         L"if(goat_rec4_g_f1_i_i_(3,2)!=16 || goat_rec4_g_f1_i_i_(1,INT64_MAX)!=-2 || "
         L"goat_rec4_g_f1_i_r_(3,0.25)!=2.0 || !signbit(goat_rec4_g_f1_i_r_(3,-0.0)) || "
         L"!isnan(goat_rec4_g_f1_i_r_(3,NAN)) || "
         L"goat_rec4_g_f1_i_r_(3,INFINITY)!=INFINITY)return 114;",
         2},
        {L"const f=func(n){if(n<1)return n+0.0;return f(n-1.0)+1.0;};f(2);",
         L"if(goat_rec5_g_f1_i_(5)!=5.0 || goat_rec5_g_f1_r_(2.5)!=2.5 || "
         L"goat_rec5_g_f1_r_(-0.5)!=-0.5)return 115;",
         2},
        {L"const f=func(n,a,b){if(n<1)return a-b;return f(n-1,b,a);};f(2,5,0.5);",
         L"if(goat_rec6_g_f1_i_i_r_(4,5,0.5)!=4.5 || "
         L"goat_rec6_g_f1_i_i_r_(3,5,0.5)!=-4.5 || "
         L"goat_rec6_g_f1_i_r_i_(3,0.5,5)!=4.5)return 116;",
         2},
        {L"const f=func(n){if(n<1)return 7;return alias(n-1)+2;};const alias=f;alias(2);",
         L"if(goat_rec7_g_f1_i_(0)!=7 || goat_rec7_g_f1_i_(12)!=31)return 117;",
         1},
        {L"const f=func(n,x){if(n<1)return x;var y=x;return f(n-1,y,y=y+2);};f(2,1);",
         L"if(goat_rec8_g_f1_i_i_(4,3)!=11)return 118;",
         1},
        {L"const f=func(n,x){if(n<1)return x;return f(n-1,x+1.0);};f(2,1.5);",
         L"if(goat_rec9_g_f1_i_r_(4,0x1p53)!=0x1p53 || "
         L"goat_rec9_g_f1_i_r_(4,0.5)!=4.5)return 119;",
         1}};

    for (size_t i = 0; i < sizeof(fixtures) / sizeof(*fixtures); i++) {
        arena_t *arena = create_arena(32);
        c_module_t *module = analyzed(arena, fixtures[i].source);
        bool success = module && module->count == fixtures[i].count
                       && module->available_count == module->count;
        for (c_module_function_t *entry = success ? module->head : NULL; entry;
             entry = entry->next) {
            string_value_t name = format_string(L"goat_rec%zu_%s", i, entry->name.data);
            entry->name = copy_string_to_arena(arena, name.data, name.length);
            FREE_STRING(name);
        }
        for (c_module_function_t *entry = success ? module->head : NULL; entry && success;
             entry = entry->next) {
            for (c_generation_callee_t *callee = entry->callees; callee;
                 callee = (c_generation_callee_t *)callee->next)
                callee->name = c_module_find(module, callee->summary)->name;
            c_generation_result_t result =
                generate_c_function(entry->summary, entry->name, NULL, entry->callees);
            success = result.status == C_GENERATION_OK;
            if (success)
                add_formatted_source(output, 0, result.source);
            else {
                fprintf(stderr,
                        "Recursive emission status %d at %ls\n",
                        result.status,
                        result.failed_node ? result.failed_node->vtbl->type_name : L"none");
                FREE_STRING(result.source);
            }
        }
        if (success)
            add_source(checks, 1, L"%s", fixtures[i].check);
        else
            fprintf(stderr,
                    "Recursion fixture %zu failed (entries: %zu)\n",
                    i,
                    module ? module->count : 0);
        destroy_arena(arena);
        if (!success)
            return false;
    }
    return true;
}

bool test_c_recursive_bindings(void) {
    arena_t *arena = create_arena(32);
    c_module_t *module =
        analyzed(arena, L"const f=func(n){if(n<1)return 0;return f(n-1)+1;};f(2);");
    ASSERT(module && module->count == 1 && module->available_count == 1);
    c_module_function_t *entry = module->head;
    ASSERT(entry->callees && !entry->callees->next);
    ASSERT(entry->callees->summary == entry->summary);
    function_summary_t copy = *entry->summary;
    c_generation_callee_t self = {.summary = &copy, .name = entry->name};
    c_generation_result_t result = generate_c_function(entry->summary, entry->name, NULL, &self);
    ASSERT(result.status == C_GENERATION_OK);
    FREE_STRING(result.source);
    copy.return_type = make_real_element();
    result = generate_c_function(entry->summary, entry->name, NULL, &self);
    ASSERT(result.status == C_GENERATION_INVALID_REQUEST && !result.source.data);
    copy = *entry->summary;
    self.name = (string_view_t){L"goat_other", 10};
    result = generate_c_function(entry->summary, entry->name, NULL, &self);
    ASSERT(result.status == C_GENERATION_INVALID_REQUEST && !result.source.data);
    result = generate_c_function(entry->summary, entry->name, NULL, NULL);
    ASSERT(result.status == C_GENERATION_UNSUPPORTED && !result.source.data);
    destroy_arena(arena);
    return true;
}
