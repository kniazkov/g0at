/**
 * @file integer.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementations of an object representing an integer.
 */

#include "common_methods.h"
#include "lib/allocate.h"
#include "lib/bitwise.h"
#include "lib/integer_math.h"
#include "lib/string_ext.h"
#include "object.h"
#include "object_state.h"
#include "process.h"

#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>

/**
 * @brief Defines the maximum capacity of the object pool.
 *
 * Sets the maximum number of objects that can be stored in the object pool before it reaches its
 * capacity.
 */
#define POOL_CAPACITY 1024

/** @brief A static integer object. */
typedef struct {
    object_t base; ///< The base object that provides common functionality.
    int64_t value; ///< The integer value of the object.
} object_static_integer_t;

/** @brief A dynamic integer object. */
typedef struct {
    object_t base;        ///< The base object that provides common functionality.
    int refs;             ///< Reference count.
    object_state_t state; ///< The state of the object (e.g., unmarked, marked, or zombie).
    int64_t value;        ///< The integer value of the object.
} object_dynamic_integer_t;

/** @brief Implements @ref object_vtbl_t::get_prototypes. */
static object_array_t proto_get_prototypes(const object_t *obj) {
    static object_t *proto = NULL;
    if (!proto) {
        proto = get_numeric_proto();
    }
    object_array_t result = {.items = &proto, .size = 1};
    return result;
}

/** @brief Implements @ref object_vtbl_t::get_topology. */
static object_array_t proto_get_topology(const object_t *obj) {
    static object_t *topology[2] = {0};
    if (topology[0] == NULL) {
        topology[0] = get_numeric_proto();
        topology[1] = get_root_object();
    }
    object_array_t result = {.items = topology, .size = 2};
    return result;
}

/** @brief Implements @ref object_vtbl_t::get_keys. */
static object_array_t get_keys(const object_t *obj) {
    return (object_array_t){NULL, 0};
}

/** @brief Implements @ref object_vtbl_t::get_property. */
static object_t *get_property(const object_t *obj, const object_t *key) {
    return NULL;
}

