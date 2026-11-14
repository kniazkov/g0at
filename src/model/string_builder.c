/** @file string_builder.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Mutable Unicode builder initialized through the ordinary ancestor init chain.
 */
#include "string_builder.h"

#include "access.h"
#include "analysis/lattice.h"
#include "builtin_function.h"
#include "common_methods.h"
#include "context.h"
#include "lib/memory/allocate.h"
#include "thread.h"

#include <stdint.h>

string_value_t string_builder_snapshot(const object_t *obj) {
    string_builder_t *buffer = get_string_builder_storage(obj);
    if (!buffer || !buffer->length)
        return EMPTY_STRING_VALUE;
    gchar_t *copy = ALLOC((buffer->length + 1) * sizeof(*copy));
    MEMCPY(copy, buffer->data, buffer->length);
    copy[buffer->length] = 0;
    return (string_value_t){copy, buffer->length, true};
}

/** @brief Update the public constant length cell, bypassing language writes. */
static void update_length(object_t *receiver) {
    field_cell_t *cell = get_own_field_cell(receiver, get_string_length());
    object_t *length = create_integer_object(receiver->process,
                                             (int64_t)get_string_builder_storage(receiver)->length);
    DECREF(cell->object);
    cell->object = length;
}

static operation_result_t self(object_t *receiver) {
    INCREF(receiver);
    return operation_success(receiver);
}

static operation_result_t invalid_call(object_t **args, uint16_t count, thread_t *thread) {
    return operation_exception(get_exception_invalid_operation());
}

static operation_result_t validate(object_t *receiver) {
    return operation_exception(receiver == get_string_builder_proto()
                                   ? get_exception_immutable_object()
                                   : get_exception_invalid_operation());
}

static operation_result_t
initialize(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    if (receiver == get_string_builder_proto())
        return validate(receiver);
    if (count || receiver->vtbl->type != TYPE_USER_DEFINED_OBJECT)
        return operation_exception(get_exception_invalid_argument());
    /* Manual repeated init must not leak or silently discard accumulated text. */
    if (get_string_builder_storage(receiver))
        return self(receiver);
    if (get_own_field_cell(receiver, get_string_length()))
        return operation_exception(get_exception_property_already_exists());
    if (!create_object_field(receiver,
                             get_string_length(),
                             get_integer_zero(),
                             true,
                             ACCESS_PUBLIC,
                             get_object_module(receiver),
                             get_object_access_domain(receiver),
                             true))
        return operation_exception(get_exception_property_already_exists());
    initialize_string_builder_storage(receiver);
    return self(receiver);
}

static operation_result_t
append(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    string_builder_t *buffer = get_string_builder_storage(receiver);
    if (!buffer)
        return validate(receiver);
    if (count != 1)
        return operation_exception(get_exception_invalid_argument());
    exception_t failure = object_observation_exception(args[0], &thread->context->access);
    if (failure.value) {
        INCREF(failure.value);
        return (operation_result_t){failure.value, true, failure.trace};
    }
    /* Same conversion as str; a snapshot also makes self append safe across growth. */
    string_value_t value = object_to_string_with_access(args[0], &thread->context->access);
    size_t maximum = SIZE_MAX / sizeof(gchar_t) - 1;
    if (maximum > INT64_MAX)
        maximum = INT64_MAX;
    if (value.length > maximum - buffer->length) {
        FREE_STRING(value);
        return operation_exception(get_exception_invalid_operation());
    }
    append_string_value(buffer, value);
    FREE_STRING(value);
    update_length(receiver);
    return self(receiver);
}

static operation_result_t
clear(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    string_builder_t *buffer = get_string_builder_storage(receiver);
    if (!buffer)
        return validate(receiver);
    if (count)
        return operation_exception(get_exception_invalid_argument());
    buffer->length = 0;
    if (buffer->data)
        buffer->data[0] = 0;
    update_length(receiver);
    return self(receiver);
}

static operation_result_t
to_string(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    if (!get_string_builder_storage(receiver))
        return operation_exception(get_exception_invalid_operation());
    if (count)
        return operation_exception(get_exception_invalid_argument());
    return operation_success(
        create_string_object(thread->process, string_builder_snapshot(receiver)));
}

