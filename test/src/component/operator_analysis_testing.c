/** @file operator_analysis_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Conservative operator analysis, without enabling VM operator dispatch (#495).
 */
#ifdef NDEBUG
#    undef NDEBUG
#endif
#include "analysis/analysis.h"
#include "analysis/function_summary.h"
#include "analysis/simplification.h"
#include "cli/options.h"
#include "codegen/linker.h"
#include "common/compilation_error.h"
#include "graph/expression.h"
#include "lib/memory/allocate.h"
#include "lib/strings/string_ext.h"
#include "model/process.h"
#include "model/thread.h"
#include "unit/analysis_test_support.h"
#include "vm/vm.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    arena_t *arena;
    node_t *root;
    analysis_collector_t *events;
    size_t diagnostic_count;
} fixture_t;

static fixture_t analyze_source(const gchar_t *source, bool optimized) {
    fixture_t f = {.arena = create_arena(8)};
    parser_memory_t memory = {f.arena, f.arena, f.arena, f.arena};
    f.root = parse_analysis_test_program(&memory, (string_value_t){source, STRLEN(source), false});
    assert(f.root);
    options_t *options = create_options();
    options->optimization_level = optimized ? OPTIMIZATION_ALL : OPTIMIZATION_NONE;
    f.events = create_analysis_collector(f.arena);
    compilation_error_t *errors = analyze(f.root, &memory, options, f.events);
    for (compilation_error_t *e = errors; e; e = e->next) {
        ++f.diagnostic_count;
        assert(e->severity == WARNING);
    }
    destroy_options(options);
    return f;
}

static const analysis_event_t *write_at(fixture_t *f, size_t row) {
    analysis_event_query_t q = {.kind = ANALYSIS_VALUE_WRITE, .row = row};
    const analysis_event_t *e = find_last_analysis_event(f->events, &q);
    assert(e && e->value);
    return e;
}

static const analysis_event_t *flags_for(fixture_t *f, node_type_t type) {
    const analysis_event_t *found = NULL;
    for (const analysis_event_t *e = f->events->head; e; e = e->next)
        if (e->kind == ANALYSIS_NODE_FLAGS && e->node->vtbl->type == type)
            found = e;
    assert(found);
    return found;
}

static void conservative_case(void) {
    const struct {
        const gchar_t *expression;
        node_type_t node;
        lattice_type_t result;
    } cases[] = {{U"a + 1", NODE_ADDITION, LATTICE_TOP},
                 {U"a - 1", NODE_SUBTRACTION, LATTICE_TOP},
                 {U"a * 2", NODE_MULTIPLICATION, LATTICE_TOP},
                 {U"a / 2", NODE_DIVISION, LATTICE_TOP},
                 {U"a % 2", NODE_MODULO, LATTICE_TOP},
                 {U"a ** 2", NODE_POWER, LATTICE_TOP},
                 {U"a & 1", NODE_BITWISE_AND, LATTICE_TOP},
                 {U"a | 1", NODE_BITWISE_OR, LATTICE_TOP},
                 {U"a ^ 1", NODE_BITWISE_XOR, LATTICE_TOP},
                 {U"a << 1", NODE_SHIFT_LEFT, LATTICE_TOP},
                 {U"a >> 1", NODE_SHIFT_RIGHT, LATTICE_TOP},
                 {U"+a", NODE_UNARY_PLUS, LATTICE_TOP},
                 {U"-a", NODE_UNARY_MINUS, LATTICE_TOP},
                 {U"~a", NODE_BITWISE_NOT, LATTICE_TOP},
                 {U"!a", NODE_LOGICAL_NOT, LATTICE_BOOLEAN},
                 {U"a == a", NODE_EQUAL, LATTICE_BOOLEAN},
                 {U"a != a", NODE_NOT_EQUAL, LATTICE_BOOLEAN},
                 {U"a < 1", NODE_LESS, LATTICE_BOOLEAN},
                 {U"a <= 1", NODE_LESS_OR_EQUAL, LATTICE_BOOLEAN},
                 {U"a > 1", NODE_GREATER, LATTICE_BOOLEAN},
                 {U"a >= 1", NODE_GREATER_OR_EQUAL, LATTICE_BOOLEAN},
                 {U"a && true", NODE_LOGICAL_AND, LATTICE_TOP},
                 {U"a || false", NODE_LOGICAL_OR, LATTICE_TOP}};

    for (int unknown = 0; unknown < 2; ++unknown)
        for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
            string_value_t source = format_string(
                U"const a = %s;\nvar captured = 7;\nvar result = %s;\nvar after = captured;",
                unknown ? U"pi" : U"{}",
                cases[i].expression);
            fixture_t f = analyze_source(source.data, true);

            assert(write_at(&f, 3)->value->type == cases[i].result);
            assert(write_at(&f, 4)->value->type == LATTICE_TOP);
            const analysis_event_t *flags = flags_for(&f, cases[i].node);
            assert(!(flags->flags & NODE_FLAG_PURE));
            assert(!can_discard_expression(flags->node));
            destroy_arena(f.arena);
            FREE_STRING(source);
        }
}

