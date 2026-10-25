/** @file test_array.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Array syntax and lifetime regressions.
 */
#include "test_array.h"

#include "analysis/analysis.h"
#include "analysis_test_support.h"
#include "cli/options.h"
#include "graph/visualization.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "model/process.h"
#include "model/thread.h"
#include "test_macro.h"
#include "vm/gc.h"

#include <stdio.h>

bool test_array_parser(void) {
    const gchar_t *valid[] = {U"[]",
                              U"[1]",
                              U"[1,]",
                              U"[[1,],[],[2,3],]",
                              U"[1+2, f(3), (4*5)]",
                              U"var a = [1,2,]",
                              U"f([1], [2,])",
                              U"[1,2][0]",
                              U"([1,2])[0]",
                              U"[[1]][0][0]",
                              U"[func(x) { return [x,] }][0](1)[0]",
                              U"[1].length",
                              U"[{ var x = [1] }][0].x[0]",
                              U"a[[1]]",
                              U"[x = 1, x = 2]",
                              U"[] == []",
                              U"![]",
                              U"func() { return [] }",
                              U"[1][0] = 2",
                              U"[1, /* last */ ]",
                              U"[1]; [2]",
                              U"var a = [1]; a[0] = []"};
    const gchar_t *invalid[] = {U"[,]",
                                U"[,1]",
                                U"[1,,]",
                                U"[1,,2]",
                                U"[1 2]",
                                U"[1;2]",
                                U"[;]",
                                U"[+]",
                                U"[return 1]",
                                U"[1",
                                U"[1)",
                                U"[1] = 2",
                                U"[1][0,]",
                                U"f(1,)",
                                U"[var a = 1]"};
    for (size_t pass = 0; pass < 2; pass++) {
        const gchar_t **texts = pass ? invalid : valid;
        size_t count = pass ? sizeof(invalid) / sizeof(*invalid) : sizeof(valid) / sizeof(*valid);
        for (size_t i = 0; i < count; i++) {
            arena_t *arena = create_arena(8);
            parser_memory_t memory = {arena, arena, arena, arena};
            node_t *root =
                parse_analysis_test_program(&memory,
                                            (string_value_t){texts[i], STRLEN(texts[i]), false});
            ASSERT(pass ? root == NULL : root != NULL);
            if (root) {
                string_value_t code = generate_goat_code_from_node(root);
                ASSERT(parse_analysis_test_program(&memory, code));
                FREE_STRING(code);
            }
            destroy_arena(arena);
        }
    }
    return true;
}