static const lattice_element_t *
unknown(abstract_state_t *state, const lattice_element_t *const *args, size_t count) {
    return make_top_element();
}

#define METHOD(id, label, executor)                                                                \
    static object_t *get_##id(void);                                                               \
    static const builtin_function_t id = {.name = U##label,                                        \
                                          .min_args = 0,                                           \
                                          .effects = BUILTIN_EFFECT_BINDINGS,                      \
                                          .execute = invalid_call,                                 \
                                          .execute_method = executor,                              \
                                          .interpret = unknown,                                    \
                                          .get_object = get_##id};                                 \
    static object_t *get_##id(void) {                                                              \
        static builtin_function_object_t storage;                                                  \
        return get_builtin_function_object(&storage, &id);                                         \
    }
METHOD(builder_init, "StringBuilder.init", initialize)
METHOD(builder_append, "StringBuilder.append", append)
METHOD(builder_clear, "StringBuilder.clear", clear)
METHOD(builder_string, "StringBuilder.toString", to_string)
#undef METHOD

static const gchar_t *names[] = {U"init", U"append", U"clear", U"toString", U"+", U"<<"};

static object_array_t get_keys(const object_t *obj) {
    static object_static_string_t storage[6];
    static object_t *keys[6];
    for (size_t i = 0; i < 6; i++)
        keys[i] = get_static_string_object(&storage[i], names[i]);
    return (object_array_t){keys, 6};
}

static object_t *get_property(const object_t *obj, const object_t *key) {
    object_array_t keys = get_keys(obj);
    if (key->vtbl->type != TYPE_STRING)
        return NULL;
    for (size_t i = 0; i < keys.size; i++) {
        if (!compare_objects_using_vtbl(key, keys.items[i])) {
            switch (i) {
                case 0:
                    return get_builder_init();
                case 2:
                    return get_builder_clear();
                case 3:
                    return get_builder_string();
                default:
                    return get_builder_append();
            }
        }
    }
    return NULL;
}

static operation_result_t immutable_append(process_t *process, object_t *left, object_t *right) {
    return operation_exception(get_exception_immutable_object());
}

static object_vtbl_t builder_proto_vtbl = {.type = TYPE_OTHER,
                                           .inc_ref = stub_memory_function,
                                           .dec_ref = stub_memory_function,
                                           .sweep = no_sweep,
                                           .release = stub_memory_function,
                                           .compare = compare_object_addresses,
                                           .clone = clone_singleton,
                                           .to_string = common_to_string,
                                           .to_string_notation = common_to_string_notation,
                                           .get_prototypes = common_get_prototypes,
                                           .get_topology = common_get_topology,
                                           .get_keys = get_keys,
                                           .get_property = get_property,
                                           .create_property = create_property_on_immutable,
                                           .set_property = set_property_on_immutable,
                                           .get_index = common_get_index,
                                           .set_index = common_set_index,
                                           .bitwise_not = stub_unary_operation,
                                           .bitwise_and = stub_bitwise,
                                           .bitwise_or = stub_bitwise,
                                           .bitwise_xor = stub_bitwise,
                                           .shift_left = immutable_append,
                                           .shift_right = stub_bitwise,
                                           .unary_plus = stub_unary_operation,
                                           .unary_minus = stub_unary_operation,
                                           .increment = stub_unary_operation,
                                           .decrement = stub_unary_operation,
                                           .add = immutable_append,
                                           .subtract = stub_subtract,
                                           .multiply = stub_multiply,
                                           .divide = stub_divide,
                                           .modulo = stub_modulo,
                                           .power = stub_power,
                                           .less = common_less,
                                           .less_or_equal = common_less_or_equal,
                                           .greater = common_greater,
                                           .greater_or_equal = common_greater_or_equal,
                                           .equal = common_equal,
                                           .not_equal = common_not_equal,
                                           .get_boolean_value = stub_get_boolean_value,
                                           .get_integer_value = stub_get_integer_value,
                                           .get_real_value = stub_get_real_value,
                                           .call = stub_call};

static object_t prototype = {.vtbl = &builder_proto_vtbl};

object_t *get_string_builder_proto(void) {
    return &prototype;
}
