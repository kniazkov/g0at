/**
 * @file lattice.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Abstract values used by static analysis.
 * TOP denotes any value; BOTTOM denotes an impossible value. Join combines paths;
 * meet intersects constraints. Payloads are arena-owned; generic elements are singletons.
 */
#pragma once

#include "lib/arena.h"
#include "lib/value.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct arena_t arena_t;

typedef struct lattice_element_t lattice_element_t;

/** @brief Abstract domains; enum order does not define lattice ordering. */
typedef enum {
    /** @brief Unknown value of any possible type. */
    LATTICE_TOP = 0,

    /** @brief Any non-null value. */
    LATTICE_NOT_NULL,

    /** @brief Null value. */
    LATTICE_NULL,

    /** @brief Any numeric value, either integer or real. */
    LATTICE_NUMERIC,

    /** @brief Any integer value. */
    LATTICE_INTEGER,

    /** @brief Integer value constrained to a closed interval. */
    LATTICE_INTEGER_RANGE,

    /** @brief Exact integer value. */
    LATTICE_INTEGER_CONSTANT,

    /** @brief Any real value. */
    LATTICE_REAL,

    /** @brief Exact real value. */
    LATTICE_REAL_CONSTANT,

    /** @brief Any string value. */
    LATTICE_STRING,

    /** @brief Exact string value. */
    LATTICE_STRING_CONSTANT,

    /** @brief Any boolean value. */
    LATTICE_BOOLEAN,

    /** @brief Boolean constant true. */
    LATTICE_TRUE,

    /** @brief Boolean constant false. */
    LATTICE_FALSE,

    /** @brief Any function value. */
    LATTICE_FUNCTION,

    /** @brief Any array value. */
    LATTICE_ARRAY,

    /** @brief Array with elements constrained to a known lattice type. */
    LATTICE_TYPED_ARRAY,

    /** @brief Any user-defined object value. */
    LATTICE_USER_DEFINED_OBJECT,

    /** @brief Impossible, contradictory, or unreachable value. */
    LATTICE_BOTTOM,
} lattice_type_t;

/** @brief Base lattice element. */
struct lattice_element_t {
    /** @brief Abstract value kind. */
    lattice_type_t type;
};

/** @brief Integer range lattice element. */
typedef struct {
    /** @brief Base lattice element. */
    lattice_element_t base;

    /** @brief Inclusive lower bound. */
    int64_t min;

    /** @brief Inclusive upper bound. */
    int64_t max;
} integer_range_element_t;

/** @brief Integer constant lattice element. */
typedef struct {
    /** @brief Base lattice element. */
    lattice_element_t base;

    /** @brief Exact integer value. */
    int64_t value;
} integer_constant_element_t;

/** @brief Real constant lattice element. */
typedef struct {
    /** @brief Base lattice element. */
    lattice_element_t base;

    /** @brief Exact real value. */
    double value;
} real_constant_element_t;

/** @brief String constant lattice element. */
typedef struct {
    /** @brief Base lattice element. */
    lattice_element_t base;

    /** @brief Exact string value. */
    string_view_t value;
} string_constant_element_t;

/** @brief Typed array lattice element. */
typedef struct {
    /** @brief Base lattice element. */
    lattice_element_t base;

    /** @brief Lattice type of array elements. */
    lattice_type_t element_type;
} typed_array_element_t;

/**
 * @brief Checks whether a lattice type represents an integer-like value.
 * @return true if the type belongs to the integer domain.
 */
static inline bool is_integer_lattice_type(lattice_type_t type) {
    return type == LATTICE_INTEGER || type == LATTICE_INTEGER_RANGE
           || type == LATTICE_INTEGER_CONSTANT;
}

/**
 * @brief Checks whether a lattice element represents an integer-like value.
 * @return true if the element belongs to the integer domain.
 */
static inline bool is_integer_lattice_element(const lattice_element_t *element) {
    return element && is_integer_lattice_type(element->type);
}

