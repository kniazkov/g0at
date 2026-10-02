/**
 * @file object.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Defines the base structure for all objects in the Goat programming language.
 */

#pragma once

#include "common/types.h"
#include "lib/value.h"
#include "model_status.h"

typedef struct object_t object_t;

/** @brief Owns one reference to a non-NULL result or thrown value, including Goat null. */
typedef struct {
    object_t *value;
    bool is_exception;
} operation_result_t;

/** @brief Transfers an owned value into a normal operation result. */
static inline operation_result_t operation_success(object_t *value) {
    return (operation_result_t){value, false};
}

/** @brief Transfers an owned value into an exceptional operation result. */
static inline operation_result_t operation_exception(object_t *value) {
    return (operation_result_t){value, true};
}

typedef struct process_t process_t;

typedef struct thread_t thread_t;

/** @brief Enumeration of object types in the Goat virtual machine. */
typedef enum {
    /** @brief Boolean type (true/false). */
    TYPE_BOOLEAN = 0,

    /** @brief Numeric type (integer or floating-point numbers). */
    TYPE_NUMBER = 1,

    /** @brief String type (sequence of characters). */
    TYPE_STRING = 2,

    /** @brief User-defined object type (for objects created by the user). */
    TYPE_USER_DEFINED_OBJECT = 3,

    /** @brief Other object type. */
    TYPE_OTHER = 4
} object_type_t;

/**
 * @brief A constant array of object pointers.
 *
 * The array itself is immutable, ensuring that its contents cannot be modified after creation.
 */
typedef struct {
    /**
     * @brief Pointer to a constant array of object pointers.
     *
     * The array itself is immutable, but the objects it points to can be mutable based on their
     * individual types.
     */
    object_t *const *items;

    /** @brief The number of objects in the array. */
    size_t size;
} object_array_t;

