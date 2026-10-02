/**
 * @file string.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementations of an object representing a string.
 */

#include "common_methods.h"
#include "lib/allocate.h"
#include "lib/comparison.h"
#include "lib/string_ext.h"
#include "object.h"
#include "object_state.h"
#include "process.h"

#include <assert.h>

/**
 * @brief Defines the maximum capacity of the object pool.
 *
 * Sets the maximum number of objects that can be stored in the object pool before it reaches its
 * capacity.
 */
#define POOL_CAPACITY 1024

/**
 * @brief A static string object.
 *
 * Static strings are immutable and exist for the entire duration of the program's execution.
 */
typedef struct {
    object_t base;        ///< The base object that provides common functionality.
    string_view_t string; ///< The string.
} object_static_string_t;

/**
 * @brief A dynamic string object.
 *
 * Dynamic strings are also immutable but are created at runtime, typically as a result of string
 * operations.
 */
typedef struct {
    object_t base;        ///< The base object that provides common functionality.
    int refs;             ///< Reference count used for garbage collection.
    object_state_t state; ///< The state of the object (e.g., unmarked, marked, or zombie).
    string_view_t string; ///< The string.
} object_dynamic_string_t;

/** @brief Implements @ref object_vtbl_t::get_keys. */
static object_array_t get_keys(const object_t *obj) {
    static object_t *keys[1] = {NULL};
    if (keys[0] == NULL) {
        keys[0] = get_string_length();
    }
    return (object_array_t){keys, sizeof(keys) / sizeof(object_t *)};
}

/** @brief Implements @ref object_vtbl_t::get_property. */
static object_t *proto_get_property(const object_t *obj, const object_t *key) {
    object_t *value = NULL;
    if (key->vtbl->type == TYPE_STRING) {
        string_value_t key_str = convert_object_to_string(key);
        if (wcscmp(L"length", key_str.data) == 0) {
            value = get_integer_zero();
        }
    }
    return value;
}

