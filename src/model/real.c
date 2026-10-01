/**
 * @file real.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of an object representing a real number.
 */
#include <assert.h>
#include <stdio.h>
#include <math.h>

#include "object.h"
#include "object_state.h"
#include "process.h"
#include "common_methods.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"

/**
 * @brief Defines the maximum capacity of the object pool.
 *
 * Sets the maximum number of objects that can be stored in the object pool before it reaches its
 * capacity.
 */
#define POOL_CAPACITY 1024

/**
 * @brief A static real number object.
 *
 * Static real numbers are used for mathematical constants and other immutable floating-point values
 * that persist throughout the program execution.
 */
typedef struct {
    object_t base; ///< The base object that provides common functionality.
    double value; ///< The double-precision floating-point value of the object.
} object_static_real_t;

/** @brief A dynamic real number object. */
typedef struct {
    object_t base; ///< The base object that provides common functionality.
    int refs; ///< Reference count for garbage collection.
    object_state_t state; ///< The state of the object (unmarked, marked, or zombie).
    double value; ///< The double-precision floating-point value of the object.
} object_dynamic_real_t;

/** @brief Implements @ref object_vtbl_t::get_prototypes. */
static object_array_t proto_get_prototypes(const object_t *obj) {
    static object_t *proto = NULL;
    if (!proto) {
        proto = get_numeric_proto();
    }
    object_array_t result = {
        .items = &proto,
        .size = 1
    };
    return result;
}

/** @brief Implements @ref object_vtbl_t::get_topology. */
static object_array_t proto_get_topology(const object_t *obj) {
    static object_t* topology[2] = {0};
    if (topology[0] == NULL) {
        topology[0] = get_numeric_proto();
        topology[1] = get_root_object();
    }
    object_array_t result = {
        .items = topology,
        .size = 2
    };
    return result;
}

/** @brief Implements @ref object_vtbl_t::get_keys. */
static object_array_t get_keys(const object_t *obj) {
    return (object_array_t){ NULL, 0 };
}

/** @brief Implements @ref object_vtbl_t::get_property. */
static object_t *get_property(const object_t *obj, const object_t *key) {
    return NULL;
}

/** @brief Virtual table defining the behavior of the real number prototype object. */
static object_vtbl_t real_proto_vtbl = {
    .type = TYPE_OTHER,
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
    .call = stub_call
};

/** @brief The real number prototype object. */
static object_t real_proto = {
    .vtbl = &real_proto_vtbl
};

object_t *get_real_proto() {
    return &real_proto;
}

/** @brief Releases or clears a dynamic real number object. */
static void release_or_clear(object_dynamic_real_t *drobj) {
    remove_object_from_list(&drobj->base.process->objects, &drobj->base);
    if (drobj->base.process->integers.size == POOL_CAPACITY) {
        FREE(drobj);
    } else {
        drobj->refs = 0;
        drobj->state = ZOMBIE;
        drobj->value = 0;
        add_object_to_list(&drobj->base.process->real_numbers, &drobj->base);
    }
}

/** @brief Implements @ref object_vtbl_t::inc_ref. */
static void inc_ref(object_t *obj) {
    object_dynamic_real_t *drobj = (object_dynamic_real_t *)obj;
    assert(drobj->state != ZOMBIE);
    drobj->refs++;
}

/** @brief Implements @ref object_vtbl_t::dec_ref. */
static void dec_ref(object_t *obj) {
    object_dynamic_real_t *drobj = (object_dynamic_real_t *)obj;
    assert(drobj->state != ZOMBIE);
    if (!(--drobj->refs)) {
        release_or_clear(drobj);
    }
}

/** @brief Implements @ref object_vtbl_t::mark. */
static void mark(object_t *obj) {
    object_dynamic_real_t *drobj = (object_dynamic_real_t *)obj;
    assert(drobj->state != ZOMBIE);
    drobj->state = MARKED;
}

/** @brief Implements @ref object_vtbl_t::sweep. */
static bool sweep(object_t *obj) {
    object_dynamic_real_t *drobj = (object_dynamic_real_t *)obj;
    assert(drobj->state != ZOMBIE);
    if (drobj->state == UNMARKED) {
        release_or_clear(drobj);
        return true;
    } else {
        drobj->state = UNMARKED;
        return false;
    }
}

/** @brief Implements @ref object_vtbl_t::release. */
static void release(object_t *obj) {
    object_dynamic_real_t *diobj = (object_dynamic_real_t *)obj;
    remove_object_from_list(
        diobj->state == ZOMBIE ? &obj->process->real_numbers : &obj->process->objects, obj
    );
    FREE(obj);
}

/** @brief Implements @ref object_vtbl_t::compare. */
static int compare(const object_t *obj1, const object_t *obj2) {
    double diff = get_object_real_value(obj1).value
        - get_object_real_value(obj2).value;
    if (diff > 0) {
        return 1;
    } else if (diff < 0) {
        return -1;
    } else {
        return 0;
    }
}

/** @brief Implements @ref object_vtbl_t::clone. */
static object_t *clone(process_t *process, object_t *obj) {
    if (process == obj->process) {
        return obj;
    }
    return create_real_number_object(process, get_object_real_value(obj).value);
}

/** @brief Implements @ref object_vtbl_t::to_string. */
static string_value_t to_string(const object_t *obj) {
    double value = get_object_real_value(obj).value;
    return format_string(L"%f", value);
}

/** @brief Implements @ref object_vtbl_t::to_string_notation. */
static string_value_t to_string_notation(const object_t *obj) {
    return to_string(obj);
}

