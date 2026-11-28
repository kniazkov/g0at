/** @file byte_array.c
 * @copyright 2026 Ivan Kniazkov
 * @brief ByteArray construction and strict indexed access.
 */
#include "byte_array.h"

#include "access.h"
#include "analysis/lattice.h"
#include "builtin_function.h"
#include "common_methods.h"
#include "context.h"
#include "lib/memory/allocate.h"
#include "thread.h"
#include "vm/gc_policy.h"
#include "vm/gc_protocol.h"

#include <stdlib.h>
#include <string.h>

bool byte_array_hex_size(size_t count, size_t *bytes) {
    if (count > (SIZE_MAX / sizeof(gchar_t) - 2) / 2)
        return false;
    *bytes = (count * 2 + 2) * sizeof(gchar_t);
    return true;
}

string_value_t byte_array_snapshot(const object_t *obj) {
    const byte_buffer_t *buffer = get_byte_array_storage(obj);
    size_t bytes;
    if (!byte_array_hex_size(buffer->length, &bytes))
        abort();
    size_t length = buffer->length * 2 + 1;
    gchar_t *text = ALLOC(bytes);
    text[0] = U'#';
    for (size_t i = 0; i < buffer->length; ++i) {
        text[1 + 2 * i] = U"0123456789ABCDEF"[buffer->data[i] >> 4];
        text[2 + 2 * i] = U"0123456789ABCDEF"[buffer->data[i] & 15];
    }
    text[length] = 0;
    return (string_value_t){text, length, true};
}

static operation_result_t
to_string(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    if (!get_byte_array_storage(receiver))
        return operation_exception(get_exception_invalid_operation());
    if (count)
        return operation_exception(get_exception_invalid_argument());
    return operation_success(create_string_object(thread->process, byte_array_snapshot(receiver)));
}

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

/** @brief A missing payload is never inherited from a prototype. */
static operation_result_t invalid_receiver(object_t *receiver) {
    return operation_exception(receiver == get_byte_array_proto()
                                   ? get_exception_immutable_object()
                                   : get_exception_invalid_operation());
}

/** @brief Strict nonnegative Integer, bounded before conversion to size_t. */
static bool size_argument(object_t *arg, size_t *value) {
    if (!is_integer_object(arg))
        return false;
    int64_t n = get_object_integer_value(arg).value;
    if (n < 0 || (uint64_t)n > byte_buffer_limit())
        return false;
    *value = (size_t)n;
    return true;
}

/** @brief Strict byte argument; no numeric coercion. */
static bool byte_argument(object_t *arg, uint8_t *value) {
    size_t n;
    if (!size_argument(arg, &n) || n > 255)
        return false;
    *value = (uint8_t)n;
    return true;
}

/** @brief Update the immutable public length after a successful mutation. */
static operation_result_t finish_mutation(object_t *receiver, size_t before) {
    gc_policy_note_growth(receiver, before);
    field_cell_t *cell = get_own_field_cell(receiver, get_string_length());
    object_t *length =
        create_integer_object(receiver->process, (int64_t)get_byte_array_storage(receiver)->length);
    DECREF(cell->object);
    cell->object = length;
    INCREF(receiver);
    return operation_success(receiver);
}

operation_result_t create_byte_array_literal(thread_t *thread, const uint8_t *bytes, size_t size) {
    object_t *proto = get_byte_array_proto();
    object_t *result = create_user_defined_object_in_file(thread->process,
                                                          (object_array_t){&proto, 1},
                                                          thread->context->access.module,
                                                          thread->context->access.file_id);
    operation_result_t initialized = initialize_byte_array(result);
    if (initialized.is_exception) {
        DECREF(result);
        return initialized;
    }
    DECREF(initialized.value);
    size_t before = gc_policy_object_bytes(result);
    byte_buffer_t *buffer = get_byte_array_storage(result);
    if (!byte_buffer_resize(buffer, size, 0)) {
        DECREF(result);
        return operation_exception(get_exception_invalid_argument());
    }
    if (size)
        memcpy(buffer->data, bytes, size);
    operation_result_t value = finish_mutation(result, before);
    DECREF(result);
    return value;
}

