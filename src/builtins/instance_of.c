/** @file instance_of.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Checks whether an object is an instance of a given prototype.
 */
#include "analysis/abstract_state.h"
#include "analysis/lattice.h"
#include "model/object.h"
#include "model/thread.h"
#include "registry.h"

/** @brief Checks whether the receiver is an instance of the given prototype. */
static operation_result_t check_instance(object_t *receiver, object_t *proto, thread_t *thread) {
    (void)thread;
    bool result = receiver == proto;
    if (!result) {
        object_array_t topology = get_object_topology(receiver);
        for (size_t index = 0; index < topology.size; index++) {
            if (topology.items[index] == proto) {
                result = true;
                break;
            }
        }
    }
    return operation_success(get_boolean_object(result));
}

/** @brief Runtime executor for the plain call form `instanceOf(object, proto)`. */
static operation_result_t execute(object_t **args, uint16_t count, thread_t *thread) {
    if (count < 2)
        return operation_exception(get_exception_invalid_argument());
    return check_instance(args[0], args[1], thread);
}

/** @brief Runtime executor for the method form `object.instanceOf(proto)`. */
static operation_result_t
execute_method(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    if (count < 1)
        return operation_exception(get_exception_invalid_argument());
    return check_instance(receiver, args[0], thread);
}

/** @brief Normal-result domain; the receiver is not visible here, so the result is a boolean. */
static const lattice_element_t *
interpret(abstract_state_t *state, const lattice_element_t *const *args, size_t count) {
    (void)state;
    (void)args;
    if (count < 1)
        return make_bottom_element();
    return make_boolean_element();
}

const builtin_function_t builtin_instance_of = {.name = U"instanceOf",
                                                .min_args = 1,
                                                .effects = BUILTIN_EFFECT_NONE,
                                                .execute = execute,
                                                .execute_method = execute_method,
                                                .interpret = interpret,
                                                .get_object = get_function_instance_of};

object_t *get_function_instance_of(void) {
    static builtin_function_object_t object;
    return get_builtin_function_object(&object, &builtin_instance_of);
}
