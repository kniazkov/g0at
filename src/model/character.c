/**
 * @file character.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementations of an object representing a character.
 */

#include "common_methods.h"
#include "lib/allocate.h"
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

/** @brief A dynamic character object. */
typedef struct {
    object_t base;        ///< The base object that provides common functionality.
    int refs;             ///< Reference count.
    object_state_t state; ///< The state of the object (e.g., unmarked, marked, or zombie).
    uint32_t value;       ///< The Unicode code point of the character.
} object_dynamic_character_t;

/** @brief Implements @ref object_vtbl_t::get_keys. */
static object_array_t get_keys(const object_t *obj) {
    return (object_array_t){NULL, 0};
}

/** @brief Implements @ref object_vtbl_t::get_property. */
static object_t *get_property(const object_t *obj, const object_t *key) {
    return NULL;
}

/** @brief Virtual table defining the behavior of the character prototype object. */
static object_vtbl_t character_proto_vtbl = {.type = TYPE_OTHER,
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
                                             .get_property = get_property,
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

/** @brief The character prototype object. */
static object_t character_proto = {.vtbl = &character_proto_vtbl};

object_t *get_character_proto() {
    return &character_proto;
}

/** @brief Releases or clears a dynamic character object. */
static void release_or_clear(object_dynamic_character_t *diobj) {
    remove_object_from_list(&diobj->base.process->objects, &diobj->base);
    if (diobj->base.process->characters.size == POOL_CAPACITY) {
        FREE(diobj);
    } else {
        diobj->refs = 0;
        diobj->state = ZOMBIE;
        diobj->value = 0;
        add_object_to_list(&diobj->base.process->characters, &diobj->base);
    }
}

/** @brief Implements @ref object_vtbl_t::inc_ref. */
static void inc_ref(object_t *obj) {
    object_dynamic_character_t *diobj = (object_dynamic_character_t *)obj;
    assert(diobj->state != ZOMBIE);
    diobj->refs++;
}

/** @brief Implements @ref object_vtbl_t::dec_ref. */
static void dec_ref(object_t *obj) {
    object_dynamic_character_t *diobj = (object_dynamic_character_t *)obj;
    assert(diobj->state != ZOMBIE);
    if (!(--diobj->refs)) {
        release_or_clear(diobj);
    }
}

/** @brief Implements @ref object_vtbl_t::mark. */
static void mark(object_t *obj) {
    object_dynamic_character_t *diobj = (object_dynamic_character_t *)obj;
    assert(diobj->state != ZOMBIE);
    diobj->state = MARKED;
}

/** @brief Implements @ref object_vtbl_t::sweep. */
static bool sweep(object_t *obj) {
    object_dynamic_character_t *diobj = (object_dynamic_character_t *)obj;
    assert(diobj->state != ZOMBIE);
    if (diobj->state == UNMARKED) {
        release_or_clear(diobj);
        return true;
    } else {
        diobj->state = UNMARKED;
        return false;
    }
}

/** @brief Implements @ref object_vtbl_t::release. */
static void release(object_t *obj) {
    object_dynamic_character_t *diobj = (object_dynamic_character_t *)obj;
    remove_object_from_list(diobj->state == ZOMBIE ? &obj->process->characters
                                                   : &obj->process->objects,
                            obj);
    FREE(obj);
}

/** @brief Returns the Unicode code point of a character object. */
static uint32_t get_value(const object_t *obj) {
    return ((object_dynamic_character_t *)obj)->value;
}

/** @brief Implements @ref object_vtbl_t::compare. */
static int compare(const object_t *obj1, const object_t *obj2) {
    uint32_t a = get_value(obj1), b = get_value(obj2);
    return (int)(a > b) - (int)(a < b);
}

/** @brief Implements @ref object_vtbl_t::clone. */
static object_t *clone(process_t *process, object_t *obj) {
    if (process == obj->process) {
        return obj;
    }
    return create_character_object(process, get_value(obj));
}

/** @brief Converts a Unicode code point to its string representation. */
static string_value_t character_to_string(uint32_t value) {
    if (sizeof(wchar_t) > 2 || value < 0x10000) {
        wchar_t *buffer = ALLOC(2 * sizeof(wchar_t));
        buffer[0] = (wchar_t)value;
        buffer[1] = 0;
        return (string_value_t){buffer, 1, true};
    }
    value -= 0x10000;
    wchar_t *buffer = ALLOC(3 * sizeof(wchar_t));
    buffer[0] = (wchar_t)(0xD800 + (value >> 10));
    buffer[1] = (wchar_t)(0xDC00 + (value & 0x3FF));
    buffer[2] = 0;
    return (string_value_t){buffer, 2, true};
}

/** @brief Converts a Unicode code point to its single-quoted notation. */
static string_value_t character_to_string_notation(uint32_t value) {
    string_builder_t builder;
    init_string_builder(&builder, 4);
    append_char(&builder, L'\'');
    switch (value) {
        case L'\r':
            append_substring(&builder, L"\\r", 2);
            break;
        case L'\n':
            append_substring(&builder, L"\\n", 2);
            break;
        case L'\t':
            append_substring(&builder, L"\\t", 2);
            break;
        case L'\b':
            append_substring(&builder, L"\\b", 2);
            break;
        case L'\\':
            append_substring(&builder, L"\\\\", 2);
            break;
        case L'\'':
            append_substring(&builder, L"\\'", 2);
            break;
        case L'"':
            append_substring(&builder, L"\\\"", 2);
            break;
        default:
            if (sizeof(wchar_t) > 2 || value < 0x10000) {
                append_char(&builder, (wchar_t)value);
            } else {
                value -= 0x10000;
                append_char(&builder, (wchar_t)(0xD800 + (value >> 10)));
                append_char(&builder, (wchar_t)(0xDC00 + (value & 0x3FF)));
            }
            break;
    }
    return append_char(&builder, L'\'');
}

/** @brief Implements @ref object_vtbl_t::to_string. */
static string_value_t to_string(const object_t *obj) {
    return character_to_string(get_value(obj));
}

/** @brief Implements @ref object_vtbl_t::to_string_notation. */
static string_value_t to_string_notation(const object_t *obj) {
    return character_to_string_notation(get_value(obj));
}

/** @brief Array of prototypes for the character object. */
static object_t *prototypes[] = {&character_proto};

/** @brief Implements @ref object_vtbl_t::get_prototypes. */
static object_array_t get_prototypes(const object_t *obj) {
    object_array_t result = {.items = prototypes, .size = 1};
    return result;
}

/** @brief Implements @ref object_vtbl_t::get_topology. */
static object_array_t get_topology(const object_t *obj) {
    static object_t *topology[2] = {0};
    if (topology[0] == NULL) {
        topology[0] = &character_proto;
        topology[1] = get_root_object();
    }
    object_array_t result = {.items = topology, .size = 2};
    return result;
}

/** @brief Implements @ref object_vtbl_t::add. */
static operation_result_t add(process_t *process, object_t *obj1, object_t *obj2) {
    string_value_t first = convert_object_to_string(obj1);
    string_value_t second = convert_object_to_string(obj2);
    string_builder_t builder;
    init_string_builder(&builder, first.length + second.length);
    append_string_value(&builder, first);
    string_value_t result = append_string_value(&builder, second);
    FREE_STRING(second);
    FREE_STRING(first);
    return operation_success(create_string_object(process, result));
}

/** @brief Implements @ref object_vtbl_t::get_boolean_value. */
static bool get_boolean_value(const object_t *obj) {
    return get_value(obj) != 0;
}

/** @brief Implements @ref object_vtbl_t::get_integer_value. */
static int_value_t get_integer_value(const object_t *obj) {
    return (int_value_t){true, get_value(obj)};
}

/** @brief This virtual table defines the behavior of the dynamic character object. */
static object_vtbl_t character_vtbl = {.type = TYPE_CHARACTER,
                                       .inc_ref = inc_ref,
                                       .dec_ref = dec_ref,
                                       .mark = mark,
                                       .sweep = sweep,
                                       .release = release,
                                       .compare = compare,
                                       .clone = clone,
                                       .to_string = to_string,
                                       .to_string_notation = to_string_notation,
                                       .get_prototypes = get_prototypes,
                                       .get_topology = get_topology,
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
                                       .shift_left = stub_bitwise,
                                       .shift_right = stub_bitwise,
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
                                       .get_integer_value = get_integer_value,
                                       .get_real_value = stub_get_real_value,
                                       .call = stub_call};

object_t *create_character_object(process_t *process, uint32_t value) {
    object_dynamic_character_t *obj;
    if (process->characters.size > 0) {
        obj = (object_dynamic_character_t *)remove_first_object_from_list(&process->characters);
    } else {
        obj = (object_dynamic_character_t *)CALLOC(sizeof(object_dynamic_character_t));
        obj->base.vtbl = &character_vtbl;
        obj->base.process = process;
    }
    obj->refs = 1;
    obj->state = UNMARKED;
    obj->value = value;
    add_object_to_list(&process->objects, &obj->base);
    return &obj->base;
}

/** @brief Identifies character representations, without numeric conversion. */
bool is_character_object(const object_t *obj) {
    return obj->vtbl == &character_vtbl;
}
