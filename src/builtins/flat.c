/** @file flat.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Shallow mutable property materialization in ordinary lookup order.
 */
#include "analysis/lattice.h"
#include "model/builtin_function.h"
#include "model/thread.h"

static void copy_properties(object_t *target, object_t *source) {
    if (source == get_root_object())
        return;
    object_array_t keys = get_object_keys(source);
    for (size_t i = 0; i < keys.size; i++) {
        object_t *key = keys.items[i];
        if (!get_object_property(target, key))
            create_object_property(target, key, get_object_property(source, key), false);
    }
}

static object_t *flatten(process_t *process, object_t *source) {
    if (source->vtbl->type != TYPE_USER_DEFINED_OBJECT) {
        INCREF(source);
        return source;
    }
    object_t *parent = get_root_object();
    object_t *target = create_user_defined_object(process, (object_array_t){&parent, 1});
    copy_properties(target, source);
    object_array_t topology = get_object_topology(source);
    for (size_t i = 0; i < topology.size; i++)
        copy_properties(target, topology.items[i]);
    return target;
}

static operation_result_t
execute_method(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    return operation_success(flatten(thread->process, receiver));
}

static operation_result_t execute(object_t **args, uint16_t count, thread_t *thread) {
    return count ? operation_success(flatten(thread->process, args[0]))
                 : operation_exception(get_exception_invalid_argument());
}

static const lattice_element_t *
interpret(abstract_state_t *state, const lattice_element_t *const *args, size_t count) {
    return count ? make_top_element() : make_bottom_element();
}

static const builtin_function_t builtin_flat = {.name = L"flat",
                                                .min_args = 0,
                                                .effects = BUILTIN_EFFECT_NONE,
                                                .execute = execute,
                                                .execute_method = execute_method,
                                                .interpret = interpret,
                                                .get_object = get_function_flat};

object_t *get_function_flat(void) {
    static builtin_function_object_t object;
    return get_builtin_function_object(&object, &builtin_flat);
}
