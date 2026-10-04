/** @file test_builtin_functions.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Compare native abstract executors with runtime calls.
 */
#include "test_builtin_functions.h"

#include "analysis/function_call.h"
#include "analysis/lattice.h"
#include "builtins/registry.h"
#include "graph/declarations.h"
#include "model/context.h"
#include "model/process.h"
#include "model/thread.h"
#include "test_macro.h"

#include <math.h>
#include <stdio.h>

static const builtin_function_t *lookup(const wchar_t *name) {
    return find_builtin_function((string_view_t){name, wcslen(name)});
}

bool test_builtin_registry() {
    size_t count;
    const builtin_function_t *const *functions = get_builtin_functions(&count);
    ASSERT(count == 33);
    ASSERT(get_object_keys(get_root_context()->data).size == count + 2);
    for (size_t i = 0; i < count; i++) {
        const builtin_function_t *f = functions[i];
        ASSERT(f && f->execute && f->interpret && f->get_object && f->name);
        ASSERT(lookup(f->name) == f);
        process_t *proc = create_process();
        object_t *key =
            create_string_object(proc, (string_value_t){f->name, wcslen(f->name), false});
        ASSERT(get_object_property(get_root_context()->data, key) == f->get_object());
        DECREF(key);
        destroy_process(proc);
        ASSERT(f->effects
               == ((!wcscmp(f->name, L"print") || !wcscmp(f->name, L"println"))
                       ? BUILTIN_EFFECT_OUTPUT
                   : !wcscmp(f->name, L"input") ? BUILTIN_EFFECT_INPUT
                                                : BUILTIN_EFFECT_NONE));
    }
    ASSERT(!lookup(L"pi") && !lookup(L"Exceptions") && !lookup(L"sig") && !lookup(L"signx"));
    const wchar_t raw[] = {L's', L'i', L'g', L'n', L'x'};
    ASSERT(find_builtin_function((string_view_t){raw, 4}) == lookup(L"sign"));
    arena_t *arena = create_arena(8);
    const lattice_element_t *a = make_builtin_function_element(arena, lookup(L"sign"));
    const lattice_element_t *b = make_builtin_function_element(arena, lookup(L"sqrt"));
    const lattice_element_t *same = make_builtin_function_element(arena, lookup(L"sign"));
    ASSERT(lattice_join(arena, a, same) == a);
    ASSERT(lattice_join(arena, a, b)->type == LATTICE_FUNCTION);
    ASSERT(lattice_meet(arena, a, b)->type == LATTICE_BOTTOM);
    ASSERT(lattice_meet(arena, a, make_function_element()) == a);
    destroy_arena(arena);
    return true;
}

typedef struct {
    bool integer;
    int64_t i;
    double r;
} sample_t;

bool test_builtin_numeric_results() {
    sample_t samples[] = {{true, INT64_MIN},
                          {true, INT64_MAX},
                          {true, -4},
                          {true, 0},
                          {true, 9},
                          {false, 0, -0.0},
                          {false, 0, 0.25},
                          {false, 0, -2.0},
                          {false, 0, 0x1p63},
                          {false, 0, -1.5},
                          {false, 0, INFINITY},
                          {false, 0, -INFINITY},
                          {false, 0, NAN}};
    size_t total;
    const builtin_function_t *const *functions = get_builtin_functions(&total);
    for (size_t f = 0; f < total; f++) {
        const builtin_function_t *descriptor = functions[f];
        if (descriptor == &builtin_print || descriptor == &builtin_println
            || descriptor == &builtin_input || descriptor == &builtin_int)
            continue;
        for (size_t i = 0; i < sizeof(samples) / sizeof(*samples); i++) {
            for (size_t j = 0;
                 j < (descriptor->min_args == 2 ? sizeof(samples) / sizeof(*samples) : 1);
                 j++) {
                arena_t *arena = create_arena(8);
                abstract_state_t *state = create_abstract_state(arena);
                process_t *proc = create_process();
                sample_t selected[] = {samples[i], samples[j]};
                const lattice_element_t *args[2];
                object_t *objects[2];
                for (size_t k = 0; k < descriptor->min_args; k++) {
                    args[k] = selected[k].integer
                                  ? make_integer_constant_element(arena, selected[k].i)
                                  : make_real_constant_element(arena, selected[k].r);
                    objects[k] = selected[k].integer
                                     ? create_integer_object(proc, selected[k].i)
                                     : create_real_number_object(proc, selected[k].r);
                }
                for (size_t k = descriptor->min_args; k > 0; k--)
                    push_object_onto_stack(proc->main_thread->data_stack, objects[k - 1]);
                const lattice_element_t *value =
                    interpret_function_call(make_builtin_function_element(arena, descriptor),
                                            args,
                                            descriptor->min_args,
                                            state);
                ASSERT(
                    call_object(descriptor->get_object(), descriptor->min_args, proc->main_thread));
                ASSERT(!proc->main_thread->exception.value
                       && proc->main_thread->data_stack->size == 1);
                object_t *actual = pop_object_from_stack(proc->main_thread->data_stack);
                if (descriptor == &builtin_sign
                    || (descriptor == &builtin_abs && selected[0].integer)) {
                    ASSERT(is_integer_object(actual));
                    ASSERT(value->type == LATTICE_INTEGER_CONSTANT);
                    ASSERT(get_object_integer_value(actual).value
                           == ((const integer_constant_element_t *)value)->value);
                } else {
                    ASSERT(!is_integer_object(actual));
                    ASSERT(value->type == LATTICE_REAL_CONSTANT);
                    double expected = ((const real_constant_element_t *)value)->value;
                    double number = get_object_real_value(actual).value;
                    ASSERT((isnan(expected) && isnan(number)) || expected == number);
                    if (expected == 0) {
                        ASSERT(!!signbit(expected) == !!signbit(number));
                    }
                }
                DECREF(actual);
                destroy_process(proc);
                destroy_abstract_state(state);
                destroy_arena(arena);
            }
        }
    }
    return true;
}

