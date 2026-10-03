/** @file test_c_emission.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Parse, analyze and generate standalone C with runtime assertions.
 */
#include "test_c_emission.h"

#include "analysis/analysis.h"
#include "analysis_test_support.h"
#include "cli/options.h"
#include "codegen/c_module.h"
#include "graph/replacement.h"
#include "lib/allocate.h"
#include "lib/io.h"
#include "lib/string_ext.h"
#include "test_macro.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <wchar.h>

/** @brief Numeric boundary fixtures use literal ASTs that also occur after transformations. */
typedef struct {
    const wchar_t *source; /**< Goat program used to discover the function signature. */
    const wchar_t *check;  /**< C assertion suffix, appended to the generated function name. */
    int literal;
    int64_t integer;
    double real;
} fixture_t;

static bool generate_tests(source_builder_t *output) {
    const fixture_t fixtures[] = {
        {L"const f=func(n){return n;};f(1);", L"(INT64_MIN)==INT64_MIN"},
        {L"const f=func(n){return n;};f(1.0);", L"(-0.0)==0 && signbit(goat_case1(-0.0))"},
        {L"const f=func(first, second){return second;};f(1,2.5);", L"(7,2.5)==2.5"},
        {L"const f=func(){return 42;};f();", L"()==42"},
        {L"const f=func(){return 0;};f();", L"()==INT64_MIN", 1, INT64_MIN},
        {L"const f=func(){return 0;};f();", L"()==INT64_MAX", 1, INT64_MAX},
        {L"const f=func(){return 0;};f();", L"()==-7", 1, -7},
        {L"const f=func(){return 0.0;};f();", L"()==0 && signbit(goat_case7())", 2, 0, -0.0},
        {L"const f=func(){return 0.0;};f();", L"()==DBL_MAX", 2, 0, DBL_MAX},
        {L"const f=func(){return 0.0;};f();",
         L"()==0x0.0000000000001p-1022",
         2,
         0,
         0x0.0000000000001p-1022},
        {L"const f=func(){return 0.0;};f();", L"()==INFINITY", 2, 0, INFINITY},
        {L"const f=func(){return 0.0;};f();", L"()==-INFINITY", 2, 0, -INFINITY},
        {L"const f=func(){return 0.0;};f();", L"()!=goat_case12()", 2, 0, NAN},
        {L"const f=func(){return 1.2345678901234567;};f();", L"()==expected_decimal"},
        {L"const f=func(n){return n;};f(1.0);", L"(INFINITY)==INFINITY"},
        {L"const f=func(n){return n;};f(1.0);", L"(NAN)!=goat_case15(NAN)"}};
    source_builder_t *checks = create_source_builder();
    add_static_source(checks, 0, L"int main(void) {");
    add_static_source(checks, 1, L"volatile double expected_decimal = 1.2345678901234567;");
    bool success = true;
    for (size_t i = 0; i < sizeof(fixtures) / sizeof(*fixtures) && success; i++) {
        const fixture_t *fixture = &fixtures[i];
        arena_t *arena = create_arena(32);
        parser_memory_t memory = {arena, arena, arena, arena};
        node_t *root = parse_analysis_test_program(
            &memory,
            (string_value_t){fixture->source, wcslen(fixture->source), false});
        options_t *options = create_options();
        success = root && !analyze(root, &memory, options, NULL);
        c_module_t *module = success ? create_c_module(arena, root) : NULL;
        success = module && module->count == 1 && module->available_count == 1;
        if (success && fixture->literal) {
            node_t *ret = get_node_child(get_node_child(module->head->summary->function, 1), 0);
            node_t *value = fixture->literal == 1 ? create_integer_node(arena, fixture->integer)
                                                  : create_real_number_node(arena, fixture->real);
            success = replace_child_node(ret, get_node_child(ret, 0), value)
                      && !analyze(root, &memory, options, NULL);
            module = success ? create_c_module(arena, root) : NULL;
            success = module && module->available_count == 1;
        }
        if (success) {
            string_value_t name = format_string(L"goat_case%zu", i);
            c_generation_result_t result =
                generate_c_function(module->head->summary,
                                    (string_view_t){name.data, name.length},
                                    NULL,
                                    NULL);
            success = result.status == C_GENERATION_OK && result.source.data;
            if (success) {
                add_formatted_source(output, 0, result.source);
                add_source(checks,
                           1,
                           L"if (!(%s%s)) return %zu;",
                           name.data,
                           fixture->check,
                           i + 1);
            } else {
                FREE_STRING(result.source);
            }
            FREE_STRING(name);
        }
        if (!success)
            fprintf(stderr, "C emission fixture %zu failed\n", i);
        destroy_options(options);
        destroy_arena(arena);
    }
    add_static_source(checks, 1, L"return 0;");
    add_static_source(checks, 0, L"}");
    if (success)
        add_formatted_source(output, 0, build_source(checks));
    destroy_source_builder(checks);
    return success;
}

bool write_c_emission_tests(const char *path) {
    source_builder_t *output = create_source_builder();
    bool success = generate_tests(output);
    if (success) {
        string_value_t source = build_source(output);
        success = write_utf8_file(path, source.data);
        FREE_STRING(source);
    }
    destroy_source_builder(output);
    return success;
}

bool test_c_emission(void) {
    source_builder_t *output = create_source_builder();
    bool success = generate_tests(output);
    destroy_source_builder(output);
    ASSERT(success);
    return true;
}

bool test_c_emission_rejections(void) {
    arena_t *arena = create_arena(32);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root =
        parse_analysis_test_program(&memory,
                                    STATIC_STRING(L"const f=func(n){return n;};f(1);f(1.0);"));
    ASSERT(root);
    options_t *options = create_options();
    ASSERT(!analyze(root, &memory, options, NULL));
    c_module_t *module = create_c_module(arena, root);
    ASSERT(module->count == 2);
    for (c_module_function_t *entry = module->head; entry; entry = entry->next) {
        c_generation_result_t result = generate_c_function(entry->summary, entry->name, NULL, NULL);
        ASSERT(result.status == C_GENERATION_OK);
        const wchar_t *type = entry->summary->parameter_types[0]->type == LATTICE_INTEGER
                                  ? L"int64_t goat_p0"
                                  : L"double goat_p0";
        ASSERT(wcsstr(result.source.data, type));
        FREE_STRING(result.source);
    }
    function_summary_t changed = *module->head->summary;
    const lattice_element_t *parameter = changed.parameter_types[0];
    changed.return_type =
        parameter->type == LATTICE_INTEGER ? make_real_element() : make_integer_element();
    c_generation_result_t result = generate_c_function(&changed, module->head->name, NULL, NULL);
    ASSERT(result.status == C_GENERATION_NOT_PROVEN && !result.source.data);
    changed = *module->head->summary;
    changed.c_expressions = NULL;
    result = generate_c_function(&changed, module->head->name, NULL, NULL);
    ASSERT(result.status == C_GENERATION_NOT_PROVEN && !result.source.data);
    result = generate_c_function(module->head->summary, (string_view_t){L"goat_x;", 7}, NULL, NULL);
    ASSERT(result.status == C_GENERATION_INVALID_REQUEST && !result.source.data);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}