/**
 * @brief Checks whether a lattice type represents a real-like value.
 * @return true if the type belongs to the real domain.
 */
static inline bool is_real_lattice_type(lattice_type_t type) {
    return type == LATTICE_REAL || type == LATTICE_REAL_CONSTANT;
}

/**
 * @brief Checks whether a lattice element represents a real-like value.
 * @return true if the element belongs to the real domain.
 */
static inline bool is_real_lattice_element(const lattice_element_t *element) {
    return element && is_real_lattice_type(element->type);
}

/**
 * @brief Checks whether a lattice type represents a numeric value.
 * @return true if the type belongs to the numeric domain.
 */
static inline bool is_numeric_lattice_type(lattice_type_t type) {
    return type == LATTICE_NUMERIC || is_integer_lattice_type(type) || is_real_lattice_type(type);
}

/**
 * @brief Checks whether a lattice element represents a numeric value.
 * @return true if the element belongs to the numeric domain.
 */
static inline bool is_numeric_lattice_element(const lattice_element_t *element) {
    return element && is_numeric_lattice_type(element->type);
}

/**
 * @brief Checks whether a lattice type represents a string-like value.
 * @return true if the type belongs to the string domain.
 */
static inline bool is_string_lattice_type(lattice_type_t type) {
    return type == LATTICE_STRING || type == LATTICE_STRING_CONSTANT;
}

/**
 * @brief Checks whether a lattice element represents a string-like value.
 * @return true if the element belongs to the string domain.
 */
static inline bool is_string_lattice_element(const lattice_element_t *element) {
    return element && is_string_lattice_type(element->type);
}

/**
 * @brief Checks whether a lattice type represents a boolean-like value.
 * @return true if the type belongs to the boolean domain.
 */
static inline bool is_boolean_lattice_type(lattice_type_t type) {
    return type == LATTICE_BOOLEAN || type == LATTICE_TRUE || type == LATTICE_FALSE;
}

/**
 * @brief Checks whether a lattice element represents a boolean-like value.
 * @return true if the element belongs to the boolean domain.
 */
static inline bool is_boolean_lattice_element(const lattice_element_t *element) {
    return element && is_boolean_lattice_type(element->type);
}

/**
 * @brief Checks whether a lattice type represents an array-like value.
 * @return true if the type belongs to the array domain.
 */
static inline bool is_array_lattice_type(lattice_type_t type) {
    return type == LATTICE_ARRAY || type == LATTICE_TYPED_ARRAY;
}

/**
 * @brief Checks whether a lattice element represents an array-like value.
 * @return true if the element belongs to the array domain.
 */
static inline bool is_array_lattice_element(const lattice_element_t *element) {
    return element && is_array_lattice_type(element->type);
}

/**
 * @brief Checks whether a lattice type represents a user-defined object.
 * @return true if the type belongs to the user-defined object domain.
 */
static inline bool is_user_defined_object_lattice_type(lattice_type_t type) {
    return type == LATTICE_USER_DEFINED_OBJECT;
}

/**
 * @brief Checks whether a lattice element represents a user-defined object.
 * @return true if the element belongs to the user-defined object domain.
 */
static inline bool is_user_defined_object_lattice_element(const lattice_element_t *element) {
    return element && is_user_defined_object_lattice_type(element->type);
}

/**
 * @brief Checks whether a lattice type may participate in `+`.
 * @return true if the type is accepted by abstract addition.
 */
static inline bool is_addable_lattice_type(lattice_type_t type) {
    return type == LATTICE_TOP || type == LATTICE_NOT_NULL || is_numeric_lattice_type(type)
           || is_string_lattice_type(type) || is_array_lattice_type(type)
           || is_user_defined_object_lattice_type(type);
}

/**
 * @brief Checks whether a lattice element may participate in `+`.
 * @return true if the element is not NULL and its type is accepted by abstract addition.
 */