static void logical_case(void) {
    /* A currently truthy object may acquire a logical handler. RHS must stay in
     * the analyzed graph even though today's VM still uses builtin truthiness. */
    fixture_t f =
        analyze_source(U"const a = {};\nvar x = 0;\nvar result = a || ++x;\nvar after = x;", true);
    assert(write_at(&f, 3)->value->type == LATTICE_TOP);
    const analysis_event_t *rhs = flags_for(&f, NODE_PREFIX_INCREMENT);
    assert(!(rhs->flags & NODE_FLAG_UNREACHABLE));
    assert(!(flags_for(&f, NODE_LOGICAL_OR)->flags & NODE_FLAG_PURE));
    destroy_arena(f.arena);
}

static void update_case(void) {
    const gchar_t *expressions[] = {U"a += 1", U"++a", U"a++", U"--a", U"a--"};
    const node_type_t types[] = {NODE_COMPOUND_ASSIGNMENT,
                                 NODE_PREFIX_INCREMENT,
                                 NODE_POSTFIX_INCREMENT,
                                 NODE_PREFIX_DECREMENT,
                                 NODE_POSTFIX_DECREMENT};
    for (size_t i = 0; i < sizeof(types) / sizeof(*types); ++i) {
        string_value_t source = format_string(U"var a = {};\nvar result = %s;", expressions[i]);
        fixture_t f = analyze_source(source.data, true);
        const analysis_event_t *e = flags_for(&f, types[i]);
        assert(!(e->flags & NODE_FLAG_PURE));
        assert(!can_discard_expression(e->node));
        assert(write_at(&f, 2)->value->type == LATTICE_TOP
               || write_at(&f, 2)->value->type == LATTICE_USER_DEFINED_OBJECT);
        destroy_arena(f.arena);
        FREE_STRING(source);
    }
}

static void builtin_case(void) {
    assert(lattice_may_dispatch_operator(NULL));
    assert(lattice_may_dispatch_operator(make_top_element()));
    assert(lattice_may_dispatch_operator(make_not_null_element()));
    assert(lattice_may_dispatch_operator(make_user_defined_object_element()));
    const lattice_element_t *builtins[] = {make_bottom_element(),
                                           make_null_element(),
                                           make_boolean_element(),
                                           make_integer_element(),
                                           make_real_element(),
                                           make_numeric_element(),
                                           make_string_element(),
                                           make_function_element(),
                                           make_array_element()};
    for (size_t i = 0; i < sizeof(builtins) / sizeof(*builtins); ++i)
        assert(!lattice_may_dispatch_operator(builtins[i]));
    fixture_t f = analyze_source(U"const a={};\nvar result=!!a;", true);
    assert(write_at(&f, 2)->value->type == LATTICE_BOOLEAN);
    assert(flags_for(&f, NODE_BOOLEAN_CONVERSION)->flags & NODE_FLAG_PURE);
    destroy_arena(f.arena);
    f = analyze_source(U"const f=func(x){return x*2+1;};\nvar result=f(3);", true);
    analysis_event_query_t q = {.kind = ANALYSIS_FUNCTION_SUMMARY};
    const analysis_event_t *e = find_last_analysis_event(f.events, &q);
    assert(e && e->function_summary);
    assert(e->function_summary->effects == FUNCTION_EFFECT_NONE);
    assert(e->function_summary->c_support == FUNCTION_C_SUPPORTED);
    assert(e->function_summary->return_type->type == LATTICE_INTEGER);
    destroy_arena(f.arena);
    f = analyze_source(U"if(false){const a={}; a+1; true+1;} var result=1;", true);
    assert(f.diagnostic_count == 0);
    destroy_arena(f.arena);
}

static void mutation_case(void) {
    fixture_t f = analyze_source(U"const a={}; const alias=a;\n"
                                 U"a[\"+\"]=func(other){return 1;};\n"
                                 U"var first=a+1;\n"
                                 U"alias[\"+\"]=func(other){return \"changed\";};\n"
                                 U"var second=a+1;",
                                 true);
    assert(write_at(&f, 3)->value->type == LATTICE_TOP);
    assert(write_at(&f, 5)->value->type == LATTICE_TOP);
    assert(!(flags_for(&f, NODE_ADDITION)->flags & NODE_FLAG_PURE));
    destroy_arena(f.arena);
    f = analyze_source(U"const f=func(a){return a+1;};\nvar result=f({});", true);
    analysis_event_query_t q = {.kind = ANALYSIS_FUNCTION_SUMMARY};
    const analysis_event_t *e = find_last_analysis_event(f.events, &q);
    assert(e && e->function_summary);
    assert(e->function_summary->effects & FUNCTION_EFFECT_UNKNOWN);
    assert(e->function_summary->return_type->type == LATTICE_TOP);
    assert(e->function_summary->c_support != FUNCTION_C_SUPPORTED);
    destroy_arena(f.arena);
}