bool test_builtin_errors() {
    size_t count;
    const builtin_function_t *const *functions = get_builtin_functions(&count);
    for (size_t i = 0; i < count; i++) {
        for (size_t supplied = 0; supplied < functions[i]->min_args; supplied++) {
            process_t *proc = create_process();
            arena_t *arena = create_arena(8);
            abstract_state_t *state = create_abstract_state(arena);
            const lattice_element_t *args[] = {make_integer_constant_element(arena, 1)};
            if (supplied)
                push_object_onto_stack(proc->main_thread->data_stack,
                                       create_integer_object(proc, 1));
            ASSERT(!call_object(functions[i]->get_object(), supplied, proc->main_thread));
            ASSERT(proc->main_thread->exception.value == get_exception_invalid_argument());
            ASSERT(proc->main_thread->data_stack->size == 0);
            ASSERT(interpret_function_call(make_builtin_function_element(arena, functions[i]),
                                           args,
                                           supplied,
                                           state)
                       ->type
                   == LATTICE_BOTTOM);
            ASSERT(state->control_flow == FLOW_UNREACHABLE);
            destroy_abstract_state(state);
            destroy_arena(arena);
            destroy_process(proc);
        }
    }
    for (size_t i = 0; i < count; i++) {
        const builtin_function_t *f = functions[i];
        if (f == &builtin_print || f == &builtin_println || f == &builtin_input
            || f == &builtin_int)
            continue;
        for (size_t bad = 0; bad < f->min_args; bad++) {
            for (int kind = 0; kind < 5; kind++) {
                process_t *proc = create_process();
                object_t *root = get_root_object();
                object_t *wrong =
                    kind == 0   ? get_null_object()
                    : kind == 1 ? get_boolean_object(true)
                    : kind == 2 ? create_string_object(proc, (string_value_t){L"4", 1, false})
                    : kind == 3 ? get_function_sign()
                                : create_user_defined_object(proc, (object_array_t){&root, 1});
                for (size_t k = f->min_args; k > 0; k--)
                    push_object_onto_stack(proc->main_thread->data_stack,
                                           k - 1 == bad ? wrong : create_integer_object(proc, 1));
                ASSERT(!call_object(f->get_object(), f->min_args, proc->main_thread));
                ASSERT(proc->main_thread->exception.value == get_exception_invalid_argument());
                ASSERT(proc->main_thread->data_stack->size == 0);
                destroy_process(proc);
            }
        }
    }
    return true;
}

