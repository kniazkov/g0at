/** @file flat.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Shallow mutable property materialization in ordinary lookup order.
 */
#include "analysis/lattice.h"
#include "lib/allocate.h"
#include "model/builtin_function.h"
#include "model/context.h"
#include "model/thread.h"

/** @brief One field-domain relocation, shared across the internal property transfer. */
typedef struct {
    access_domain_t *source;
    access_domain_t *target;
} domain_pair_t;

static access_domain_t *copy_domain(vector_t *domains, access_domain_t *source) {
    for (size_t i = 0; i < domains->size; i++) {
        domain_pair_t *pair = domains->data[i];
        if (pair->source == source)
            return pair->target;
    }
    domain_pair_t *pair = ALLOC(sizeof(*pair));
    *pair = (domain_pair_t){source, create_access_domain(get_access_domain_module(source))};
    append_to_vector(domains, pair);
    return pair->target;
}

static void copy_properties(object_t *target, object_t *source, vector_t *domains) {
    if (source == get_root_object())
        return;
    object_array_t keys = get_object_keys(source);
    for (size_t i = 0; i < keys.size; i++) {
        object_t *key = keys.items[i];
        if (!get_object_property(target, key)) {
            field_cell_t *cell = get_own_field_cell(source, key);
            create_object_field(target,
                                key,
                                get_object_property(source, key),
                                false,
                                cell ? cell->modifier : ACCESS_PUBLIC,
                                cell ? cell->module : get_builtin_module_identity(),
                                cell ? copy_domain(domains, cell->domain)
                                     : get_object_access_domain(target),
                                true);
        }
    }
}

static object_t *flatten(process_t *process, object_t *source, const function_access_t *access) {
    if (source->vtbl->type != TYPE_USER_DEFINED_OBJECT) {
        INCREF(source);
        return source;
    }
    object_t *parent = get_root_object();
    object_t *target =
        create_user_defined_object_in_module(process, (object_array_t){&parent, 1}, access->module);
    vector_t *domains = create_vector();
    copy_properties(target, source, domains);
    object_array_t topology = get_object_topology(source);
    for (size_t i = 0; i < topology.size; i++)
        copy_properties(target, topology.items[i], domains);
    for (size_t i = 0; i < domains->size; i++) {
        domain_pair_t *pair = domains->data[i];
        release_access_domain(pair->target);
        FREE(pair);
    }
    destroy_vector(domains);
    return target;
}

static operation_result_t
execute_method(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    return operation_success(flatten(thread->process, receiver, &thread->context->access));
}

static operation_result_t execute(object_t **args, uint16_t count, thread_t *thread) {
    return count ? operation_success(flatten(thread->process, args[0], &thread->context->access))
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
