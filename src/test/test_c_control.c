/** @file test_c_control.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Compiled comparisons against the model and structured branch regressions.
 */
#include "test_c_control.h"

#include "analysis/analysis.h"
#include "analysis_test_support.h"
#include "cli/options.h"
#include "codegen/c_control.h"
#include "codegen/c_module.h"
#include "graph/common_methods.h"
#include "graph/replacement.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"
#include "model/object.h"
#include "model/process.h"
#include "test_macro.h"

#include <math.h>
#include <stdio.h>
#include <wchar.h>

/** @brief Instrumentation is confined to test ASTs and the generated test translation unit. */
static struct {
    node_t *node;
    node_vtbl_t *original;
    node_vtbl_t overridden;
} probes[4];

static c_generated_expression_t traced_expression(const node_t *node,
                                                  c_generation_context_t *context) {
    for (size_t i = 0; i < 4; i++) {
        if (probes[i].node != node)
            continue;
        c_generated_expression_t result = probes[i].original->generate_c_code(node, context);
        if (!result.success)
            return result;
        source_builder_t *prelude = create_source_builder();
        add_source(prelude, 0, L"goat_branch_trace = goat_branch_trace * 10 + %zu;", i + 1);
        c_emit_prelude(result.prelude, prelude, 0);
        if (result.prelude)
            destroy_source_builder(result.prelude);
        result.prelude = prelude;
        return result;
    }
    return (c_generated_expression_t){0};
}

static bool emit_program(source_builder_t *output, const wchar_t *source, const wchar_t *name) {
    arena_t *arena = create_arena(32);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root =
        parse_analysis_test_program(&memory, (string_value_t){source, wcslen(source), false});
    options_t *options = create_options();
    bool success = root && !analyze(root, &memory, options, NULL);
    c_module_t *module = success ? create_c_module(arena, root) : NULL;
    success = module && module->available_count == 1;
    bool traced = success && !wcscmp(name, L"goat_traced_branch");
    if (traced) {
        node_t *branch = (node_t *)replacement_original(
            get_node_child(get_node_child(module->head->summary->function, 1), 0));
        node_t *condition = (node_t *)replacement_original(get_node_child(branch, 0));
        probes[0].node = (node_t *)replacement_original(get_node_child(condition, 0));
        probes[1].node = (node_t *)replacement_original(get_node_child(condition, 1));
        probes[2].node =
            (node_t *)replacement_original(get_node_child(get_node_child(branch, 1), 0));
        probes[3].node =
            (node_t *)replacement_original(get_node_child(get_node_child(branch, 2), 0));
        for (size_t i = 0; i < 4; i++) {
            probes[i].original = probes[i].node->vtbl;
            probes[i].overridden = *probes[i].original;
            probes[i].overridden.generate_c_code = traced_expression;
            probes[i].node->vtbl = &probes[i].overridden;
        }
    }
    if (success) {
        c_generation_result_t result = generate_c_function(module->head->summary,
                                                           (string_view_t){name, wcslen(name)},
                                                           NULL,
                                                           NULL);
        success = result.status == C_GENERATION_OK;
        if (!success)
            fprintf(stderr,
                    "status %d node %ls\n",
                    result.status,
                    result.failed_node ? result.failed_node->vtbl->type_name : L"none");
        if (success)
            add_formatted_source(output, 0, result.source);
        else
            FREE_STRING(result.source);
    }
    if (traced)
        for (size_t i = 0; i < 4; i++)
            probes[i].node->vtbl = probes[i].original;
    if (!success)
        fprintf(stderr, "C control fixture failed: %ls\n", source);
    destroy_options(options);
    destroy_arena(arena);
    return success;
}