static inline bool is_addable_lattice_element(const lattice_element_t *element) {
    return element && is_addable_lattice_type(element->type);
}

/** @brief Gets the top lattice element singleton. */
const lattice_element_t *make_top_element();

/**
 * @brief Gets the not-null lattice element singleton.
 * @return Constant pointer to the not-null lattice element.
 */
const lattice_element_t *make_not_null_element();

/**
 * @brief Gets the null lattice element singleton.
 * @return Constant pointer to the null lattice element.
 */
const lattice_element_t *make_null_element();

/** @brief Gets the numeric lattice element singleton. */
const lattice_element_t *make_numeric_element();

/** @brief Gets the integer lattice element singleton. */
const lattice_element_t *make_integer_element();

/** @brief Normalizes empty, singleton, and full int64 ranges to BOTTOM, a constant, and INTEGER. */
const lattice_element_t *make_integer_range_element(arena_t *arena, int64_t min, int64_t max);

/** @brief Creates an integer constant lattice element. */
const lattice_element_t *make_integer_constant_element(arena_t *arena, int64_t value);

/** @brief Gets the real lattice element singleton. */
const lattice_element_t *make_real_element();

/** @brief Creates a real constant; lattice equality groups NaNs but distinguishes signed zeros. */
const lattice_element_t *make_real_constant_element(arena_t *arena, double value);

/** @brief Gets the string lattice element singleton. */
const lattice_element_t *make_string_element();

/**
 * @brief Creates a string constant lattice element.
 *
 * The pointed data must outlive the element, because apparently memory ownership still exists to
 * punish optimism.
 */
const lattice_element_t *make_string_constant_element(arena_t *arena, string_view_t value);

/** @brief Gets the boolean lattice element singleton. */
const lattice_element_t *make_boolean_element();

/**
 * @brief Gets the true lattice element singleton.
 * @return Constant pointer to the true lattice element.
 */
const lattice_element_t *make_true_element();

/**
 * @brief Gets the false lattice element singleton.
 * @return Constant pointer to the false lattice element.
 */
const lattice_element_t *make_false_element();

/** @brief Gets the function lattice element singleton. */
const lattice_element_t *make_function_element();

/** @brief Gets the array lattice element singleton. */
const lattice_element_t *make_array_element();

/**
 * @brief Arrays whose elements belong to element_type; empty arrays are always included.
 * TOP normalizes to ARRAY; BOTTOM describes only the empty array.
 * @pre element_type is an unparameterized domain, not a range, constant payload, or TYPED_ARRAY.
 * TRUE and FALSE are supported; nested arrays may use ARRAY without an element constraint.
 */
const lattice_element_t *make_typed_array_element(arena_t *arena, lattice_type_t element_type);

/** @brief Gets the user-defined object lattice element singleton. */
const lattice_element_t *make_user_defined_object_element();

/** @brief Gets the bottom lattice element singleton. */
const lattice_element_t *make_bottom_element();

/** @brief Computes the least upper bound of two lattice elements. */
const lattice_element_t *
lattice_join(arena_t *arena, const lattice_element_t *left, const lattice_element_t *right);

/** @brief Computes the greatest lower bound of two lattice elements. */
const lattice_element_t *
lattice_meet(arena_t *arena, const lattice_element_t *left, const lattice_element_t *right);

/** @brief Converts a lattice element to a human-readable string. */
string_value_t lattice_to_string(const lattice_element_t *element);

/** @brief Possible truth values; zero means no reachable value. */
typedef enum {
    ABSTRACT_NEVER = 0,
    ABSTRACT_FALSE = 1,
    ABSTRACT_TRUE = 2,
    ABSTRACT_EITHER = 3
} abstract_truth_t;

/** @brief Converts an abstract value using Goat's truthiness rules. */
abstract_truth_t lattice_truth(const lattice_element_t *value);