/** @brief Virtual table defining the behavior of the integer prototype object. */
static object_vtbl_t integer_proto_vtbl = {.type = TYPE_OTHER,
                                           .inc_ref = stub_memory_function,
                                           .dec_ref = stub_memory_function,
                                           .mark = stub_memory_function,
                                           .sweep = no_sweep,
                                           .release = stub_memory_function,
                                           .compare = compare_object_addresses,
                                           .clone = clone_singleton,
                                           .to_string = common_to_string,
                                           .to_string_notation = common_to_string_notation,
                                           .get_prototypes = proto_get_prototypes,
                                           .get_topology = proto_get_topology,
                                           .get_keys = get_keys,
                                           .get_property = get_property,
                                           .create_property = create_property_on_immutable,
                                           .set_property = set_property_on_immutable,
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

/** @brief The integer prototype object. */
static object_t integer_proto = {.vtbl = &integer_proto_vtbl};

object_t *get_integer_proto() {
    return &integer_proto;
}

/** @brief Releases or clears a dynamic integer object. */
static void release_or_clear(object_dynamic_integer_t *diobj) {
    remove_object_from_list(&diobj->base.process->objects, &diobj->base);
    if (diobj->base.process->integers.size == POOL_CAPACITY) {
        FREE(diobj);
    } else {
        diobj->refs = 0;
        diobj->state = ZOMBIE;
        diobj->value = 0;
        add_object_to_list(&diobj->base.process->integers, &diobj->base);
    }
}

/** @brief Implements @ref object_vtbl_t::inc_ref. */
static void inc_ref(object_t *obj) {
    object_dynamic_integer_t *diobj = (object_dynamic_integer_t *)obj;
    assert(diobj->state != ZOMBIE);
    diobj->refs++;
}

/** @brief Implements @ref object_vtbl_t::dec_ref. */
static void dec_ref(object_t *obj) {
    object_dynamic_integer_t *diobj = (object_dynamic_integer_t *)obj;
    assert(diobj->state != ZOMBIE);
    if (!(--diobj->refs)) {
        release_or_clear(diobj);
    }
}

/** @brief Implements @ref object_vtbl_t::mark. */
static void mark(object_t *obj) {
    object_dynamic_integer_t *diobj = (object_dynamic_integer_t *)obj;
    assert(diobj->state != ZOMBIE);
    diobj->state = MARKED;
}

/** @brief Implements @ref object_vtbl_t::sweep. */
static bool sweep(object_t *obj) {
    object_dynamic_integer_t *diobj = (object_dynamic_integer_t *)obj;
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
    object_dynamic_integer_t *diobj = (object_dynamic_integer_t *)obj;
    remove_object_from_list(diobj->state == ZOMBIE ? &obj->process->integers
                                                   : &obj->process->objects,
                            obj);
    FREE(obj);
}

static operation_result_t bitwise_not(process_t *process, object_t *obj) {
    return (operation_result_t){
        create_integer_object(process, invert_integer(get_object_integer_value(obj).value)),
        false};
}

static operation_result_t
bitwise_binary(process_t *process, object_t *left, object_t *right, bitwise_kind_t kind) {
    if (!is_integer_object(right))
        return operation_exception(get_exception_invalid_argument());
    int64_t a = get_object_integer_value(left).value, b = get_object_integer_value(right).value;
    if ((kind == BIT_SHIFT_LEFT || kind == BIT_SHIFT_RIGHT) && (b < 0 || b > 63))
        return operation_exception(get_exception_invalid_argument());
    return (operation_result_t){create_integer_object(process, bitwise_integer(a, b, kind)), false};
}

static operation_result_t bitwise_and(process_t *process, object_t *left, object_t *right) {
    return bitwise_binary(process, left, right, BIT_AND);
}

static operation_result_t bitwise_or(process_t *process, object_t *left, object_t *right) {
    return bitwise_binary(process, left, right, BIT_OR);
}

static operation_result_t bitwise_xor(process_t *process, object_t *left, object_t *right) {
    return bitwise_binary(process, left, right, BIT_XOR);
}

static operation_result_t shift_left(process_t *process, object_t *left, object_t *right) {
    return bitwise_binary(process, left, right, BIT_SHIFT_LEFT);
}

static operation_result_t shift_right(process_t *process, object_t *left, object_t *right) {
    return bitwise_binary(process, left, right, BIT_SHIFT_RIGHT);
}

/** @brief Implements @ref object_vtbl_t::clone. */
static object_t *clone(process_t *process, object_t *obj) {
    if (process == obj->process) {
        return obj;
    }
    return create_integer_object(process, get_object_integer_value(obj).value);
}

/** @brief Implements @ref object_vtbl_t::to_string. */
static string_value_t to_string(const object_t *obj) {
    int64_t value = get_object_integer_value(obj).value;
    return format_string(L"%ld", value);
}

/** @brief Implements @ref object_vtbl_t::to_string_notation. */
static string_value_t to_string_notation(const object_t *obj) {
    return to_string(obj);
}

/** @brief Array of prototypes for the integer object. */
static object_t *prototypes[] = {&integer_proto};

/** @brief Implements @ref object_vtbl_t::get_prototypes. */
static object_array_t get_prototypes(const object_t *obj) {
    object_array_t result = {.items = prototypes, .size = 1};
    return result;
}

/** @brief Implements @ref object_vtbl_t::get_topology. */
static object_array_t get_topology(const object_t *obj) {
    static object_t *topology[3] = {0};
    if (topology[0] == NULL) {
        topology[0] = &integer_proto;
        topology[1] = get_numeric_proto();
        topology[2] = get_root_object();
    }
    object_array_t result = {.items = topology, .size = 3};
    return result;
}

/** @brief Implements @ref object_vtbl_t::unary_minus. */
static operation_result_t unary_minus(process_t *process, object_t *obj) {
    return operation_success(
        create_integer_object(process,
                              subtract_int64_saturating(0, get_object_integer_value(obj).value)));
}

/** @brief Implements @ref object_vtbl_t::increment. */
static operation_result_t increment(process_t *process, object_t *obj) {
    return operation_success(
        create_integer_object(process,
                              add_int64_saturating(get_object_integer_value(obj).value, 1)));
}

/** @brief Implements @ref object_vtbl_t::decrement. */
static operation_result_t decrement(process_t *process, object_t *obj) {
    return operation_success(
        create_integer_object(process,
                              subtract_int64_saturating(get_object_integer_value(obj).value, 1)));
}

/** @brief Implements @ref object_vtbl_t::add. */
static operation_result_t add(process_t *process, object_t *obj1, object_t *obj2) {
    int_value_t first = get_object_integer_value(obj1);
    if (is_integer_object(obj2)) {
        int_value_t second_int = get_object_integer_value(obj2);
        return operation_success(
            create_integer_object(process, add_int64_saturating(first.value, second_int.value)));
    }
    real_value_t second_real = get_object_real_value(obj2);
    if (second_real.has_value) {
        return operation_success(
            create_real_number_object(process, integer_to_double(first.value) + second_real.value));
    }
    return operation_exception(get_exception_invalid_argument());
}

/** @brief Implements @ref object_vtbl_t::subtract. */
static operation_result_t subtract(process_t *process, object_t *obj1, object_t *obj2) {
    int_value_t first = get_object_integer_value(obj1);
    if (is_integer_object(obj2)) {
        int_value_t second_int = get_object_integer_value(obj2);
        return operation_success(
            create_integer_object(process,
                                  subtract_int64_saturating(first.value, second_int.value)));
    }
    real_value_t second_real = get_object_real_value(obj2);
    if (second_real.has_value) {
        return operation_success(
            create_real_number_object(process, integer_to_double(first.value) - second_real.value));
    }
    return operation_exception(get_exception_invalid_argument());
}

/** @brief Implements @ref object_vtbl_t::multiply. */
static operation_result_t multiply(process_t *process, object_t *obj1, object_t *obj2) {
    int_value_t first = get_object_integer_value(obj1);
    if (is_integer_object(obj2)) {
        int_value_t second_int = get_object_integer_value(obj2);
        return operation_success(
            create_integer_object(process,
                                  multiply_int64_saturating(first.value, second_int.value)));
    }
    real_value_t second_real = get_object_real_value(obj2);
    if (second_real.has_value) {
        return operation_success(
            create_real_number_object(process, integer_to_double(first.value) * second_real.value));
    }
    return operation_exception(get_exception_invalid_argument());
}

/** @brief Implements @ref object_vtbl_t::divide. */
static operation_result_t divide(process_t *process, object_t *obj1, object_t *obj2) {
    int64_t first = get_object_integer_value(obj1).value;
    if (is_integer_object(obj2)) {
        int64_t second = get_object_integer_value(obj2).value;
        if (second == 0)
            return operation_exception(get_exception_division_by_zero());
        /* Neither / nor % is defined for this pair in C. */
        if (first == INT64_MIN && second == -1)
            return operation_success(create_integer_object(process, INT64_MAX));
        if (first % second == 0)
            return operation_success(create_integer_object(process, first / second));
        return operation_success(
            create_real_number_object(process,
                                      integer_to_double(first) / integer_to_double(second)));
    }
    real_value_t second = get_object_real_value(obj2);
    if (!second.has_value)
        return operation_exception(get_exception_invalid_argument());
    if (second.value == 0)
        return operation_exception(get_exception_division_by_zero());
    return operation_success(
        create_real_number_object(process, integer_to_double(first) / second.value));
}

/** @brief Implements @ref object_vtbl_t::modulo. */
static operation_result_t modulo(process_t *process, object_t *obj1, object_t *obj2) {
    int_value_t first = get_object_integer_value(obj1);
    if (is_integer_object(obj2)) {
        int_value_t second_int = get_object_integer_value(obj2);
        if (second_int.value == 0)
            return operation_exception(get_exception_division_by_zero());
        if (first.value == INT64_MIN && second_int.value == -1)
            return operation_success(get_integer_zero());
        return operation_success(create_integer_object(process, first.value % second_int.value));
    }
    return operation_exception(get_exception_invalid_argument());
}

/** @brief Implements @ref object_vtbl_t::power. */
static operation_result_t power(process_t *process, object_t *obj1, object_t *obj2) {
    int_value_t first = get_object_integer_value(obj1);
    real_value_t second = get_object_real_value(obj2);
    if (second.has_value) {
        return operation_success(
            create_real_number_object(process, pow(integer_to_double(first.value), second.value)));
    }
    return operation_exception(get_exception_invalid_argument());
}

/** @brief Implements @ref object_vtbl_t::get_boolean_value. */
static bool get_boolean_value(const object_t *obj) {
    return get_object_integer_value(obj).value != 0;
}

/** @brief Implements @ref object_vtbl_t::get_integer_value. */
static int_value_t static_get_integer_value(const object_t *obj) {
    object_static_integer_t *siobj = (object_static_integer_t *)obj;
    return (int_value_t){true, siobj->value};
}

/** @brief Implements @ref object_vtbl_t::get_integer_value. */
static int_value_t dynamic_get_integer_value(const object_t *obj) {
    object_dynamic_integer_t *diobj = (object_dynamic_integer_t *)obj;
    return (int_value_t){true, diobj->value};
}

/** @brief Implements @ref object_vtbl_t::get_real_value. */
static real_value_t static_get_real_value(const object_t *obj) {
    object_static_integer_t *siobj = (object_static_integer_t *)obj;
    return (real_value_t){true, (double)siobj->value};
}

/** @brief Implements @ref object_vtbl_t::get_real_value. */
static real_value_t dynamic_get_real_value(const object_t *obj) {
    object_dynamic_integer_t *diobj = (object_dynamic_integer_t *)obj;
    return (real_value_t){true, (double)diobj->value};
}

/** @brief This virtual table defines the behavior of the static integer object. */
static object_vtbl_t static_vtbl = {.type = TYPE_NUMBER,
                                    .inc_ref = stub_memory_function,
                                    .dec_ref = stub_memory_function,
                                    .mark = stub_memory_function,
                                    .sweep = no_sweep,
                                    .release = stub_memory_function,
                                    .compare = compare_numeric_keys,
                                    .clone = clone,
                                    .to_string = to_string,
                                    .to_string_notation = to_string_notation,
                                    .get_prototypes = get_prototypes,
                                    .get_topology = get_topology,
                                    .get_keys = get_keys,
                                    .get_property = get_property,
                                    .create_property = create_property_on_immutable,
                                    .set_property = set_property_on_immutable,
                                    .bitwise_not = bitwise_not,
                                    .bitwise_and = bitwise_and,
                                    .bitwise_or = bitwise_or,
                                    .bitwise_xor = bitwise_xor,
                                    .shift_left = shift_left,
                                    .shift_right = shift_right,
                                    .unary_plus = numeric_unary_plus,
                                    .unary_minus = unary_minus,
                                    .increment = increment,
                                    .decrement = decrement,
                                    .add = add,
                                    .subtract = subtract,
                                    .multiply = multiply,
                                    .divide = divide,
                                    .modulo = modulo,
                                    .power = power,
                                    .less = common_less,
                                    .less_or_equal = common_less_or_equal,
                                    .greater = common_greater,
                                    .greater_or_equal = common_greater_or_equal,
                                    .equal = common_equal,
                                    .not_equal = common_not_equal,
                                    .get_boolean_value = get_boolean_value,
                                    .get_integer_value = static_get_integer_value,
                                    .get_real_value = static_get_real_value,
                                    .call = stub_call};

/** @brief The total number of static integer objects. */
#define STATIC_INTEGER_RANGE (MAX_STATIC_INTEGER - MIN_STATIC_INTEGER + 1)

/** @brief Static array of objects representing static integer values. */
static object_static_integer_t static_integers[STATIC_INTEGER_RANGE];

/** @brief Flag to indicate whether the static integers array has been initialized. */
static bool is_static_integers_initialized = false;

/** @brief Initializes the static integer objects array. */
static void initialize_static_integers() {
    for (int i = MIN_STATIC_INTEGER; i <= MAX_STATIC_INTEGER; ++i) {
        static_integers[i - MIN_STATIC_INTEGER] =
            (object_static_integer_t){{&static_vtbl, NULL, NULL, NULL}, i};
    }
    is_static_integers_initialized = true;
}

object_t *get_static_integer_object(int value) {
    assert(value >= MIN_STATIC_INTEGER && value <= MAX_STATIC_INTEGER);
    if (!is_static_integers_initialized) {
        initialize_static_integers();
    }
    return &static_integers[value - MIN_STATIC_INTEGER].base;
}

object_t *get_integer_zero() {
    return get_static_integer_object(0);
}

/** @brief This virtual table defines the behavior of the dynamic integer object. */
static object_vtbl_t dynamic_vtbl = {.type = TYPE_NUMBER,
                                     .inc_ref = inc_ref,
                                     .dec_ref = dec_ref,
                                     .mark = mark,
                                     .sweep = sweep,
                                     .release = release,
                                     .compare = compare_numeric_keys,
                                     .clone = clone,
                                     .to_string = to_string,
                                     .to_string_notation = to_string_notation,
                                     .get_prototypes = get_prototypes,
                                     .get_topology = get_topology,
                                     .get_keys = get_keys,
                                     .get_property = get_property,
                                     .create_property = create_property_on_immutable,
                                     .set_property = set_property_on_immutable,
                                     .bitwise_not = bitwise_not,
                                     .bitwise_and = bitwise_and,
                                     .bitwise_or = bitwise_or,
                                     .bitwise_xor = bitwise_xor,
                                     .shift_left = shift_left,
                                     .shift_right = shift_right,
                                     .unary_plus = numeric_unary_plus,
                                     .unary_minus = unary_minus,
                                     .increment = increment,
                                     .decrement = decrement,
                                     .add = add,
                                     .subtract = subtract,
                                     .multiply = multiply,
                                     .divide = divide,
                                     .modulo = modulo,
                                     .power = power,
                                     .less = common_less,
                                     .less_or_equal = common_less_or_equal,
                                     .greater = common_greater,
                                     .greater_or_equal = common_greater_or_equal,
                                     .equal = common_equal,
                                     .not_equal = common_not_equal,
                                     .get_boolean_value = get_boolean_value,
                                     .get_integer_value = dynamic_get_integer_value,
                                     .get_real_value = dynamic_get_real_value,
                                     .call = stub_call};

object_t *create_integer_object(process_t *process, int64_t value) {
    if (value >= MIN_STATIC_INTEGER && value <= MAX_STATIC_INTEGER) {
        return get_static_integer_object((int)value);
    }
    object_dynamic_integer_t *obj;
    if (process->integers.size > 0) {
        obj = (object_dynamic_integer_t *)remove_first_object_from_list(&process->integers);
    } else {
        obj = (object_dynamic_integer_t *)CALLOC(sizeof(object_dynamic_integer_t));
        obj->base.vtbl = &dynamic_vtbl;
        obj->base.process = process;
    }
    obj->refs = 1;
    obj->state = UNMARKED;
    obj->value = value;
    add_object_to_list(&process->objects, &obj->base);
    return &obj->base;
}

/** @brief Identifies native integer representations, without numeric conversion. */
bool is_integer_object(const object_t *obj) {
    return obj->vtbl == &static_vtbl || obj->vtbl == &dynamic_vtbl;
}
