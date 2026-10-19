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
#include "test_macro.h"
#include "vm/gc.h"

#include <stdio.h>

bool test_array_parser(void) {
    const wchar_t *valid[] = {L"[]",
                              L"[1]",
                              L"[1,]",
                              L"[[1,],[],[2,3],]",
                              L"[1+2, f(3), (4*5)]",
                              L"var a = [1,2,]",
                              L"f([1], [2,])",
                              L"[1,2][0]",
                              L"([1,2])[0]",
                              L"[[1]][0][0]",
                              L"[func(x) { return [x,] }][0](1)[0]",
                              L"[1].length",
                              L"[{ var x = [1] }][0].x[0]",
                              L"a[[1]]",
                              L"[x = 1, x = 2]",
                              L"[] == []",
                              L"![]",
                              L"func() { return [] }",
                              L"[1][0] = 2",
                              L"[1, /* last */ ]",
                              L"[1]; [2]",
                              L"var a = [1]; a[0] = []"};
    const wchar_t *invalid[] = {L"[,]",
                                L"[,1]",
                                L"[1,,]",
                                L"[1,,2]",
                                L"[1 2]",
                                L"[1;2]",
                                L"[;]",
                                L"[+]",
                                L"[return 1]",
                                L"[1",
                                L"[1)",
                                L"[1] = 2",
                                L"[1][0,]",
                                L"f(1,)",
                                L"[var a = 1]"};
    for (size_t pass = 0; pass < 2; pass++) {
        const wchar_t **texts = pass ? invalid : valid;
        size_t count = pass ? sizeof(invalid) / sizeof(*invalid) : sizeof(valid) / sizeof(*valid);
        for (size_t i = 0; i < count; i++) {
            arena_t *arena = create_arena(8);
            parser_memory_t memory = {arena, arena, arena, arena};
            node_t *root =
                parse_analysis_test_program(&memory,
                                            (string_value_t){texts[i], wcslen(texts[i]), false});
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
    ASSERT(wcscmp(notation.data, L"[[...],[ ]]") == 0);
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
        ASSERT(wcscmp(notation.data, L"[{[...]:[...]}]") == 0);
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
        STATIC_STRING(L"var obj = { var x = [1] }; println(obj.x[0]);"));
    ASSERT(root);
    options_t *options = create_options();
    options->optimization_level = OPTIMIZATION_NONE;
    ASSERT(!analyze(root, &memory, options, NULL));
    string_value_t dot = generate_graph_dot(root);
    ASSERT(wcsstr(dot.data, L"member access"));
    ASSERT(wcsstr(dot.data, L"index access"));
    ASSERT(wcsstr(dot.data, L"statement list"));
    ASSERT(!wcsstr(dot.data, L"member_access"));
    ASSERT(!wcsstr(dot.data, L"index_access"));
    ASSERT(!wcsstr(dot.data, L"statement_list"));
    FREE_STRING(dot);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}