static operation_result_t
append(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    if (!get_byte_array_storage(receiver))
        return invalid_receiver(receiver);
    if (count < 1 || count > 3)
        return operation_exception(get_exception_invalid_argument());
    exception_t failure = gc_observe_object(thread, args[0]);
    if (failure.value) {
        INCREF(failure.value);
        return (operation_result_t){failure.value, true, failure.trace};
    }
    /* Observation can suspend. Reacquire buffers and validate ranges afterwards. */
    byte_buffer_t *buffer = get_byte_array_storage(receiver);
    byte_buffer_t *source = get_byte_array_storage(args[0]);
    size_t before = gc_policy_object_bytes(receiver);
    if (source) {
        size_t start = 0, length;
        if (count >= 2 && !size_argument(args[1], &start))
            return operation_exception(get_exception_invalid_argument());
        if (start > source->length)
            return operation_exception(get_exception_invalid_argument());
        length = source->length - start;
        if (count == 3 && !size_argument(args[2], &length))
            return operation_exception(get_exception_invalid_argument());
        if (!byte_buffer_append(buffer, source, start, length))
            return operation_exception(get_exception_invalid_argument());
    } else {
        uint8_t byte;
        if (count != 1 || !byte_argument(args[0], &byte) || buffer->length == byte_buffer_limit())
            return operation_exception(get_exception_invalid_argument());
        byte_buffer_resize(buffer, buffer->length + 1, byte);
    }
    return finish_mutation(receiver, before);
}

static operation_result_t
resize(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    byte_buffer_t *buffer = get_byte_array_storage(receiver);
    if (!buffer)
        return invalid_receiver(receiver);
    size_t length;
    uint8_t fill = 0;
    if (count < 1 || count > 2 || !size_argument(args[0], &length)
        || (count == 2 && !byte_argument(args[1], &fill)))
        return operation_exception(get_exception_invalid_argument());
    size_t before = gc_policy_object_bytes(receiver);
    if (!byte_buffer_resize(buffer, length, fill))
        return operation_exception(get_exception_invalid_argument());
    return finish_mutation(receiver, before);
}

static operation_result_t
clear(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    byte_buffer_t *buffer = get_byte_array_storage(receiver);
    if (!buffer)
        return invalid_receiver(receiver);
    if (count)
        return operation_exception(get_exception_invalid_argument());
    size_t before = gc_policy_object_bytes(receiver);
    byte_buffer_clear(buffer);
    return finish_mutation(receiver, before);
}

static operation_result_t
shrink(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    byte_buffer_t *buffer = get_byte_array_storage(receiver);
    if (!buffer)
        return invalid_receiver(receiver);
    if (count)
        return operation_exception(get_exception_invalid_argument());
    size_t before = gc_policy_object_bytes(receiver);
    byte_buffer_shrink(buffer);
    return finish_mutation(receiver, before);
}

/** @brief Validate a range by subtraction, including the empty end range. */
static bool range_arguments(const byte_buffer_t *buffer,
                            object_t **args,
                            uint16_t count,
                            size_t *start,
                            size_t *length) {
    *start = 0;
    if (count && !size_argument(args[0], start))
        return false;
    if (*start > buffer->length)
        return false;
    *length = buffer->length - *start;
    return count < 2 || (size_argument(args[1], length) && *length <= buffer->length - *start);
}

static operation_result_t
slice(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    byte_buffer_t *buffer = get_byte_array_storage(receiver);
    if (!buffer)
        return operation_exception(get_exception_invalid_operation());
    size_t start, length;
    if (count < 1 || count > 2 || !range_arguments(buffer, args, count, &start, &length))
        return operation_exception(get_exception_invalid_argument());
    object_t *proto = get_byte_array_proto();
    object_t *result = create_user_defined_object_in_file(thread->process,
                                                          (object_array_t){&proto, 1},
                                                          thread->context->access.module,
                                                          thread->context->access.file_id);
    operation_result_t initialized = initialize_byte_array(result);
    if (initialized.is_exception) {
        DECREF(result);
        return initialized;
    }
    DECREF(initialized.value);
    size_t before = gc_policy_object_bytes(result);
    byte_buffer_append(get_byte_array_storage(result), buffer, start, length);
    operation_result_t value = finish_mutation(result, before);
    DECREF(result);
    return value;
}

static operation_result_t
copy_to(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    if (!get_byte_array_storage(receiver))
        return operation_exception(get_exception_invalid_operation());
    if (count != 4)
        return operation_exception(get_exception_invalid_argument());
    if (args[0] == get_byte_array_proto())
        return operation_exception(get_exception_immutable_object());
    if (!get_byte_array_storage(args[0]))
        return operation_exception(get_exception_invalid_argument());
    exception_t failure = gc_observe_object(thread, args[0]);
    if (failure.value) {
        INCREF(failure.value);
        return (operation_result_t){failure.value, true, failure.trace};
    }
    byte_buffer_t *source = get_byte_array_storage(receiver),
                  *target = get_byte_array_storage(args[0]);
    size_t from, to, length;
    if (!size_argument(args[1], &from) || !size_argument(args[2], &to)
        || !size_argument(args[3], &length)
        || !byte_buffer_copy_range(target, to, source, from, length))
        return operation_exception(get_exception_invalid_argument());
    INCREF(args[0]);
    return operation_success(args[0]);
}