/** @brief The virtual table structure for objects in Goat. */
typedef struct {
    /** @brief The type of the object. */
    object_type_t type;

    /**
     * @brief Adding a reference to an object (incrementing its reference count).
     *
     * Increments the reference count of the object to indicate that it is being referenced by
     * another part of the program.
     */
    void (*inc_ref)(object_t *obj);

    /**
     * @brief Removing a reference from an object (decrementing its reference count).
     *
     * Decrements the reference count of the object. When the reference count reaches zero, the
     * object is eligible immediate destruction.
     */
    void (*dec_ref)(object_t *obj);

    /** @brief Marking an object during garbage collection. */
    void (*mark)(object_t *obj);

    /**
     * @brief Sweeping (cleaning up) an object during garbage collection.
     * @return true if the object was either destroyed or moved to object pool (ZOMBIE), false if
     * the object was marked (still alive) and shouldn't be processed.
     */
    bool (*sweep)(object_t *obj);

    /** @brief Releasing (destroying) an object. */
    void (*release)(object_t *obj);

    /** @brief Comparing two objects. */
    int (*compare)(const object_t *obj1, const object_t *obj2);

    /**
     * @brief Creates a clone of the given object.
     *
     * It is used to create an independent copy of an object, owned by the given process.
     * @note The cloned object should be independent of the original. Depending on the type of
     * object, additional cloning operations may be implemented.
     */
    object_t *(*clone)(process_t *process, object_t *obj);

    /**
     * @brief Converting an object to its string representation.
     * @note The returned string is dynamically allocated, and the caller must ensure that the
     * memory is freed after use to avoid memory leaks. The `should_free` flag in the
     * `string_value_t` structure indicates whether the caller should free the memory.
     */
    string_value_t (*to_string)(const object_t *obj);

    /**
     * @brief Converting an object to its Goat notation representation.
     * @note The returned string is dynamically allocated, and the caller must ensure that the
     * memory is freed after use to avoid memory leaks. The `should_free` flag in the
     * `string_value_t` structure indicates whether the caller should free the memory.
     */
    string_value_t (*to_string_notation)(const object_t *obj);

    /** @brief Retrieving the prototypes of an object. */
    object_array_t (*get_prototypes)(const object_t *obj);

    /** @brief Retrieving the full prototype topology of an object. */
    object_array_t (*get_topology)(const object_t *obj);

    /**
     * @brief Retrieves all property keys from an object.
     * @note The memory for the returned array is managed internally by the object, and the caller
     * must not attempt to free or modify it.
     */
    object_array_t (*get_keys)(const object_t *obj);

    /**
     * @brief Retrieves the value of a property from an object.
     *
     * If the property does not exist, the function returns NULL.
     * @return A pointer to the value of the property, or NULL if the property does not exist.
     */
    object_t *(*get_property)(const object_t *obj, const object_t *key);

    /**
     * @brief Adds a new property to an object.
     * `constant`: If `true`, the property will be marked as constant and cannot be modified
     * after creation.
     */
    model_status_t (*create_property)(object_t *obj, object_t *key, object_t *value, bool constant);

    /** @brief Sets a property on an object. */
    model_status_t (*set_property)(object_t *obj, object_t *key, object_t *value);

    /** @brief Unary numeric identity; returns an owned result or exception. */
    operation_result_t (*unary_plus)(process_t *process, object_t *obj);

    /** @brief Unary numeric negation; returns an owned result or exception. */
    operation_result_t (*unary_minus)(process_t *process, object_t *obj);

    /** @brief Numeric successor; returns an owned result or exception. */
    operation_result_t (*increment)(process_t *process, object_t *obj);
    /** @brief Numeric predecessor; returns an owned result or exception. */
    operation_result_t (*decrement)(process_t *process, object_t *obj);

    /** @brief Adding two objects. */
    operation_result_t (*add)(process_t *process, object_t *obj1, object_t *obj2);

    /** @brief Subtracting two objects. */
    operation_result_t (*subtract)(process_t *process, object_t *obj1, object_t *obj2);

    /** @brief Multiplying two objects. */
    operation_result_t (*multiply)(process_t *process, object_t *obj1, object_t *obj2);

    /** @brief Dividing two objects. */
    operation_result_t (*divide)(process_t *process, object_t *obj1, object_t *obj2);

    /**
     * @brief Computing the remainder of division (modulo).
     *
     * Executes the `MOD` operation, returning the remainder after division.
     */
    operation_result_t (*modulo)(process_t *process, object_t *obj1, object_t *obj2);

    /** @brief Integer bitwise inversion. */
    operation_result_t (*bitwise_not)(process_t *process, object_t *obj);
    /** @brief Integer bitwise operation. */
    operation_result_t (*bitwise_and)(process_t *process, object_t *left, object_t *right);
    /** @brief Integer bitwise operation. */
    operation_result_t (*bitwise_or)(process_t *process, object_t *left, object_t *right);
    /** @brief Integer bitwise operation. */
    operation_result_t (*bitwise_xor)(process_t *process, object_t *left, object_t *right);
    /** @brief Integer bitwise operation. */
    operation_result_t (*shift_left)(process_t *process, object_t *left, object_t *right);
    /** @brief Integer bitwise operation. */
    operation_result_t (*shift_right)(process_t *process, object_t *left, object_t *right);

    /** @brief Exponentiation (power). */
    operation_result_t (*power)(process_t *process, object_t *obj1, object_t *obj2);

    /** @brief Returns a boolean result or a comparison exception. */
    operation_result_t (*less)(process_t *process, object_t *obj1, object_t *obj2);

    /** @brief Returns a boolean result or a comparison exception. */
    operation_result_t (*less_or_equal)(process_t *process, object_t *obj1, object_t *obj2);

    /** @brief Returns a boolean result or a comparison exception. */
    operation_result_t (*greater)(process_t *process, object_t *obj1, object_t *obj2);

    /** @brief Returns a boolean result or a comparison exception. */
    operation_result_t (*greater_or_equal)(process_t *process, object_t *obj1, object_t *obj2);

    /** @brief Returns a boolean result or a comparison exception. */
    operation_result_t (*equal)(process_t *process, object_t *obj1, object_t *obj2);

    /** @brief Returns a boolean result or a comparison exception. */
    operation_result_t (*not_equal)(process_t *process, object_t *obj1, object_t *obj2);

    /**
     * @brief Retrieving the boolean value of an object.
     *
     * For example, zero, `null`, or empty values might be considered `false`, while others are
     * `true`.
     */
    bool (*get_boolean_value)(const object_t *obj);

    /** @brief Converts to an integer; has_value == false means conversion is unavailable. */
    int_value_t (*get_integer_value)(const object_t *obj);

    /** @brief Converts to a real; has_value == false means conversion is unavailable. */
    real_value_t (*get_real_value)(const object_t *obj);

    /**
     * @brief Invoking a function object.
     * @return `true` if the object is a functional object and the call was performed, `false`
     * otherwise.
     */
    bool (*call)(object_t *obj, uint16_t arg_count, thread_t *thread);
} object_vtbl_t;

/**
 * @brief The base object structure in Goat.
 *
 * All objects, whether primitive types, functions, or other user-defined types, share this common
 * structure, which includes a pointer to their virtual table.
 */
struct object_t {
    /** @brief Pointer to the object's virtual table. */
    object_vtbl_t *vtbl;