bool test_builtin_domains() {
    arena_t *arena = create_arena(8);
    abstract_state_t *state = create_abstract_state(arena);
    const lattice_element_t *sign = make_builtin_function_element(arena, lookup(L"sign"));
    const lattice_element_t *sqrt_value = make_builtin_function_element(arena, lookup(L"sqrt"));
    const lattice_element_t *args[] = {make_integer_range_element(arena, -9, -1),
                                       make_top_element()};
    const lattice_element_t *value = interpret_function_call(sign, args, 1, state);
    ASSERT(value->type == LATTICE_INTEGER_CONSTANT
           && ((const integer_constant_element_t *)value)->value == -1);
    args[0] = make_integer_range_element(arena, 0, 7);
    value = interpret_function_call(sign, args, 1, state);
    ASSERT(value->type == LATTICE_INTEGER_RANGE);
    ASSERT(((const integer_range_element_t *)value)->min == 0
           && ((const integer_range_element_t *)value)->max == 1);
    args[0] = make_top_element();
    ASSERT(interpret_function_call(sqrt_value, args, 1, state)->type == LATTICE_REAL);
    ASSERT(interpret_function_call(make_builtin_function_element(arena, lookup(L"atan")),
                                   args,
                                   2,
                                   state)
               ->type
           == LATTICE_REAL);
    ASSERT(!state->builtin_bindings_unknown);
    declarator_t sentinel = {.name = {L"sentinel", 8}};
    const lattice_element_t *saved = make_integer_constant_element(arena, 17);
    set_in_abstract_state(state, &sentinel, saved);
    const builtin_function_t *printer = lookup(L"print");
    ASSERT(
        interpret_function_call(make_builtin_function_element(arena, printer), args, 1, state)->type
        == LATTICE_NULL);
    ASSERT(get_from_abstract_state(state, &sentinel) == saved);
    ASSERT(interpret_function_call(make_builtin_function_element(arena, lookup(L"println")),
                                   args,
                                   1,
                                   state)
               ->type
           == LATTICE_NULL);
    ASSERT(get_from_abstract_state(state, &sentinel) == saved);
    ASSERT(!state->builtin_bindings_unknown);
    builtin_function_t mutating = *printer;
    mutating.effects = BUILTIN_EFFECT_BINDINGS;
    ASSERT(interpret_function_call(make_builtin_function_element(arena, &mutating), args, 1, state)
               ->type
           == LATTICE_NULL);
    ASSERT(get_from_abstract_state(state, &sentinel)->type == LATTICE_TOP);
    ASSERT(state->builtin_bindings_unknown);
    args[0] = make_string_element();
    ASSERT(interpret_function_call(sign, args, 1, state)->type == LATTICE_BOTTOM);
    ASSERT(state->control_flow == FLOW_UNREACHABLE);
    destroy_abstract_state(state);
    destroy_arena(arena);
    return true;
}

bool test_abs_domains() {
    arena_t *arena = create_arena(8);
    abstract_state_t *state = create_abstract_state(arena);
    const lattice_element_t *args[] = {NULL};
    const lattice_element_t *domains[] = {make_integer_element(),
                                          make_integer_range_element(arena, -9, 4),
                                          make_real_element(),
                                          make_numeric_element(),
                                          make_top_element(),
                                          make_not_null_element(),
                                          make_true_element(),
                                          make_string_element(),
                                          make_null_element(),
                                          make_bottom_element()};
    const lattice_type_t expected[] = {LATTICE_INTEGER,
                                       LATTICE_INTEGER,
                                       LATTICE_REAL,
                                       LATTICE_NUMERIC,
                                       LATTICE_NUMERIC,
                                       LATTICE_NUMERIC,
                                       LATTICE_BOTTOM,
                                       LATTICE_BOTTOM,
                                       LATTICE_BOTTOM,
                                       LATTICE_BOTTOM};
    for (size_t i = 0; i < sizeof(domains) / sizeof(*domains); i++) {
        args[0] = domains[i];
        ASSERT(builtin_abs.interpret(state, args, 1)->type == expected[i]);
    }
    const int64_t inputs[] = {INT64_MIN, INT64_MAX, -9007199254740993LL, -7, 0, 7};
    const int64_t results[] = {INT64_MIN, INT64_MAX, 9007199254740993LL, 7, 0, 7};
    for (size_t i = 0; i < sizeof(inputs) / sizeof(*inputs); i++) {
        args[0] = make_integer_constant_element(arena, inputs[i]);
        const lattice_element_t *value = builtin_abs.interpret(state, args, 1);
        ASSERT(value->type == LATTICE_INTEGER_CONSTANT);
        ASSERT(((const integer_constant_element_t *)value)->value == results[i]);
    }
    destroy_abstract_state(state);
    destroy_arena(arena);
    return true;
}