/** @brief Array of prototypes for the real number object. */
static object_t* prototypes[] = {
    &real_proto
};

/** @brief Implements @ref object_vtbl_t::get_prototypes. */
static object_array_t get_prototypes(const object_t *obj) {
    object_array_t result = {
        .items = prototypes,
        .size = 1
    };
    return result;
}

/** @brief Implements @ref object_vtbl_t::get_topology. */
static object_array_t get_topology(const object_t *obj) {
    static object_t* topology[3] = {0};
    if (topology[0] == NULL) {
        topology[0] = &real_proto;
        topology[1] = get_numeric_proto();
        topology[2] = get_root_object();
    }
    object_array_t result = {
        .items = topology,
        .size = 3
    };
    return result;
}

/** @brief Implements @ref object_vtbl_t::add. */
static object_t *add(process_t *process, object_t *obj1, object_t *obj2) {
    real_value_t first = get_object_real_value(obj1);
    real_value_t second = get_object_real_value(obj2);
    if (!second.has_value) {
        return NULL;
    }
    return create_real_number_object(process, first.value + second.value);
}

/** @brief Implements @ref object_vtbl_t::subtract. */
static object_t *subtract(process_t *process, object_t *obj1, object_t *obj2) {
    real_value_t first = get_object_real_value(obj1);
    real_value_t second = get_object_real_value(obj2);
    if (!second.has_value) {
        return NULL;
    }
    return create_real_number_object(process, first.value - second.value);
}

/** @brief Implements @ref object_vtbl_t::multiply. */
static object_t *multiply(process_t *process, object_t *obj1, object_t *obj2) {
    real_value_t first = get_object_real_value(obj1);
    real_value_t second = get_object_real_value(obj2);
    if (!second.has_value) {
        return NULL;
    }
    return create_real_number_object(process, first.value * second.value);
}

/** @brief Implements @ref object_vtbl_t::divide. */
static object_t *divide(process_t *process, object_t *obj1, object_t *obj2) {
    real_value_t first = get_object_real_value(obj1);
    real_value_t second = get_object_real_value(obj2);
    if (!second.has_value) {
        return NULL;
    }
    if (second.value == 0) {
        return NULL;
    }
    return create_real_number_object(process, first.value / second.value);
}

/** @brief Implements @ref object_vtbl_t::power. */
static object_t *power(process_t *process, object_t *obj1, object_t *obj2) {
    real_value_t first = get_object_real_value(obj1);
    real_value_t second = get_object_real_value(obj2);
    if (!second.has_value) {
        return NULL;
    }
    return create_real_number_object(process, pow(first.value, second.value));
}

/** @brief Implements @ref object_vtbl_t::get_boolean_value. */
static bool get_boolean_value(const object_t *obj) {
    return get_object_real_value(obj).value != 0.0;
}

/** @brief Implements @ref object_vtbl_t::get_integer_value. */
static int_value_t static_get_integer_value(const object_t *obj) {
    return (int_value_t){ false, 0 };
}

/** @brief Implements @ref object_vtbl_t::get_integer_value. */
static int_value_t dynamic_get_integer_value(const object_t *obj) {
    object_dynamic_real_t *drobj = (object_dynamic_real_t *)obj;
    double value = drobj->value;
    if (value == trunc(value) && value >= (double)INT64_MIN && value <= (double)INT64_MAX) {
        return (int_value_t){ true, (int64_t)value };
    }
    return (int_value_t){ false, 0 };
}

/** @brief Implements @ref object_vtbl_t::get_real_value. */
static real_value_t static_get_real_value(const object_t *obj) {
    object_static_real_t *srobj = (object_static_real_t *)obj;
    return (real_value_t){ true, srobj->value };
}

/** @brief Implements @ref object_vtbl_t::get_real_value. */
static real_value_t dynamic_get_real_value(const object_t *obj) {
    object_dynamic_real_t *drobj = (object_dynamic_real_t *)obj;
    return (real_value_t){ true, drobj->value };
}

/** @brief This virtual table defines the behavior of the static real number object. */
static object_vtbl_t static_vtbl = {
    .type = TYPE_NUMBER,
    .inc_ref = stub_memory_function,
    .dec_ref = stub_memory_function,
    .mark = stub_memory_function,
    .sweep = no_sweep,
    .release = stub_memory_function,
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
    .add = add,
    .subtract = subtract,
    .multiply = multiply,
    .divide = divide,
    .modulo = stub_modulo,
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
    .call = stub_call
};

/** @brief Static real number object representing the mathematical constant π (Pi). */
static object_static_real_t pi_object = {
    .base = {
        .vtbl = &static_vtbl
    },
    .value = M_PI
};

object_t* get_pi_object() {
    return &pi_object.base;
}

/** @brief This virtual table defines the behavior of the dynamic real number object. */
static object_vtbl_t dynamic_vtbl = {
    .type = TYPE_NUMBER,
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
    .add = add,
    .subtract = subtract,
    .multiply = multiply,
    .divide = divide,
    .modulo = stub_modulo,
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
    .call = stub_call
};

object_t *create_real_number_object(process_t *process, double value) {
    object_dynamic_real_t *obj;
    if (process->real_numbers.size > 0) {
        obj = (object_dynamic_real_t *)remove_first_object_from_list(&process->real_numbers);
    } else {
        obj = (object_dynamic_real_t *)CALLOC(sizeof(object_dynamic_real_t));
        obj->base.vtbl = &dynamic_vtbl;
        obj->base.process = process;
    }
    obj->refs = 1;
    obj->state = UNMARKED;
    obj->value = value;
    add_object_to_list(&process->objects, &obj->base);
    return &obj->base;
}