    /**
     * @brief Pointer to the process that owns this object.
     *
     * Each object is associated with a process that manages its lifetime.
     */
    process_t *process;

    /** @brief Pointer to the previous object in the list. */
    object_t *previous;

    /** @brief Pointer to the next object in the list. */
    object_t *next;
};

/**
 * @brief Macro to increment the reference count of an object.
 *
 * Increments the reference count of the object by calling the `inc_ref` function through the
 * object's virtual table.
 * `obj`: The object whose reference count is to be incremented.
 */
#define INCREF(obj) (((object_t *)(obj))->vtbl->inc_ref((object_t *)(obj)))

/**
 * @brief Macro to decrement the reference count of an object.
 *
 * Decrements the reference count of the object by calling the `dec_ref` function through the
 * object's virtual table. If the reference count reaches zero, the object is released or cleared.
 * `obj`: The object whose reference count is to be decremented.
 */
#define DECREF(obj) (((object_t *)(obj))->vtbl->dec_ref((object_t *)(obj)))

/** @brief Decrements the reference count unless the pointer is NULL. */
#define DECREFIF(obj)                                                                              \
    if ((obj) != NULL) {                                                                           \
        ((object_t *)(obj))->vtbl->dec_ref((object_t *)(obj));                                     \
    }

/** @brief Marks an object during garbage collection. */
static inline void mark_object(object_t *obj) {
    obj->vtbl->mark(obj);
}

/**
 * @brief Sweeps an object during garbage collection.
 * @return `true` if the object was destroyed or moved to object pool, `false` if it remains alive.
 */
static inline bool sweep_object(object_t *obj) {
    return obj->vtbl->sweep(obj);
}

/** @brief Releases an object. */
static inline void release_object(object_t *obj) {
    obj->vtbl->release(obj);
}

/**
 * @brief Compares two objects.
 * @return A negative value if `obj1 < obj2`, zero if equal, positive if `obj1 > obj2`.
 */
static inline int compare_objects_using_vtbl(const object_t *obj1, const object_t *obj2) {
    return obj1->vtbl->compare(obj1, obj2);
}

/**
 * @brief Clones an object for a process.
 *
 * This helper dispatches to the object's virtual table and creates a clone owned by the given
 * process.
 */
static inline object_t *clone_object(process_t *process, object_t *obj) {
    return obj->vtbl->clone(process, obj);
}

/** @brief Converts an object to its plain string representation. */
static inline string_value_t convert_object_to_string(const object_t *obj) {
    return obj->vtbl->to_string(obj);
}

/** @brief Converts an object to its Goat notation representation. */
static inline string_value_t convert_object_to_string_notation(const object_t *obj) {
    return obj->vtbl->to_string_notation(obj);
}

/** @brief Gets the immediate prototypes of an object. */
static inline object_array_t get_object_prototypes(const object_t *obj) {
    return obj->vtbl->get_prototypes(obj);
}

/** @brief Gets the full prototype topology of an object. */
static inline object_array_t get_object_topology(const object_t *obj) {
    return obj->vtbl->get_topology(obj);
}

/** @brief Gets all property keys of an object. */
static inline object_array_t get_object_keys(const object_t *obj) {
    return obj->vtbl->get_keys(obj);
}

/**
 * @brief Gets a property value from an object.
 * @return The property value, or NULL if not found.
 */
static inline object_t *get_object_property(const object_t *obj, const object_t *key) {
    return obj->vtbl->get_property(obj, key);
}

/** @brief Creates a property on an object. */
static inline model_status_t
create_object_property(object_t *obj, object_t *key, object_t *value, bool constant) {
    return obj->vtbl->create_property(obj, key, value, constant);
}

/** @brief Sets a property on an object. */
static inline model_status_t set_object_property(object_t *obj, object_t *key, object_t *value) {
    return obj->vtbl->set_property(obj, key, value);
}

/** @brief Returns the numeric successor or an exception. */
static inline operation_result_t increment_object(process_t *process, object_t *obj) {
    return obj->vtbl->increment(process, obj);
}

/** @brief Returns the numeric predecessor or an exception. */
static inline operation_result_t decrement_object(process_t *process, object_t *obj) {
    return obj->vtbl->decrement(process, obj);
}

/** @brief Applies unary plus. */
static inline operation_result_t unary_plus_object(process_t *process, object_t *obj) {
    return obj->vtbl->unary_plus(process, obj);
}

/** @brief Applies unary minus. */
static inline operation_result_t unary_minus_object(process_t *process, object_t *obj) {
    return obj->vtbl->unary_minus(process, obj);
}

