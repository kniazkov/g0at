#include "lib/strings/string_ext.h"
/** @file string_proto.c
 * @copyright 2026 Ivan Kniazkov
 * @brief The built-in String prototype and its method/property lookup.
 */
#include "analysis/lattice.h"
#include "builtin_function.h"
#include "builtins/registry.h"
#include "byte_array.h"
#include "common_methods.h"
#include "goat/unicode.h"
#include "lib/memory/allocate.h"
#include "object.h"
#include "thread.h"
#include "vm/gc_protocol.h"

static operation_result_t utf8_detached(object_t **args, uint16_t count, thread_t *thread) {
    return operation_exception(get_exception_invalid_operation());
}

static operation_result_t
to_utf8(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    if (receiver->vtbl->type != TYPE_STRING)
        return operation_exception(get_exception_invalid_operation());
    if (count)
        return operation_exception(get_exception_invalid_argument());
    string_value_t value = convert_object_to_string(receiver);
    size_t size;
    char *bytes = encode_utf8_view(VALUE_TO_VIEW(value), &size);
    FREE_STRING(value);
    if (!bytes)
        return operation_exception(get_exception_invalid_argument());
    operation_result_t result = create_byte_array_literal(thread, (const uint8_t *)bytes, size);
    FREE(bytes);
    return result;
}

static operation_result_t
from_utf8(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    if (receiver != get_string_proto())
        return operation_exception(get_exception_invalid_operation());
    if (count != 1 || !get_byte_array_storage(args[0]))
        return operation_exception(get_exception_invalid_argument());
    exception_t failure = gc_observe_object(thread, args[0]);
    if (failure.value) {
        INCREF(failure.value);
        return (operation_result_t){failure.value, true, failure.trace};
    }
    const byte_buffer_t *bytes = get_byte_array_storage(args[0]);
    string_value_t value =
        decode_utf8_n(bytes->length ? (const char *)bytes->data : "", bytes->length);
    if (!value.data)
        return operation_exception(get_exception_invalid_argument());
    return operation_success(create_string_object(thread->process, value));
}

static const lattice_element_t *
utf8_unknown(abstract_state_t *state, const lattice_element_t *const *args, size_t count) {
    return make_top_element();
}

#define UTF8_METHOD(id, label, executor)                                                           \
    static object_t *get_##id(void);                                                               \
    static const builtin_function_t id = {.name = U##label,                                        \
                                          .min_args = 0,                                           \
                                          .effects = BUILTIN_EFFECT_BINDINGS,                      \
                                          .execute = utf8_detached,                                \
                                          .execute_method = executor,                              \
                                          .interpret = utf8_unknown,                               \
                                          .get_object = get_##id};                                 \
    static object_t *get_##id(void) {                                                              \
        static builtin_function_object_t storage;                                                  \
        return get_builtin_function_object(&storage, &id);                                         \
    }
UTF8_METHOD(to_utf8_method, "String.toUtf8", to_utf8)
UTF8_METHOD(from_utf8_method, "String.fromUtf8", from_utf8)
#undef UTF8_METHOD

static object_t *get_to_utf8_key(void) {
    static object_static_string_t key;
    return get_static_string_object(&key, U"toUtf8");
}

static object_t *get_from_utf8_key(void) {
    static object_static_string_t key;
    return get_static_string_object(&key, U"fromUtf8");
}

/** @brief Immutable method-name key. */
static object_t *get_string_char_at(void) {
    static object_static_string_t key;
    return get_static_string_object(&key, U"charAt");
}

/** @brief Immutable method-name key. */
static object_t *get_string_substring(void) {
    static object_static_string_t key;
    return get_static_string_object(&key, U"substring");
}

/** @brief Immutable method-name key. */
static object_t *get_string_contains(void) {
    static object_static_string_t key;
    return get_static_string_object(&key, U"contains");
}

/** @brief Immutable method-name key. */
static object_t *get_string_index_of(void) {
    static object_static_string_t key;
    return get_static_string_object(&key, U"indexOf");
}

/** @brief Immutable method-name key. */
static object_t *get_string_last_index_of(void) {
    static object_static_string_t key;
    return get_static_string_object(&key, U"lastIndexOf");
}

/** @brief Immutable method-name key. */
static object_t *get_string_starts_with(void) {
    static object_static_string_t key;
    return get_static_string_object(&key, U"startsWith");
}

/** @brief Immutable method-name key. */
static object_t *get_string_ends_with(void) {
    static object_static_string_t key;
    return get_static_string_object(&key, U"endsWith");
}

/** @brief Immutable method-name key. */
static object_t *get_string_is_empty(void) {
    static object_static_string_t key;
    return get_static_string_object(&key, U"isEmpty");
}

/** @brief Immutable method-name key. */
static object_t *get_string_is_blank(void) {
    static object_static_string_t key;
    return get_static_string_object(&key, U"isBlank");
}

/** @brief Immutable method-name key. */
static object_t *get_string_to_upper_case(void) {
    static object_static_string_t key;
    return get_static_string_object(&key, U"toUpperCase");
}

/** @brief Immutable method-name key. */
static object_t *get_string_to_lower_case(void) {
    static object_static_string_t key;
    return get_static_string_object(&key, U"toLowerCase");
}

/** @brief Immutable method-name key. */
static object_t *get_string_replace(void) {
    static object_static_string_t key;
    return get_static_string_object(&key, U"replace");
}

/** @brief Immutable method-name key. */
static object_t *get_string_repeat(void) {
    static object_static_string_t key;
    return get_static_string_object(&key, U"repeat");
}