/** @brief Virtual table defining the behavior of the prototype string object. */
static object_vtbl_t string_proto_vtbl = {.type = TYPE_OTHER,
                                          .inc_ref = stub_memory_function,
                                          .dec_ref = stub_memory_function,
                                          .mark = stub_memory_function,
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

/** @brief Releases or clears a dynamic string object. */
static void release_or_clear(object_dynamic_string_t *dsobj) {
    FREE((wchar_t *)(dsobj->string.data));
    remove_object_from_list(&dsobj->base.process->objects, &dsobj->base);
    if (dsobj->base.process->dynamic_strings.size == POOL_CAPACITY) {
        FREE(dsobj);
    } else {
        dsobj->refs = 0;
        dsobj->state = ZOMBIE;
        dsobj->string = (string_view_t){NULL, 0};
        add_object_to_list(&dsobj->base.process->dynamic_strings, &dsobj->base);
    }
}

/** @brief Implements @ref object_vtbl_t::inc_ref. */
static void inc_ref(object_t *obj) {
    object_dynamic_string_t *dsobj = (object_dynamic_string_t *)obj;
    assert(dsobj->state != ZOMBIE);
    dsobj->refs++;
}

/** @brief Implements @ref object_vtbl_t::dec_ref. */
static void dec_ref(object_t *obj) {
    object_dynamic_string_t *dsobj = (object_dynamic_string_t *)obj;
    assert(dsobj->state != ZOMBIE);
    if (!(--dsobj->refs)) {
        release_or_clear(dsobj);
    }
}

/** @brief Implements @ref object_vtbl_t::mark. */
static void mark(object_t *obj) {
    object_dynamic_string_t *dsobj = (object_dynamic_string_t *)obj;
    assert(dsobj->state != ZOMBIE);
    dsobj->state = MARKED;
}

/** @brief Implements @ref object_vtbl_t::sweep. */
static bool sweep(object_t *obj) {
    object_dynamic_string_t *dsobj = (object_dynamic_string_t *)obj;
    assert(dsobj->state != ZOMBIE);
    if (dsobj->state == UNMARKED) {
        release_or_clear(dsobj);
        return true;
    } else {
        dsobj->state = UNMARKED;
        return false;
    }
}

/** @brief Implements @ref object_vtbl_t::release. */
static void release(object_t *obj) {
    object_dynamic_string_t *dsobj = (object_dynamic_string_t *)obj;
    remove_object_from_list(dsobj->state == ZOMBIE ? &obj->process->dynamic_strings
                                                   : &obj->process->objects,
                            obj);
    FREE((wchar_t *)(dsobj->string.data));
    FREE(obj);
}

/** @brief Implements @ref object_vtbl_t::compare. */
static int compare(const object_t *obj1, const object_t *obj2) {
    string_value_t first = convert_object_to_string(obj1);
    string_value_t second = convert_object_to_string(obj2);
    comparison_order_t order = compare_strings(VALUE_TO_VIEW(first), VALUE_TO_VIEW(second));
    int result = order == ORDER_LESS ? -1 : order == ORDER_GREATER ? 1 : 0;
    FREE_STRING(first);
    FREE_STRING(second);
    return result;
}

/** @brief Implements @ref object_vtbl_t::clone. */
static object_t *clone(process_t *process, object_t *obj) {
    if (process == obj->process) {
        return obj;
    }
    string_value_t value = convert_object_to_string(obj);
    return create_string_object(process, value);
}

/** @brief Implements @ref object_vtbl_t::to_string. */
static string_value_t static_to_string(const object_t *obj) {
    object_static_string_t *stsobj = (object_static_string_t *)obj;
    return VIEW_TO_VALUE(stsobj->string);
}

/** @brief Implements @ref object_vtbl_t::to_string. */
static string_value_t dynamic_to_string(const object_t *obj) {
    object_dynamic_string_t *dsobj = (object_dynamic_string_t *)obj;
    return VIEW_TO_VALUE(dsobj->string);
}

/** @brief Implements @ref object_vtbl_t::to_string_notation. */
static string_value_t to_string_notation(const object_t *obj) {
    string_value_t value = convert_object_to_string(obj);
    return string_to_string_notation(L"", value);
}

/** @brief Array of prototypes for the string object. */
static object_t *prototypes[] = {&string_proto};

/** @brief Implements @ref object_vtbl_t::get_prototypes. */
static object_array_t get_prototypes(const object_t *obj) {
    object_array_t result = {.items = prototypes, .size = 1};
    return result;
}

/** @brief Implements @ref object_vtbl_t::get_topology. */
static object_array_t get_topology(const object_t *obj) {
    static object_t *topology[2] = {0};
    if (topology[0] == NULL) {
        topology[0] = &string_proto;
        topology[1] = get_root_object();
    }
    object_array_t result = {.items = topology, .size = 2};
    return result;
}

/** @brief Implements @ref object_vtbl_t::get_property. */
static object_t *static_get_property(const object_t *obj, const object_t *key) {
    if (key->vtbl->type == TYPE_STRING) {
        object_static_string_t *stsobj = (object_static_string_t *)obj;
        string_value_t key_str = convert_object_to_string(key);
        if (wcscmp(L"length", key_str.data) == 0) {
            return get_static_integer_object((int)stsobj->string.length);
        }
    }
    return NULL;
}

/** @brief Implements @ref object_vtbl_t::get_property. */
static object_t *dynamic_get_property(const object_t *obj, const object_t *key) {
    if (key->vtbl->type == TYPE_STRING) {
        object_dynamic_string_t *dsobj = (object_dynamic_string_t *)obj;
        string_value_t key_str = convert_object_to_string(key);
        if (wcscmp(L"length", key_str.data) == 0) {
            return create_integer_object(obj->process, (int64_t)dsobj->string.length);
        }
    }
    return NULL;
}

/** @brief Implements @ref object_vtbl_t::add. */
static operation_result_t add(process_t *process, object_t *obj1, object_t *obj2) {
    string_value_t first = convert_object_to_string(obj1);
    if (first.length == 0) {
        if (obj2->vtbl->type == TYPE_STRING) {
            INCREF(obj2);
            return operation_success(obj2);
        }
        return operation_success(create_string_object(process, convert_object_to_string(obj2)));
    }
    string_value_t second = convert_object_to_string(obj2);
    if (second.length == 0) {
        FREE_STRING(second);
        INCREF(obj1);
        return operation_success(obj1);
    }
    string_builder_t builder;
    init_string_builder(&builder, first.length + second.length);
    append_string_value(&builder, first);
    string_value_t result = append_string_value(&builder, second);
    FREE_STRING(second);
    return operation_success(create_string_object(process, result));
}

/** @brief Implements @ref object_vtbl_t::get_boolean_value. */

static bool get_boolean_value(const object_t *obj) {
    string_value_t value = convert_object_to_string(obj);
    return value.length > 0;
}

/** @brief Virtual table defining the behavior of the static string object. */
static object_vtbl_t static_string_vtbl = {.type = TYPE_STRING,
                                           .inc_ref = stub_memory_function,
                                           .dec_ref = stub_memory_function,
                                           .mark = stub_memory_function,
                                           .sweep = no_sweep,
                                           .release = stub_memory_function,
                                           .compare = compare,
                                           .clone = clone,
                                           .to_string = static_to_string,
                                           .to_string_notation = to_string_notation,
                                           .get_prototypes = get_prototypes,
                                           .get_topology = get_topology,
                                           .get_keys = get_keys,
                                           .get_property = static_get_property,
                                           .create_property = create_property_on_immutable,
                                           .set_property = set_property_on_immutable,
                                           .unary_plus = stub_unary_operation,
                                           .unary_minus = stub_unary_operation,
                                           .increment = stub_unary_operation,
                                           .decrement = stub_unary_operation,
                                           .add = add,
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
                                           .get_boolean_value = get_boolean_value,
                                           .get_integer_value = stub_get_integer_value,
                                           .get_real_value = stub_get_real_value,
                                           .call = stub_call};

/** @brief Macro to declare a static string object and provide access to it. */
#define DECLARE_STATIC_STRING(name, string)                                                        \
    static object_static_string_t name = {{&static_string_vtbl, NULL, NULL, NULL},                 \
                                          {(string), sizeof(string) / sizeof(wchar_t) - 1}};       \
    object_t *get_##name() {                                                                       \
        return &name.base;                                                                         \
    }