static bool comparison_tests(source_builder_t *output, source_builder_t *checks) {
    const int64_t integers[] = {INT64_MIN,
                                INT64_MIN + 1,
                                -INT64_C(9007199254740993),
                                -1,
                                0,
                                1,
                                INT64_C(9007199254740993),
                                INT64_MAX - 1,
                                INT64_MAX};
    const double reals[] = {-INFINITY,
                            -0x1.0000000000001p63,
                            -0x1p63,
                            -0x1.fffffffffffffp62,
                            -9007199254740992.0,
                            -1.5,
                            -0.5,
                            -0.0,
                            0.0,
                            0.5,
                            1.5,
                            9007199254740992.0,
                            0x1.fffffffffffffp62,
                            0x1p63,
                            0x1.0000000000001p63,
                            INFINITY,
                            NAN};
    add_static_source(output,
                      0,
                      L"static volatile int64_t goat_cmp_ints[] = {INT64_MIN, INT64_MIN+1, "
                      L"-INT64_C(9007199254740993), -1, 0, 1, INT64_C(9007199254740993), "
                      L"INT64_MAX-1, INT64_MAX};");
    add_static_source(
        output,
        0,
        L"static volatile double goat_cmp_reals[] = {-INFINITY, -0x1.0000000000001p63, -0x1p63, "
        L"-0x1.fffffffffffffp62, -9007199254740992.0, -1.5, -0.5, -0.0, 0.0, 0.5, 1.5, "
        L"9007199254740992.0, 0x1.fffffffffffffp62, 0x1p63, 0x1.0000000000001p63, INFINITY, NAN};");
    static const wchar_t *symbols[] = {L"<", L"<=", L">", L">=", L"==", L"!="};
    operation_result_t (*operations[])(process_t *, object_t *, object_t *) = {
        is_object_less_than,
        is_object_less_or_equal,
        is_object_greater_than,
        is_object_greater_or_equal,
        are_objects_equal,
        are_objects_not_equal};
    process_t *process = create_process();
    bool success = true;
    for (size_t op = 0; op < 6 && success; op++) {
        for (size_t pair = 0; pair < 4 && success; pair++) {
            bool left_real = pair & 1, right_real = pair & 2;
            string_value_t source =
                format_string(L"const f=func(a,b){if(a%s b)return 1 else return 0;};f(%s,%s);",
                              symbols[op],
                              left_real ? L"1.0" : L"1",
                              right_real ? L"2.0" : L"2");
            string_value_t name = format_string(L"goat_compare%zu_%zu", op, pair);
            success = emit_program(output, source.data, name.data);
            size_t rows =
                left_real ? sizeof(reals) / sizeof(*reals) : sizeof(integers) / sizeof(*integers);
            size_t cols =
                right_real ? sizeof(reals) / sizeof(*reals) : sizeof(integers) / sizeof(*integers);
            add_source(output, 0, L"static const unsigned char %s_expected[] = {", name.data);
            for (size_t i = 0; i < rows && success; i++) {
                for (size_t j = 0; j < cols && success; j++) {
                    object_t *left = left_real ? create_real_number_object(process, reals[i])
                                               : create_integer_object(process, integers[i]);
                    object_t *right = right_real ? create_real_number_object(process, reals[j])
                                                 : create_integer_object(process, integers[j]);
                    operation_result_t result = operations[op](process, left, right);
                    success = !result.is_exception && result.value;
                    if (success)
                        add_source(output,
                                   1,
                                   L"%u,",
                                   get_object_boolean_value(result.value) ? 1 : 0);
                    DECREF(result.value);
                    DECREF(left);
                    DECREF(right);
                }
            }
            add_static_source(output, 0, L"};");
            add_source(checks,
                       1,
                       L"for (size_t i=0; i<%zu; i++) for (size_t j=0; j<%zu; j++) {",
                       rows,
                       cols);
            add_source(checks,
                       2,
                       L"if (%s(%s[i], %s[j]) != %s_expected[i*%zu+j]) {",
                       name.data,
                       left_real ? L"goat_cmp_reals" : L"goat_cmp_ints",
                       right_real ? L"goat_cmp_reals" : L"goat_cmp_ints",
                       name.data,
                       cols);
            add_source(
                checks,
                3,
                L"fprintf(stderr, \"comparison %zu/%zu %%zu/%%zu failed\\n\", i, j); return 101;",
                op,
                pair);
            add_static_source(checks, 2, L"}");
            add_static_source(checks, 1, L"}");
            FREE_STRING(source);
            FREE_STRING(name);
        }
    }
    destroy_process(process);
    return success;
}