bool test_array_model(void) {
    process_t *proc = create_process();
    object_t *child = create_array_object(proc, (object_array_t){0});
    object_t *items[] = {child, child};
    object_t *array = create_array_object(proc, (object_array_t){items, 2});
    ASSERT(get_object_prototypes(array).size == 1);
    ASSERT(get_object_prototypes(array).items[0] == get_array_proto());
    ASSERT(get_object_topology(array).size == 2);
    ASSERT(get_object_topology(array).items[0] == get_array_proto());
    ASSERT(get_object_topology(array).items[1] == get_root_object());
    ASSERT(get_object_prototypes(get_array_proto()).items[0] == get_root_object());
    DECREF(child);
    add_gc_root(proc, array);
    collect_garbage(proc);
    ASSERT(get_array_elements(array).size == 2);
    ASSERT(get_array_elements(array).items[0] == child);
    object_t *index = get_integer_zero();
    object_array_t indices = {&index, 1};
    operation_result_t result = array->vtbl->get_index(proc, array, indices);
    ASSERT(!result.is_exception && result.value == child);
    DECREF(result.value);
    result = array->vtbl->set_index(proc, array, indices, child);
    ASSERT(!result.is_exception && result.value == child);
    DECREF(result.value);
    index = get_static_integer_object(-1);
    result = array->vtbl->get_index(proc, array, indices);
    ASSERT(result.is_exception && result.value == get_exception_invalid_argument());
    DECREF(result.value);
    index = get_static_integer_object(2);
    result = array->vtbl->set_index(proc, array, indices, child);
    ASSERT(result.is_exception);
    DECREF(result.value);
    index = get_integer_zero();
    result = array->vtbl->set_index(proc, array, indices, array);
    DECREF(result.value);
    object_t *copy = clone_object(proc, array);
    ASSERT(copy != array && get_array_elements(copy).items[0] == copy);
    ASSERT(get_array_elements(copy).items[1] == child);
    add_gc_root(proc, copy);
    DECREF(array);
    remove_gc_root(proc, array);
    collect_garbage(proc);
    ASSERT(get_array_elements(copy).items[0] == copy);
    string_value_t notation = convert_object_to_string_notation(copy);
    ASSERT(STRCMP(notation.data, U"[[...],[ ]]") == 0);
    FREE_STRING(notation);
    remove_gc_root(proc, copy);
    DECREF(copy);
    collect_garbage(proc);
    size_t baseline = proc->objects.size;
    for (size_t i = 0; i < 100; i++) {
        object_t *root = get_root_object();
        object_t *dict = create_user_defined_object(proc, (object_array_t){&root, 1});
        array = create_array_object(proc, (object_array_t){&dict, 1});
        ASSERT(create_object_property(dict, array, array, false) == MSTAT_OK);
        add_gc_root(proc, array);
        DECREF(dict);
        DECREF(array);
        collect_garbage(proc);
        ASSERT(get_array_elements(array).items[0] == dict);
        notation = convert_object_to_string_notation(array);
        ASSERT(STRCMP(notation.data, U"[{[...]:[...]}]") == 0);
        FREE_STRING(notation);
        remove_gc_root(proc, array);
        collect_garbage(proc);
        ASSERT(proc->objects.size == baseline);
    }
    /* A dynamic length object must be retained and marked, not returned as a new reference. */
    object_t *many[256];
    for (size_t i = 0; i < 256; i++)
        many[i] = get_null_object();
    array = create_array_object(proc, (object_array_t){many, 256});
    add_gc_root(proc, array);
    collect_garbage(proc);
    object_t *length = get_object_property(array, get_string_length());
    ASSERT(get_object_integer_value(length).value == 256);
    ASSERT(get_object_property(array, get_string_length()) == length);
    remove_gc_root(proc, array);
    DECREF(array);
    destroy_process(proc);
    return true;
}

bool test_array_graph_labels(void) {
    arena_t *arena = create_arena(16);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root = parse_analysis_test_program(
        &memory,
        STATIC_STRING(U"var obj = { var x = [1] }; println(obj.x[0]);"));
    ASSERT(root);
    options_t *options = create_options();
    options->optimization_level = OPTIMIZATION_NONE;
    ASSERT(!analyze(root, &memory, options, NULL));
    string_value_t dot = generate_graph_dot(root);
    ASSERT(STRSTR(dot.data, U"member access"));
    ASSERT(STRSTR(dot.data, U"index access"));
    ASSERT(STRSTR(dot.data, U"statement list"));
    ASSERT(!STRSTR(dot.data, U"member_access"));
    ASSERT(!STRSTR(dot.data, U"index_access"));
    ASSERT(!STRSTR(dot.data, U"statement_list"));
    FREE_STRING(dot);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}

/** @brief Calls an Array method while preserving the caller's references. */
static operation_result_t
array_call(process_t *proc, object_t *array, const gchar_t *name, object_t **args, size_t count) {
    object_t *key = create_string_object(proc, (string_value_t){name, STRLEN(name), false});
    object_t *method = get_object_property(get_array_proto(), key);
    DECREF(key);
    for (size_t i = 0; i < count; i++) {
        INCREF(args[i]);
        push_object_onto_stack(proc->main_thread->data_stack, args[i]);
    }
    if (!call_object(method, (uint16_t)count, array, proc->main_thread)) {
        object_t *error = proc->main_thread->exception.value;
        proc->main_thread->exception.value = NULL;
        clear_exception(&proc->main_thread->exception);
        return operation_exception(error);
    }
    return operation_success(pop_object_from_stack(proc->main_thread->data_stack));
}