static operation_result_t
fill(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    byte_buffer_t *buffer = get_byte_array_storage(receiver);
    if (!buffer)
        return invalid_receiver(receiver);
    uint8_t byte;
    size_t start, length;
    if (count < 1 || count > 3 || !byte_argument(args[0], &byte)
        || !range_arguments(buffer, args + 1, count - 1, &start, &length))
        return operation_exception(get_exception_invalid_argument());
    if (length)
        memset(buffer->data + start, byte, length);
    INCREF(receiver);
    return operation_success(receiver);
}

static operation_result_t
index_of(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    if (!get_byte_array_storage(receiver))
        return operation_exception(get_exception_invalid_operation());
    if (count < 1 || count > 2)
        return operation_exception(get_exception_invalid_argument());
    exception_t failure = gc_observe_object(thread, args[0]);
    if (failure.value) {
        INCREF(failure.value);
        return (operation_result_t){failure.value, true, failure.trace};
    }
    byte_buffer_t *buffer = get_byte_array_storage(receiver),
                  *pattern = get_byte_array_storage(args[0]);
    size_t start = 0, length = pattern ? pattern->length : 1;
    uint8_t byte;
    if ((count == 2 && !size_argument(args[1], &start)) || start > buffer->length
        || (!pattern && !byte_argument(args[0], &byte)))
        return operation_exception(get_exception_invalid_argument());
    int64_t found = -1;
    if (!length)
        found = (int64_t)start;
    else if (length <= buffer->length - start) {
        const uint8_t *needle = pattern ? pattern->data : &byte;
        size_t last = buffer->length - length;
        for (size_t i = start; i <= last; ++i) {
            if (!memcmp(buffer->data + i, needle, length)) {
                found = (int64_t)i;
                break;
            }
        }
    }
    return operation_success(create_integer_object(thread->process, found));
}

static operation_result_t immutable_shift(process_t *process, object_t *left, object_t *right) {
    return operation_exception(get_exception_immutable_object());
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
METHOD(init_method, "ByteArray.init", initialize)
METHOD(append_method, "ByteArray.append", append)
METHOD(resize_method, "ByteArray.resize", resize)
METHOD(clear_method, "ByteArray.clear", clear)
METHOD(shrink_method, "ByteArray.shrinkToFit", shrink)
METHOD(slice_method, "ByteArray.slice", slice)
METHOD(copy_method, "ByteArray.copyTo", copy_to)
METHOD(fill_method, "ByteArray.fill", fill)
METHOD(index_method, "ByteArray.indexOf", index_of)
METHOD(string_method, "ByteArray.toString", to_string)
#undef METHOD
static const gchar_t *names[] = {U"init",
                                 U"append",
                                 U"<<",
                                 U"resize",
                                 U"clear",
                                 U"shrinkToFit",
                                 U"slice",
                                 U"copyTo",
                                 U"fill",
                                 U"indexOf",
                                 U"toString"};

static object_array_t get_keys(const object_t *obj) {
    static object_static_string_t storage[11];
    static object_t *keys[11];
    for (size_t i = 0; i < 11; ++i)
        keys[i] = get_static_string_object(&storage[i], names[i]);
    return (object_array_t){keys, 11};
}

static object_t *get_property(const object_t *obj, const object_t *key) {
    if (key->vtbl->type != TYPE_STRING)
        return NULL;
    object_array_t keys = get_keys(obj);
    for (size_t i = 0; i < keys.size; ++i) {
        if (!compare_objects_using_vtbl(key, keys.items[i])) {
            switch (i) {
                case 0:
                    return get_init_method();
                case 1:
                case 2:
                    return get_append_method();
                case 3:
                    return get_resize_method();
                case 4:
                    return get_clear_method();
                case 5:
                    return get_shrink_method();
                case 6:
                    return get_slice_method();
                case 7:
                    return get_copy_method();
                case 8:
                    return get_fill_method();
                case 9:
                    return get_index_method();
                default:
                    return get_string_method();
            }
        }
    }
    return NULL;
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
                                        .shift_left = immutable_shift,
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