/** @brief Adds two objects. */
static inline operation_result_t add_objects(process_t *process, object_t *obj1, object_t *obj2) {
    return obj1->vtbl->add(process, obj1, obj2);
}

/** @brief Subtracts one object from another. */
static inline operation_result_t
subtract_objects(process_t *process, object_t *obj1, object_t *obj2) {
    return obj1->vtbl->subtract(process, obj1, obj2);
}

/** @brief Multiplies two objects. */
static inline operation_result_t
multiply_objects(process_t *process, object_t *obj1, object_t *obj2) {
    return obj1->vtbl->multiply(process, obj1, obj2);
}

/** @brief Divides one object by another. */
static inline operation_result_t
divide_objects(process_t *process, object_t *obj1, object_t *obj2) {
    return obj1->vtbl->divide(process, obj1, obj2);
}

/** @brief Computes the modulo of two objects. */
static inline operation_result_t
modulo_objects(process_t *process, object_t *obj1, object_t *obj2) {
    return obj1->vtbl->modulo(process, obj1, obj2);
}

/** @brief Raises one object to the power of another. */
static inline operation_result_t power_objects(process_t *process, object_t *obj1, object_t *obj2) {
    return obj1->vtbl->power(process, obj1, obj2);
}

/** @brief Dispatches comparison, returning an owned result or exception. */
static inline operation_result_t
is_object_less_than(process_t *process, object_t *obj1, object_t *obj2) {
    return obj1->vtbl->less(process, obj1, obj2);
}

/** @brief Dispatches comparison, returning an owned result or exception. */
static inline operation_result_t
is_object_less_or_equal(process_t *process, object_t *obj1, object_t *obj2) {
    return obj1->vtbl->less_or_equal(process, obj1, obj2);
}

/** @brief Dispatches comparison, returning an owned result or exception. */
static inline operation_result_t
is_object_greater_than(process_t *process, object_t *obj1, object_t *obj2) {
    return obj1->vtbl->greater(process, obj1, obj2);
}

/** @brief Dispatches comparison, returning an owned result or exception. */
static inline operation_result_t
is_object_greater_or_equal(process_t *process, object_t *obj1, object_t *obj2) {
    return obj1->vtbl->greater_or_equal(process, obj1, obj2);
}

/** @brief Dispatches comparison, returning an owned result or exception. */
static inline operation_result_t
are_objects_equal(process_t *process, object_t *obj1, object_t *obj2) {
    return obj1->vtbl->equal(process, obj1, obj2);
}

/** @brief Dispatches comparison, returning an owned result or exception. */
static inline operation_result_t
are_objects_not_equal(process_t *process, object_t *obj1, object_t *obj2) {
    return obj1->vtbl->not_equal(process, obj1, obj2);
}

/** @brief Gets the boolean value of an object. */
static inline bool get_object_boolean_value(const object_t *obj) {
    return obj->vtbl->get_boolean_value(obj);
}

/** @brief Gets the integer value of an object. */
static inline int_value_t get_object_integer_value(const object_t *obj) {
    return obj->vtbl->get_integer_value(obj);
}

/** @brief Gets the real value of an object. */
static inline real_value_t get_object_real_value(const object_t *obj) {
    return obj->vtbl->get_real_value(obj);
}

/**
 * @brief Invokes an object as a function.
 * @return `true` if the call was performed, `false` otherwise.
 */
static inline bool call_object(object_t *obj, uint16_t arg_count, thread_t *thread) {
    return obj->vtbl->call(obj, arg_count, thread);
}

/**
 * @brief Typedef for a function that retrieves a static object.
 *
 * These objects are immutable and exist for the lifetime of the program.
 */
typedef object_t *(*static_object_getter_t)(void);

/**
 * @brief Retrieves the root object of the Goat programming language.
 *
 * The root object has no prototype of its own and defines the base methods and properties shared by
 * all other objects.
 */
object_t *get_root_object();

/** @brief Gets the immutable namespace of built-in exception strings. */
object_t *get_exceptions_object();

/**
 * @brief Retrieves the singleton instance of the `null` object.
 * @return A pointer to the singleton `null` object.
 */
object_t *get_null_object();

/** @brief Retrieves the boolean prototype object. */
object_t *get_boolean_proto();

/**
 * @brief Retrieves the singleton instance for a given boolean value.
 * @return A pointer to the singleton object representing `true` or `false`.
 */
object_t *get_boolean_object(bool value);

/** @brief The minimum static integer value. */
#define MIN_STATIC_INTEGER -1

/** @brief The maximum static integer value. */
#define MAX_STATIC_INTEGER 127