static void polymorphic_case(void) {
    fixture_t f =
        analyze_source(U"const f=func(a){return a+1;};\nvar numeric=f(3);\nvar object=f({});",
                       true);
    bool numeric = false, object = false;
    for (const analysis_event_t *e = f.events->head; e; e = e->next) {
        if (e->kind != ANALYSIS_FUNCTION_SUMMARY || !e->function_summary)
            continue;
        const function_summary_t *summary = e->function_summary;
        assert(summary->parameter_count == 1);
        if (summary->parameter_types[0]->type == LATTICE_INTEGER) {
            numeric = true;
            assert(summary->effects == FUNCTION_EFFECT_NONE);
            assert(summary->c_support == FUNCTION_C_SUPPORTED);
        } else if (summary->parameter_types[0]->type == LATTICE_USER_DEFINED_OBJECT) {
            object = true;
            assert(summary->effects & FUNCTION_EFFECT_UNKNOWN);
            assert(summary->c_support != FUNCTION_C_SUPPORTED);
        }
    }
    assert(numeric && object);
    destroy_arena(f.arena);
}

static void switch_case(void) {
    fixture_t f =
        analyze_source(U"const a={};\nvar x=7;\nswitch(a){case 1: ;}\nvar after=x;", true);
    assert(write_at(&f, 4)->value->type == LATTICE_TOP);
    destroy_arena(f.arena);
    f = analyze_source(U"var x=7;\nswitch(1){case 1: x=9; default: x=3;}\nvar after=x;", true);
    assert(write_at(&f, 3)->value->type == LATTICE_INTEGER_CONSTANT);
    assert(((const integer_constant_element_t *)write_at(&f, 3)->value)->value == 9);
    destroy_arena(f.arena);
}

static void deferred(node_t *node, code_builder_t *code, data_builder_t *data) {
    if (node->vtbl->type == NODE_FUNCTION_OBJECT)
        generate_deferred_bytecode_from_node(node, code, data);
    for (size_t i = 0; i < get_node_child_count(node); ++i)
        deferred(get_node_child(node, i), code, data);
}

static void runtime_program(const gchar_t *source) {
    string_value_t checked = format_string(U"%s\nif (!result) {throw \"test failed\";}", source);
    for (int optimized = 0; optimized < 2; ++optimized) {
        fixture_t f = analyze_source(checked.data, optimized != 0);
        code_builder_t *builder = create_code_builder();
        data_builder_t *data = create_data_builder();
        generate_bytecode_from_node(f.root, builder, data);
        deferred(f.root, builder, data);
        bytecode_t *code = link_code_and_data(builder, data);
        process_t *p = create_process();
        assert(run(p, code) == 0);

        destroy_process(p);
        free_bytecode(code);
        destroy_code_builder(builder);
        destroy_data_builder(data);
        destroy_arena(f.arena);
    }
    FREE_STRING(checked);
}

static void runtime_case(void) {
    /* INVALID_OPERATION remains catchable, not a fatal compile-time diagnostic.
     * No user operator execution is expected until the later VM issues. */
    runtime_program(U"var result=false; try { const a={}; a+1; } "
                    U"catch(e) {result=e==Exceptions.INVALID_OPERATION;}");
    runtime_program(U"var result=false; try {true+1;} "
                    U"catch(e) {result=e==Exceptions.INVALID_OPERATION;}");
    runtime_program(U"var result=false; var x=0; false&&++x; true||++x; "
                    U"result=x==0 && 2+3*4==14 && (12&10)==8;");
    runtime_program(U"var result=false; var x=2; x+=3; const old=x++; "
                    U"result=old==5 && x==6 && ++x==7;");
    runtime_program(U"var result=false; if(false) {const a={}; a+1;} result=true;");
}

int main(int argc, char **argv) {
    assert(argc == 2);
    if (!strcmp(argv[1], "conservative"))
        conservative_case();
    else if (!strcmp(argv[1], "logical"))
        logical_case();
    else if (!strcmp(argv[1], "update"))
        update_case();
    else if (!strcmp(argv[1], "runtime"))
        runtime_case();
    else if (!strcmp(argv[1], "builtin"))
        builtin_case();
    else if (!strcmp(argv[1], "polymorphic"))
        polymorphic_case();
    else if (!strcmp(argv[1], "switch"))
        switch_case();
    else if (!strcmp(argv[1], "mutation"))
        mutation_case();
    else
        assert(!"unknown operator analysis case");
    return 0;
}