/** @brief Immutable method-name key. */
static object_t *get_string_format(void) {
    static object_static_string_t key;
    return get_static_string_object(&key, U"format");
}

/** @brief Implements @ref object_vtbl_t::get_keys. */
static object_array_t get_keys(const object_t *obj) {
    static object_t *keys[18] = {NULL,
                                 NULL,
                                 NULL,
                                 NULL,
                                 NULL,
                                 NULL,
                                 NULL,
                                 NULL,
                                 NULL,
                                 NULL,
                                 NULL,
                                 NULL,
                                 NULL,
                                 NULL,
                                 NULL,
                                 NULL};
    if (keys[0] == NULL) {
        keys[0] = get_string_length();
        keys[1] = get_string_trim();
        keys[2] = get_string_char_at();
        keys[3] = get_string_substring();
        keys[4] = get_string_contains();
        keys[5] = get_string_index_of();
        keys[6] = get_string_last_index_of();
        keys[7] = get_string_starts_with();
        keys[8] = get_string_ends_with();
        keys[9] = get_string_is_empty();
        keys[10] = get_string_is_blank();
        keys[11] = get_string_to_upper_case();
        keys[12] = get_string_to_lower_case();
        keys[13] = get_string_replace();
        keys[14] = get_string_repeat();
        keys[15] = get_string_format();
        keys[16] = get_to_utf8_key();
        keys[17] = get_from_utf8_key();
    }
    return (object_array_t){keys, sizeof(keys) / sizeof(object_t *)};
}

/** @brief Implements @ref object_vtbl_t::get_property. */
static object_t *proto_get_property(const object_t *obj, const object_t *key) {
    object_t *value = NULL;
    if (key->vtbl->type == TYPE_STRING) {
        if (!compare_objects_using_vtbl(key, get_to_utf8_key()))
            return get_to_utf8_method();
        if (!compare_objects_using_vtbl(key, get_from_utf8_key()))
            return get_from_utf8_method();
        string_value_t key_str = convert_object_to_string(key);
        if (string_view_equals(VALUE_TO_VIEW(key_str), VALUE_TO_VIEW(STATIC_STRING(U"length")))) {
            value = get_integer_zero();
        } else if (string_view_equals(VALUE_TO_VIEW(key_str),
                                      VALUE_TO_VIEW(STATIC_STRING(U"trim")))) {
            value = get_function_trim();
        } else if (string_view_equals(VALUE_TO_VIEW(key_str),
                                      VALUE_TO_VIEW(STATIC_STRING(U"charAt")))) {
            value = get_function_char_at();
        } else if (string_view_equals(VALUE_TO_VIEW(key_str),
                                      VALUE_TO_VIEW(STATIC_STRING(U"substring")))) {
            value = get_function_substring();
        } else if (string_view_equals(VALUE_TO_VIEW(key_str),
                                      VALUE_TO_VIEW(STATIC_STRING(U"contains")))) {
            value = get_function_contains();
        } else if (string_view_equals(VALUE_TO_VIEW(key_str),
                                      VALUE_TO_VIEW(STATIC_STRING(U"indexOf")))) {
            value = get_function_index_of();
        } else if (string_view_equals(VALUE_TO_VIEW(key_str),
                                      VALUE_TO_VIEW(STATIC_STRING(U"lastIndexOf")))) {
            value = get_function_last_index_of();
        } else if (string_view_equals(VALUE_TO_VIEW(key_str),
                                      VALUE_TO_VIEW(STATIC_STRING(U"startsWith")))) {
            value = get_function_starts_with();
        } else if (string_view_equals(VALUE_TO_VIEW(key_str),
                                      VALUE_TO_VIEW(STATIC_STRING(U"endsWith")))) {
            value = get_function_ends_with();
        } else if (string_view_equals(VALUE_TO_VIEW(key_str),
                                      VALUE_TO_VIEW(STATIC_STRING(U"isEmpty")))) {
            value = get_function_is_empty();
        } else if (string_view_equals(VALUE_TO_VIEW(key_str),
                                      VALUE_TO_VIEW(STATIC_STRING(U"isBlank")))) {
            value = get_function_is_blank();
        } else if (string_view_equals(VALUE_TO_VIEW(key_str),
                                      VALUE_TO_VIEW(STATIC_STRING(U"toUpperCase")))) {
            value = get_function_to_upper_case();
        } else if (string_view_equals(VALUE_TO_VIEW(key_str),
                                      VALUE_TO_VIEW(STATIC_STRING(U"toLowerCase")))) {
            value = get_function_to_lower_case();
        } else if (string_view_equals(VALUE_TO_VIEW(key_str),
                                      VALUE_TO_VIEW(STATIC_STRING(U"replace")))) {
            value = get_function_replace();
        } else if (string_view_equals(VALUE_TO_VIEW(key_str),
                                      VALUE_TO_VIEW(STATIC_STRING(U"repeat")))) {
            value = get_function_repeat();
        } else if (string_view_equals(VALUE_TO_VIEW(key_str),
                                      VALUE_TO_VIEW(STATIC_STRING(U"format")))) {
            value = get_function_format();
        }
    }
    return value;
}

/** @brief Virtual table defining the behavior of the prototype string object. */
static object_vtbl_t string_proto_vtbl = {.type = TYPE_OTHER,
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
                                          .get_property = proto_get_property,
                                          .create_property = create_property_on_immutable,
                                          .set_property = set_property_on_immutable,
                                          .get_index = common_get_index,
                                          .set_index = common_set_index,
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

/** @brief The prototype string object. */
static object_t string_proto = {.vtbl = &string_proto_vtbl};

object_t *get_string_proto() {
    return &string_proto;
}