bool append_c_control_tests(source_builder_t *output, source_builder_t *checks) {
    if (!comparison_tests(output, checks))
        return false;
    add_static_source(output, 0, L"static int goat_branch_trace;");
    if (!emit_program(output,
                      L"const f=func(n){if(n<0)return n+1 else return n-1;};f(1);",
                      L"goat_traced_branch"))
        return false;
    add_static_source(checks, 1, L"goat_branch_trace=0;");
    add_static_source(checks,
                      1,
                      L"if (goat_traced_branch(-2)!=-1 || goat_branch_trace!=123) return 103;");
    add_static_source(checks, 1, L"goat_branch_trace=0;");
    add_static_source(checks,
                      1,
                      L"if (goat_traced_branch(2)!=1 || goat_branch_trace!=124) return 104;");

    const struct {
        const wchar_t *source, *check;
    } fixtures[] = {
        {L"const f=func(n){if(n<0){return -n;}else{return n+1;}};f(1);",
         L"(-7)==7 && goat_branch0(3)==4"},
        {L"const f=func(n){if(n<0)return -1;if(n==0)return 0;return 1;};f(1);",
         L"(-1)==-1 && goat_branch1(0)==0 && goat_branch1(1)==1"},
        {L"const f=func(n){if(n){if(n<0)return 1 else return 2;}else return 3;};f(1);",
         L"(-1)==1 && goat_branch2(1)==2 && goat_branch2(0)==3"},
        {L"const f=func(n){if(n){if(n<0){return 1;}}else{return 2;}return 3;};f(1);",
         L"(-1)==1 && goat_branch3(0)==2 && goat_branch3(1)==3"},
        {L"const f=func(n){if(n)return 1;return 0;};f(1.0);",
         L"(NAN)==1 && goat_branch4(INFINITY)==1 && goat_branch4(-INFINITY)==1 && "
         L"goat_branch4(-0.0)==0 && goat_branch4(0.0)==0 && "
         L"goat_branch4(0x0.0000000000001p-1022)==1"},
        {L"const f=func(n){if(n)return 1;return 0;};f(1);",
         L"(INT64_MIN)==1 && goat_branch5(INT64_MAX)==1 && goat_branch5(0)==0"},
        {L"const f=func(n){if((n-1)<(n+1))return n*2 else return -n;};f(1);",
         L"(4)==8 && goat_branch6(INT64_MAX)==INT64_MAX"},
        {L"const f=func(n){if((n<0)){return -n;}else if((n>0)){return n;}else{return 0;}};f(1);",
         L"(-3)==3 && goat_branch7(3)==3 && goat_branch7(0)==0"},
        {L"const f=func(n){if(n){}else{};return n;};f(1);", L"(4)==4 && goat_branch8(0)==0"},
        {L"const f=func(n){if(n){n+2;}else{n-2;}return n;};f(1);", L"(4)==4 && goat_branch9(0)==0"},
        {L"const f=func(n){if(n>0)return 1.5 else return -2.5;};f(1.0);",
         L"(1.0)==1.5 && goat_branch10(NAN)==-2.5"},
        {L"const f=func(n){if(n<1)return 0;if(n==1)return 1;return n+2;};f(1);",
         L"(0)==0 && goat_branch11(1)==1 && goat_branch11(4)==6"},
        /* Expectations independent of the runtime comparison helper. */
        {L"const f=func(a,b){if(a==b)return 1;return 0;};f(1,1.0);",
         L"(INT64_C(9007199254740993),9007199254740992.0)==0 && "
         L"goat_branch12(INT64_MIN,-0x1p63)==1 && goat_branch12(INT64_MAX,0x1p63)==0"},
        {L"const f=func(a,b){if(a>b)return 1;return 0;};f(1,1.0);",
         L"(INT64_C(9007199254740993),9007199254740992.0)==1 && goat_branch13(0,-0.5)==1 && "
         L"goat_branch13(0,NAN)==0"},
        {L"const f=func(a,b){if(a<b)return 1;return 0;};f(1.0,1);",
         L"(9007199254740992.0,INT64_C(9007199254740993))==1 && goat_branch14(NAN,0)==0"},
        {L"const f=func(n){if(true)return n;};f(1);", L"(7)==7"},
        {L"const f=func(n){if(false){}return n;};f(1);", L"(8)==8"},
        {L"const f=func(n){if((1))return n;};f(1);", L"(9)==9"},
        {L"const f=func(n){if(-1.0)return n;};f(1);", L"(10)==10"}};

    for (size_t i = 0; i < sizeof(fixtures) / sizeof(*fixtures); i++) {
        string_value_t name = format_string(L"goat_branch%zu", i);
        bool success = emit_program(output, fixtures[i].source, name.data);
        if (success) {
            add_source(
                checks,
                1,
                L"if (!(%s%s)) { fprintf(stderr, \"branch fixture %zu failed\\n\"); return 102; }",
                name.data,
                fixtures[i].check,
                i);
        }
        FREE_STRING(name);
        if (!success)
            return false;
    }
    return true;
}