/** @brief Retrieves the prototype for numeric objects (integer and float). */
object_t *get_numeric_proto();

/** @brief Retrieves the integer prototype object. */
object_t *get_integer_proto();

/** @brief Retrieves the real number prototype object. */
object_t *get_real_proto();

/**
 * @brief Retrieves a static integer object.
 *
 * The input value must be within the range `MIN_STATIC_INTEGER` to `MAX_STATIC_INTEGER`; otherwise,
 * an assertion will fail.
 */
object_t *get_static_integer_object(int value);

/** @brief Retrieves a static object representing the integer value `0`. */
object_t *get_integer_zero();

/** @brief Creates or retrieves an integer object. */
object_t *create_integer_object(process_t *process, int64_t value);

/** @brief Tests representation rather than convertibility to an integer. */
bool is_integer_object(const object_t *obj);

/** @brief Gets the singleton instance of the Pi constant object. */
object_t *get_pi_object();

/** @brief Creates a real number object. */
object_t *create_real_number_object(process_t *process, double value);

/** @brief Retrieves the string prototype object. */
object_t *get_string_proto();

/** @brief Caller-owned permanent storage for an immutable string singleton. */
typedef struct {
    object_t base;
    string_view_t string;
} object_static_string_t;

/** @brief Initializes once; storage and text must live for the entire process. */
object_t *get_static_string_object(object_static_string_t *storage, const wchar_t *text);

/** @brief Creates a dynamic string object from a string value. */
object_t *create_string_object(process_t *process, string_value_t value);

/** @brief Creates a new user-defined object. */
object_t *create_user_defined_object(process_t *process, object_array_t proto);

/**
 * @brief Creates a new dynamic function object.
 *
 * The argument names array (`arg_names`) must be allocated by the caller before invoking this
 * function. Ownership of this array is transferred to the function object, and it will be freed
 * internally during object cleanup.
 * `arg_names`: Array of argument name objects. Ownership is transferred.
 */
object_t *create_function_object(process_t *process,
                                 object_t **arg_names,
                                 size_t arg_count,
                                 instr_index_t first_instr_id,
                                 object_t *closure);

/** @brief Macro to declare a getter function for a static object. */
#define DECLARE_STATIC_OBJECT(name) object_t *get_##name();

/** @brief Declares getter functions for common static string objects. */
DECLARE_STATIC_OBJECT(empty_string)
DECLARE_STATIC_OBJECT(string_exceptions)
DECLARE_STATIC_OBJECT(string_atan)
DECLARE_STATIC_OBJECT(string_length)
DECLARE_STATIC_OBJECT(string_pi)
DECLARE_STATIC_OBJECT(string_print)
DECLARE_STATIC_OBJECT(string_sign)
DECLARE_STATIC_OBJECT(string_sqrt)

/** @brief Retrieves the function prototype object. */
object_t *get_function_proto();

object_t *get_function_atan(void);
object_t *get_function_print(void);
object_t *get_function_sign(void);
object_t *get_function_sqrt(void);

/** @brief Stable string values for built-in exceptions; no exception wrappers. */
DECLARE_STATIC_OBJECT(exception_division_by_zero)
DECLARE_STATIC_OBJECT(exception_immutable_object)
DECLARE_STATIC_OBJECT(exception_invalid_argument)
DECLARE_STATIC_OBJECT(exception_invalid_operation)
DECLARE_STATIC_OBJECT(exception_property_already_exists)
DECLARE_STATIC_OBJECT(exception_property_is_constant)
DECLARE_STATIC_OBJECT(exception_property_not_found)

/** @brief Dispatches bitwise inversion with owned result or exception. */
static inline operation_result_t bitwise_not_object(process_t *process, object_t *obj) {
    return obj->vtbl->bitwise_not(process, obj);
}

static inline operation_result_t
bitwise_and_objects(process_t *process, object_t *left, object_t *right) {
    return left->vtbl->bitwise_and(process, left, right);
}

static inline operation_result_t
bitwise_or_objects(process_t *process, object_t *left, object_t *right) {
    return left->vtbl->bitwise_or(process, left, right);
}

static inline operation_result_t
bitwise_xor_objects(process_t *process, object_t *left, object_t *right) {
    return left->vtbl->bitwise_xor(process, left, right);
}

static inline operation_result_t
shift_left_objects(process_t *process, object_t *left, object_t *right) {
    return left->vtbl->shift_left(process, left, right);
}

static inline operation_result_t
shift_right_objects(process_t *process, object_t *left, object_t *right) {
    return left->vtbl->shift_right(process, left, right);
}
