/** @file byte_array.c
 * @copyright 2026 Ivan Kniazkov
 * @brief ByteArray construction and strict indexed access.
 */
#include "byte_array.h"

#include "access.h"
#include "analysis/lattice.h"
#include "builtin_function.h"
#include "common_methods.h"
#include "thread.h"

operation_result_t initialize_byte_array(object_t *receiver) {
    if (receiver == get_byte_array_proto())
        return operation_exception(get_exception_immutable_object());
    if (receiver->vtbl->type != TYPE_USER_DEFINED_OBJECT)
        return operation_exception(get_exception_invalid_operation());
    if (!get_byte_array_storage(receiver)) {
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
        initialize_byte_array_storage(receiver);
    }
    INCREF(receiver);
    return operation_success(receiver);
}

/** @brief Validate the single index without conversion or unsigned wraparound. */
static bool index_value(const byte_buffer_t *buffer, object_array_t indices, size_t *index) {
    if (indices.size != 1 || !is_integer_object(indices.items[0]))
        return false;
    int64_t value = get_object_integer_value(indices.items[0]).value;
    if (value < 0 || (uint64_t)value >= buffer->length)
        return false;
    *index = (size_t)value;
    return true;
}

operation_result_t byte_array_get_index(process_t *process, object_t *obj, object_array_t indices) {
    byte_buffer_t *buffer = get_byte_array_storage(obj);
    if (!buffer)
        return common_get_index(process, obj, indices);
    size_t index;
    if (!index_value(buffer, indices, &index))
        return operation_exception(get_exception_invalid_argument());
    return operation_success(create_integer_object(process, buffer->data[index]));
}

operation_result_t
byte_array_set_index(process_t *process, object_t *obj, object_array_t indices, object_t *value) {
    if (obj == get_byte_array_proto())
        return operation_exception(get_exception_immutable_object());
    byte_buffer_t *buffer = get_byte_array_storage(obj);
    if (!buffer)
        return common_set_index(process, obj, indices, value);
    size_t index;
    if (!index_value(buffer, indices, &index) || !is_integer_object(value))
        return operation_exception(get_exception_invalid_argument());
    int64_t byte = get_object_integer_value(value).value;
    if (byte < 0 || byte > 255)
        return operation_exception(get_exception_invalid_argument());
    buffer->data[index] = (uint8_t)byte;
    INCREF(value);
    return operation_success(value);
}

static operation_result_t invalid_call(object_t **args, uint16_t count, thread_t *thread) {
    return operation_exception(get_exception_invalid_operation());
}

static operation_result_t
initialize(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    if (count)
        return operation_exception(get_exception_invalid_argument());
    return initialize_byte_array(receiver);
}

static const lattice_element_t *
unknown(abstract_state_t *state, const lattice_element_t *const *args, size_t count) {
    return make_top_element();
}

static object_t *get_init(void);
static const builtin_function_t init = {.name = U"ByteArray.init",
                                        .min_args = 0,
                                        .effects = BUILTIN_EFFECT_BINDINGS,
                                        .execute = invalid_call,
                                        .execute_method = initialize,
                                        .interpret = unknown,
                                        .get_object = get_init};

static object_t *get_init(void) {
    static builtin_function_object_t storage;
    return get_builtin_function_object(&storage, &init);
}

static object_array_t get_keys(const object_t *obj) {
    static object_static_string_t storage;
    static object_t *keys[1];
    keys[0] = get_static_string_object(&storage, U"init");
    return (object_array_t){keys, 1};
}

static object_t *get_property(const object_t *obj, const object_t *key) {
    return key->vtbl->type == TYPE_STRING
                   && !compare_objects_using_vtbl(key, get_keys(obj).items[0])
               ? get_init()
               : NULL;
}

static object_vtbl_t byte_proto_vtbl = {.type = TYPE_OTHER,
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
                                        .get_index = byte_array_get_index,
                                        .set_index = byte_array_set_index,
                                        .bitwise_not = stub_unary_operation,
                                        .bitwise_and = stub_bitwise,
                                        .bitwise_or = stub_bitwise,
                                        .bitwise_xor = stub_bitwise,
                                        .shift_left = stub_bitwise,
                                        .shift_right = stub_bitwise,
                                        .unary_plus = stub_unary_operation,
                                        .unary_minus = stub_unary_operation,
                                        .increment = stub_unary_operation,
                                        .decrement = stub_unary_operation,
                                        .add = stub_add,
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

static object_t prototype = {.vtbl = &byte_proto_vtbl};

object_t *get_byte_array_proto(void) {
    return &prototype;
}

bool byte_array_property_index(const object_t *obj, bool store) {
    if (get_byte_array_storage(obj) || obj == get_byte_array_proto())
        return false;
    return store ? (obj->vtbl->set_index == common_set_index
                    || obj->vtbl->set_index == byte_array_set_index)
                 : (obj->vtbl->get_index == common_get_index
                    || obj->vtbl->get_index == byte_array_get_index);
}
