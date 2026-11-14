/** @file init_snapshot.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Constructor snapshot selection and explicit reference/tracing ownership.
 */
#include "init_snapshot.h"

#include "access.h"
#include "array.h"
#include "common_methods.h"
#include "exception.h"
#include "lib/collections/avl_tree.h"
#include "lib/memory/allocate.h"
#include "vm/gc.h"

struct init_snapshot_t {
    process_t *process;
    object_t *roots;
};

typedef struct {
    object_t *object;
    size_t remaining;
} traversal_frame_t;

static void push_ancestor(vector_t *stack, avl_tree_t *seen, object_t *object) {
    set_in_avl_tree(seen, object, (value_t){.ptr = object});
    traversal_frame_t *frame = ALLOC(sizeof(*frame));
    frame->object = object;
    frame->remaining = get_inheritance_prototypes(object).size;
    append_to_vector(stack, frame);
}

init_snapshot_result_t collect_init_snapshot(process_t *process, object_t *receiver) {
    init_snapshot_result_t result = {0};
    static object_static_string_t storage;
    object_t *key = get_static_string_object(&storage, U"init");
    vector_t *stack = create_vector();
    vector_t *values = create_vector();
    avl_tree_t *seen =
        create_avl_tree((int (*)(const void *, const void *))compare_object_addresses);
    append_to_vector(values, receiver);
    push_ancestor(stack, seen, receiver);
    /* Mirror prototype lookup's right-to-left postorder, without its final reversal.
       Explicit frames avoid adding C recursion for deeply nested inheritance. */
    while (stack->size) {
        traversal_frame_t *frame = stack->data[stack->size - 1];
        if (frame->remaining) {
            object_t *parent = get_inheritance_prototypes(frame->object).items[--frame->remaining];
            if (!avl_tree_contains(seen, parent))
                push_ancestor(stack, seen, parent);
            continue;
        }
        object_t *ancestor = frame->object;
        FREE(frame);
        stack->size--;
        if (ancestor == receiver)
            continue;
        field_cell_t *cell = get_own_field_cell(ancestor, key);
        object_t *function = cell ? cell->object : get_object_property(ancestor, key);
        if (!function)
            continue;
        if (!is_function_object(function)) {
            result.exception.value = get_exception_invalid_operation();
            INCREF(result.exception.value);
            break;
        }
        append_to_vector(values, function);
    }
    if (!result.exception.value) {
        init_snapshot_t *snapshot = ALLOC(sizeof(*snapshot));
        snapshot->process = process;
        snapshot->roots = create_read_only_array_object(
            process,
            (object_array_t){(object_t **)values->data, values->size});
        add_gc_root(process, snapshot->roots);
        result.snapshot = snapshot;
    }
    for (size_t i = 0; i < stack->size; i++)
        FREE(stack->data[i]);
    destroy_vector(stack);
    destroy_vector(values);
    destroy_avl_tree(seen);
    return result;
}

object_t *get_init_snapshot_receiver(const init_snapshot_t *snapshot) {
    return get_array_elements(snapshot->roots).items[0];
}

object_array_t get_init_snapshot_functions(const init_snapshot_t *snapshot) {
    object_array_t values = get_array_elements(snapshot->roots);
    return (object_array_t){values.items + 1, values.size - 1};
}

void destroy_init_snapshot(init_snapshot_t *snapshot) {
    if (!snapshot)
        return;
    remove_gc_root(snapshot->process, snapshot->roots);
    DECREF(snapshot->roots);
    FREE(snapshot);
}