/** @brief Declares some common static string objects. */
DECLARE_STATIC_STRING(empty_string, L"")
DECLARE_STATIC_STRING(string_exceptions, L"Exceptions")
DECLARE_STATIC_STRING(exception_division_by_zero, L"DIVISION_BY_ZERO")
DECLARE_STATIC_STRING(exception_immutable_object, L"IMMUTABLE_OBJECT")
DECLARE_STATIC_STRING(exception_invalid_argument, L"INVALID_ARGUMENT")
DECLARE_STATIC_STRING(exception_invalid_operation, L"INVALID_OPERATION")
DECLARE_STATIC_STRING(exception_property_already_exists, L"PROPERTY_ALREADY_EXISTS")
DECLARE_STATIC_STRING(exception_property_is_constant, L"PROPERTY_IS_CONSTANT")
DECLARE_STATIC_STRING(exception_property_not_found, L"PROPERTY_NOT_FOUND")
DECLARE_STATIC_STRING(string_atan, L"atan")
DECLARE_STATIC_STRING(string_length, L"length")
DECLARE_STATIC_STRING(string_pi, L"pi")
DECLARE_STATIC_STRING(string_print, L"print")
DECLARE_STATIC_STRING(string_sign, L"sign")
DECLARE_STATIC_STRING(string_sqrt, L"sqrt")

/** @brief Virtual table defining the behavior of the dynamic string object. */
static object_vtbl_t dynamic_string_vtbl = {.type = TYPE_STRING,
                                            .inc_ref = inc_ref,
                                            .dec_ref = dec_ref,
                                            .mark = mark,
                                            .sweep = sweep,
                                            .release = release,
                                            .compare = compare,
                                            .clone = clone,
                                            .to_string = dynamic_to_string,
                                            .to_string_notation = to_string_notation,
                                            .get_prototypes = get_prototypes,
                                            .get_topology = get_topology,
                                            .get_keys = get_keys,
                                            .get_property = dynamic_get_property,
                                            .create_property = create_property_on_immutable,
                                            .set_property = set_property_on_immutable,
                                            .unary_plus = stub_unary_operation,
                                            .unary_minus = stub_unary_operation,
                                            .increment = stub_unary_operation,
                                            .decrement = stub_unary_operation,
                                            .add = add,
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
                                            .get_boolean_value = get_boolean_value,
                                            .get_integer_value = stub_get_integer_value,
                                            .get_real_value = stub_get_real_value,
                                            .call = stub_call};

object_t *create_string_object(process_t *process, string_value_t value) {
    if (value.length == 0) {
        FREE_STRING(value);
        return get_empty_string();
    }
    object_dynamic_string_t *obj;
    if (process->dynamic_strings.size > 0) {
        obj = (object_dynamic_string_t *)remove_first_object_from_list(&process->dynamic_strings);
    } else {
        obj = (object_dynamic_string_t *)CALLOC(sizeof(object_dynamic_string_t));
        obj->base.vtbl = &dynamic_string_vtbl;
        obj->base.process = process;
    }
    obj->refs = 1;
    obj->state = UNMARKED;
    if (value.should_free) {
        obj->string.data = value.data;
    } else {
        wchar_t *copy = ALLOC((value.length + 1) * sizeof(wchar_t));
        wmemcpy(copy, value.data, value.length);
        copy[value.length] = 0;
        obj->string.data = copy;
    }
    obj->string.length = value.length;
    add_object_to_list(&process->objects, &obj->base);
    return &obj->base;
}