/** @brief Custom comparison proves dispatch and ownership of dynamically allocated exceptions. */
static operation_result_t comparison_error(process_t *proc, object_t *a, object_t *b) {
    return operation_exception(create_string_object(proc, STATIC_STRING(U"comparison failed")));
}

bool test_array_methods(void) {
    process_t *proc = create_process();
    object_t *array = create_array_object(proc, (object_array_t){0});
    add_gc_root(proc, array);
    collect_garbage(proc);
    size_t baseline = proc->objects.size;
    for (size_t pass = 0; pass < 50; pass++) {
        for (size_t i = 0; i < 256; i++) {
            object_t *child = create_array_object(proc, (object_array_t){&array, 1});
            operation_result_t result = array_call(proc, array, U"push", &child, 1);
            ASSERT(!result.is_exception
                   && get_object_integer_value(result.value).value == (int64_t)i + 1);
            DECREF(result.value);
            DECREF(child);
        }
        collect_garbage(proc);
        ASSERT(get_array_elements(array).size == 256);
        ASSERT(get_array_elements(get_array_elements(array).items[0]).items[0] == array);
        object_t *old_length = get_object_property(array, get_string_length());
        INCREF(old_length);
        operation_result_t result = array_call(proc, array, U"clear", NULL, 0);
        ASSERT(!result.is_exception && result.value == array);
        DECREF(result.value);
        ASSERT(get_object_integer_value(old_length).value == 256);
        DECREF(old_length);
        collect_garbage(proc);
        ASSERT(proc->objects.size == baseline);
    }
    object_t *child = create_array_object(proc, (object_array_t){0});
    operation_result_t result = array_call(proc, array, U"push", &child, 1);
    DECREF(result.value);
    DECREF(child);
    result = array_call(proc, array, U"slice", NULL, 0);
    ASSERT(!result.is_exception);
    object_t *slice = result.value;
    add_gc_root(proc, slice);
    result = array_call(proc, array, U"clear", NULL, 0);
    DECREF(result.value);
    collect_garbage(proc);
    ASSERT(get_array_elements(slice).items[0] == child);
    result = array_call(proc, slice, U"pop", NULL, 0);
    ASSERT(!result.is_exception && result.value == child);
    ASSERT(get_array_elements(result.value).size == 0);
    DECREF(result.value);
    remove_gc_root(proc, slice);
    DECREF(slice);
    /* Every comparison failure must propagate without changing order or losing elements. */
    object_t *root = get_root_object();
    object_t *first = create_user_defined_object(proc, (object_array_t){&root, 1});
    object_t *second = create_user_defined_object(proc, (object_array_t){&root, 1});
    object_vtbl_t vtbl = *first->vtbl;
    vtbl.less = comparison_error;
    vtbl.equal = comparison_error;
    first->vtbl = &vtbl;
    second->vtbl = &vtbl;
    object_t *items[] = {first, second};
    result = array_call(proc, array, U"push", items, 2);
    DECREF(result.value);
    result = array_call(proc, array, U"sort", NULL, 0);
    ASSERT(result.is_exception);
    string_value_t error = convert_object_to_string(result.value);
    ASSERT(!STRCMP(error.data, U"comparison failed"));
    FREE_STRING(error);
    DECREF(result.value);
    ASSERT(get_array_elements(array).items[0] == first
           && get_array_elements(array).items[1] == second);
    result = array_call(proc, array, U"indexOf", &first, 1);
    ASSERT(result.is_exception);
    DECREF(result.value);
    result = array_call(proc, array, U"lastIndexOf", &first, 1);
    ASSERT(result.is_exception);
    DECREF(result.value);
    DECREF(first);
    DECREF(second);
    remove_gc_root(proc, array);
    DECREF(array);
    destroy_process(proc);
    return true;
}