bool test_c_control_rejections(void) {
    arena_t *arena = create_arena(32);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(
        &memory,
        STATIC_STRING(L"const f=func(n){if(n<0)return n+0 else return n+1;};f(1);"));
    ASSERT(root);
    options_t *options = create_options();
    ASSERT(!analyze(root, &memory, options, NULL));
    c_module_t *module = create_c_module(arena, root);
    ASSERT(module->available_count == 1);
    function_summary_t summary = *module->head->summary;
    const node_t *branch =
        replacement_original(get_node_child(get_node_child(summary.function, 1), 0));
    node_t *targets[] = {
        (node_t *)replacement_original(get_node_child(branch, 0)),
        (node_t *)replacement_original(get_node_child(get_node_child(branch, 1), 0)),
        (node_t *)replacement_original(get_node_child(get_node_child(branch, 2), 0))};
    for (size_t i = 0; i < 3; i++) {
        c_expression_proof_t *head = NULL;
        bool removed = false;
        for (const c_expression_proof_t *proof = module->head->summary->c_expressions; proof;
             proof = proof->next) {
            if (proof->node == targets[i]) {
                removed = true;
                continue;
            }
            c_expression_proof_t *copy = alloc_from_arena(arena, sizeof(*copy));
            *copy = *proof;
            copy->next = head;
            head = copy;
        }
        ASSERT(removed);
        summary.c_expressions = head;
        c_generation_result_t result =
            generate_c_function(&summary, module->head->name, NULL, NULL);
        ASSERT(result.status == C_GENERATION_NOT_PROVEN && !result.source.data);
        ASSERT(result.failed_node == targets[i]);
    }
    summary = *module->head->summary;
    node_vtbl_t overridden = *targets[2]->vtbl;
    node_vtbl_t *original = targets[2]->vtbl;
    overridden.generate_c_code = no_c_code;
    targets[2]->vtbl = &overridden;
    c_generation_result_t result = generate_c_function(&summary, module->head->name, NULL, NULL);
    targets[2]->vtbl = original;
    ASSERT(result.status == C_GENERATION_UNSUPPORTED && !result.source.data);
    ASSERT(result.failed_node == targets[2]);
    result = generate_c_function(&summary, module->head->name, NULL, NULL);
    ASSERT(result.status == C_GENERATION_OK);
    FREE_STRING(result.source);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}
