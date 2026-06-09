/**
 * @file lattice.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Abstract-value lattice used by static analysis.
 *
 * This file declares the abstract values used by the static analyzer to
 * describe sets of possible runtime values without executing the program.
 * These values form a small lattice: each element represents either a broad
 * category such as "any non-null value", a narrower category such as "any
 * integer", or a precise fact such as "the integer constant 42".
 *
 * The lattice is organized around the following main ideas:
 * - @ref LATTICE_TOP represents any possible value;
 * - @ref LATTICE_NOT_NULL represents any value except @ref LATTICE_NULL;
 * - @ref LATTICE_BOTTOM represents an impossible, contradictory, or unreachable
 *   value;
 * - numeric values have additional precision levels for integer ranges,
 *   integer constants, real values, and real constants;
 * - strings, booleans, arrays, functions, and user-defined objects have their
 *   own abstract domains.
 *
 * The two fundamental operations are:
 * - @ref lattice_join, which computes the least upper bound of two abstract
 *   values and is used when control-flow paths merge;
 * - @ref lattice_meet, which computes the greatest lower bound of two abstract
 *   values and is used when independent constraints must both hold.
 *
 * Most general lattice elements are immutable singletons. Elements carrying
 * extra data, such as integer ranges or string constants, are allocated from an
 * arena and returned through the common @ref lattice_element_t base pointer.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lib/arena.h"
#include "lib/value.h"

/**
 * @struct arena_t
 * @brief Forward declaration for memory arena structure.
 */
typedef struct arena_t arena_t;

/**
 * @typedef lattice_element_t
 * @brief Forward declaration for an abstract-interpretation lattice element.
 */
typedef struct lattice_element_t lattice_element_t;

/**
 * @enum lattice_type_t
 * @brief Basic abstract value kinds in the analysis lattice.
 *
 * The order of enum values does not define the lattice ordering. It only names
 * the currently supported abstract domains. Yes, shocking: enum order is not a
 * type theory.
 */
typedef enum {
    /**
     * @brief Unknown value of any possible type.
     */
    LATTICE_TOP = 0,

    /**
     * @brief Any non-null value.
     */
    LATTICE_NOT_NULL,

    /**
     * @brief Null value.
     */
    LATTICE_NULL,

    /**
     * @brief Any numeric value, either integer or real.
     */
    LATTICE_NUMERIC,

    /**
     * @brief Any integer value.
     */
    LATTICE_INTEGER,

    /**
     * @brief Integer value constrained to a closed interval.
     */
    LATTICE_INTEGER_RANGE,

    /**
     * @brief Exact integer value.
     */
    LATTICE_INTEGER_CONSTANT,

    /**
     * @brief Any real value.
     */
    LATTICE_REAL,

    /**
     * @brief Exact real value.
     */
    LATTICE_REAL_CONSTANT,

    /**
     * @brief Any string value.
     */
    LATTICE_STRING,

    /**
     * @brief Exact string value.
     */
    LATTICE_STRING_CONSTANT,

    /**
     * @brief Any boolean value.
     */
    LATTICE_BOOLEAN,

    /**
     * @brief Boolean constant true.
     */
    LATTICE_TRUE,

    /**
     * @brief Boolean constant false.
     */
    LATTICE_FALSE,

    /**
     * @brief Any function value.
     */
    LATTICE_FUNCTION,

    /**
     * @brief Any array value.
     */
    LATTICE_ARRAY,

    /**
     * @brief Array with elements constrained to a known lattice type.
     */
    LATTICE_TYPED_ARRAY,

    /**
     * @brief Any user-defined object value.
     */
    LATTICE_USER_DEFINED_OBJECT,

    /**
     * @brief Impossible, contradictory, or unreachable value.
     */
    LATTICE_BOTTOM,
} lattice_type_t;

/**
 * @struct lattice_element_t
 * @brief Base lattice element.
 *
 * This is the minimal common representation for every abstract value.
 * Type-specific elements may embed it as their first field.
 */
struct lattice_element_t {
    /**
     * @brief Abstract value kind.
     */
    lattice_type_t type;
};

/**
 * @struct integer_range_element_t
 * @brief Integer range lattice element.
 */
typedef struct {
    /**
     * @brief Base lattice element.
     */
    lattice_element_t base;

    /**
     * @brief Inclusive lower bound.
     */
    int64_t min;

    /**
     * @brief Inclusive upper bound.
     */
    int64_t max;
} integer_range_element_t;

/**
 * @struct integer_constant_element_t
 * @brief Integer constant lattice element.
 */
typedef struct {
    /**
     * @brief Base lattice element.
     */
    lattice_element_t base;

    /**
     * @brief Exact integer value.
     */
    int64_t value;
} integer_constant_element_t;

/**
 * @struct real_constant_element_t
 * @brief Real constant lattice element.
 */
typedef struct {
    /**
     * @brief Base lattice element.
     */
    lattice_element_t base;

    /**
     * @brief Exact real value.
     */
    double value;
} real_constant_element_t;

/**
 * @struct string_constant_element_t
 * @brief String constant lattice element.
 */
typedef struct {
    /**
     * @brief Base lattice element.
     */
    lattice_element_t base;

    /**
     * @brief Exact string value.
     */
    string_view_t value;
} string_constant_element_t;

/**
 * @struct typed_array_element_t
 * @brief Typed array lattice element.
 */
typedef struct {
    /**
     * @brief Base lattice element.
     */
    lattice_element_t base;

    /**
     * @brief Lattice type of array elements.
     */
    lattice_type_t element_type;
} typed_array_element_t;

/**
 * @brief Checks whether a lattice type represents an integer-like value.
 *
 * Integer constants and integer ranges are treated as integer values too,
 * because precision is not a separate type universe, как ни странно.
 *
 * @param type Lattice type to check.
 * @return true if the type belongs to the integer domain.
 */
static inline bool is_integer_lattice_type(lattice_type_t type) {
    return type == LATTICE_INTEGER ||
           type == LATTICE_INTEGER_RANGE ||
           type == LATTICE_INTEGER_CONSTANT;
}

/**
 * @brief Checks whether a lattice element represents an integer-like value.
 *
 * @param element Lattice element to check.
 * @return true if the element belongs to the integer domain.
 */
static inline bool is_integer_lattice_element(const lattice_element_t *element) {
    return element && is_integer_lattice_type(element->type);
}

/**
 * @brief Checks whether a lattice type represents a real-like value.
 *
 * @param type Lattice type to check.
 * @return true if the type belongs to the real domain.
 */
static inline bool is_real_lattice_type(lattice_type_t type) {
    return type == LATTICE_REAL ||
           type == LATTICE_REAL_CONSTANT;
}

/**
 * @brief Checks whether a lattice element represents a real-like value.
 *
 * @param element Lattice element to check.
 * @return true if the element belongs to the real domain.
 */
static inline bool is_real_lattice_element(const lattice_element_t *element) {
    return element && is_real_lattice_type(element->type);
}

/**
 * @brief Checks whether a lattice type represents a numeric value.
 *
 * Includes broad numeric values, integers, integer ranges, integer constants,
 * reals, and real constants.
 *
 * @param type Lattice type to check.
 * @return true if the type belongs to the numeric domain.
 */
static inline bool is_numeric_lattice_type(lattice_type_t type) {
    return type == LATTICE_NUMERIC ||
           is_integer_lattice_type(type) ||
           is_real_lattice_type(type);
}

/**
 * @brief Checks whether a lattice element represents a numeric value.
 *
 * @param element Lattice element to check.
 * @return true if the element belongs to the numeric domain.
 */
static inline bool is_numeric_lattice_element(const lattice_element_t *element) {
    return element && is_numeric_lattice_type(element->type);
}

/**
 * @brief Checks whether a lattice type represents a string-like value.
 *
 * @param type Lattice type to check.
 * @return true if the type belongs to the string domain.
 */
static inline bool is_string_lattice_type(lattice_type_t type) {
    return type == LATTICE_STRING ||
           type == LATTICE_STRING_CONSTANT;
}

/**
 * @brief Checks whether a lattice element represents a string-like value.
 *
 * @param element Lattice element to check.
 * @return true if the element belongs to the string domain.
 */
static inline bool is_string_lattice_element(const lattice_element_t *element) {
    return element && is_string_lattice_type(element->type);
}

/**
 * @brief Checks whether a lattice type represents a boolean-like value.
 *
 * @param type Lattice type to check.
 * @return true if the type belongs to the boolean domain.
 */
static inline bool is_boolean_lattice_type(lattice_type_t type) {
    return type == LATTICE_BOOLEAN ||
           type == LATTICE_TRUE ||
           type == LATTICE_FALSE;
}

/**
 * @brief Checks whether a lattice element represents a boolean-like value.
 *
 * @param element Lattice element to check.
 * @return true if the element belongs to the boolean domain.
 */
static inline bool is_boolean_lattice_element(const lattice_element_t *element) {
    return element && is_boolean_lattice_type(element->type);
}

/**
 * @brief Checks whether a lattice type represents an array-like value.
 *
 * @param type Lattice type to check.
 * @return true if the type belongs to the array domain.
 */
static inline bool is_array_lattice_type(lattice_type_t type) {
    return type == LATTICE_ARRAY ||
           type == LATTICE_TYPED_ARRAY;
}

/**
 * @brief Checks whether a lattice element represents an array-like value.
 *
 * @param element Lattice element to check.
 * @return true if the element belongs to the array domain.
 */
static inline bool is_array_lattice_element(const lattice_element_t *element) {
    return element && is_array_lattice_type(element->type);
}

/**
 * @brief Checks whether a lattice type represents a user-defined object.
 *
 * @param type Lattice type to check.
 * @return true if the type belongs to the user-defined object domain.
 */
static inline bool is_user_defined_object_lattice_type(lattice_type_t type) {
    return type == LATTICE_USER_DEFINED_OBJECT;
}

/**
 * @brief Checks whether a lattice element represents a user-defined object.
 *
 * @param element Lattice element to check.
 * @return true if the element belongs to the user-defined object domain.
 */
static inline bool is_user_defined_object_lattice_element(const lattice_element_t *element) {
    return element && is_user_defined_object_lattice_type(element->type);
}

/**
 * @brief Checks whether a lattice type may participate in `+`.
 *
 * In Goat, addition is abstractly meaningful for numeric addition, string
 * concatenation, array concatenation, and user-defined object addition.
 *
 * User-defined objects are considered addable because the left operand may
 * define or dispatch a custom `+` operator. The result of such operation is
 * conservatively unknown.
 *
 * @param type Lattice type to check.
 * @return true if the type is accepted by abstract addition.
 */
static inline bool is_addable_lattice_type(lattice_type_t type) {
    return type == LATTICE_TOP ||
           type == LATTICE_NOT_NULL ||
           is_numeric_lattice_type(type) ||
           is_string_lattice_type(type) ||
           is_array_lattice_type(type) ||
           is_user_defined_object_lattice_type(type);
}

/**
 * @brief Checks whether a lattice element may participate in `+`.
 *
 * In Goat, addition is abstractly meaningful for numeric addition, string
 * concatenation, array concatenation, and user-defined object addition.
 *
 * This is just the element-level wrapper around `is_addable_lattice_type()`,
 * because apparently even one-line helpers need paperwork in C.
 *
 * @param element Lattice element to check.
 * @return true if the element is not NULL and its type is accepted by abstract
 *         addition.
 */
static inline bool is_addable_lattice_element(const lattice_element_t *element) {
    return element && is_addable_lattice_type(element->type);
}

/**
 * @brief Gets the top lattice element singleton.
 *
 * @return Constant pointer to the top lattice element.
 */
const lattice_element_t *make_top_element();

/**
 * @brief Gets the not-null lattice element singleton.
 *
 * @return Constant pointer to the not-null lattice element.
 */
const lattice_element_t *make_not_null_element();

/**
 * @brief Gets the null lattice element singleton.
 *
 * @return Constant pointer to the null lattice element.
 */
const lattice_element_t *make_null_element();

/**
 * @brief Gets the numeric lattice element singleton.
 *
 * @return Constant pointer to the numeric lattice element.
 */
const lattice_element_t *make_numeric_element();

/**
 * @brief Gets the integer lattice element singleton.
 *
 * @return Constant pointer to the integer lattice element.
 */
const lattice_element_t *make_integer_element();

/**
 * @brief Creates an integer range lattice element.
 *
 * @param arena Memory arena for allocating the element.
 * @param min Inclusive lower bound.
 * @param max Inclusive upper bound.
 * @return Constant pointer to the integer range lattice element.
 */
const lattice_element_t *make_integer_range_element(arena_t *arena, int64_t min, int64_t max);

/**
 * @brief Creates an integer constant lattice element.
 *
 * @param arena Memory arena for allocating the element.
 * @param value Exact integer value.
 * @return Constant pointer to the integer constant lattice element.
 */
const lattice_element_t *make_integer_constant_element(arena_t *arena, int64_t value);

/**
 * @brief Gets the real lattice element singleton.
 *
 * @return Constant pointer to the real lattice element.
 */
const lattice_element_t *make_real_element();

/**
 * @brief Creates a real constant lattice element.
 *
 * @param arena Memory arena for allocating the element.
 * @param value Exact real value.
 * @return Constant pointer to the real constant lattice element.
 */
const lattice_element_t *make_real_constant_element(arena_t *arena, double value);

/**
 * @brief Gets the string lattice element singleton.
 *
 * @return Constant pointer to the string lattice element.
 */
const lattice_element_t *make_string_element();

/**
 * @brief Creates a string constant lattice element.
 *
 * The string view is stored as-is. The pointed data must outlive the element,
 * because apparently memory ownership still exists to punish optimism.
 *
 * @param arena Memory arena for allocating the element.
 * @param value Exact string value.
 * @return Constant pointer to the string constant lattice element.
 */
const lattice_element_t *make_string_constant_element(arena_t *arena, string_view_t value);

/**
 * @brief Gets the boolean lattice element singleton.
 *
 * @return Constant pointer to the boolean lattice element.
 */
const lattice_element_t *make_boolean_element();

/**
 * @brief Gets the true lattice element singleton.
 *
 * @return Constant pointer to the true lattice element.
 */
const lattice_element_t *make_true_element();

/**
 * @brief Gets the false lattice element singleton.
 *
 * @return Constant pointer to the false lattice element.
 */
const lattice_element_t *make_false_element();

/**
 * @brief Gets the function lattice element singleton.
 *
 * @return Constant pointer to the function lattice element.
 */
const lattice_element_t *make_function_element();

/**
 * @brief Gets the array lattice element singleton.
 *
 * @return Constant pointer to the array lattice element.
 */
const lattice_element_t *make_array_element();

/**
 * @brief Creates a typed array lattice element.
 *
 * @param arena Memory arena for allocating the element.
 * @param element_type Lattice type of array elements.
 * @return Constant pointer to the typed array lattice element.
 */
const lattice_element_t *make_typed_array_element(arena_t *arena, lattice_type_t element_type);

/**
 * @brief Gets the user-defined object lattice element singleton.
 *
 * @return Constant pointer to the user-defined object lattice element.
 */
const lattice_element_t *make_user_defined_object_element();

/**
 * @brief Gets the bottom lattice element singleton.
 *
 * @return Constant pointer to the bottom lattice element.
 */
const lattice_element_t *make_bottom_element();

/**
 * @brief Computes the least upper bound of two lattice elements.
 *
 * @param arena Memory arena for allocating result elements.
 * @param left First lattice element.
 * @param right Second lattice element.
 * @return Constant pointer to the least upper bound of `left` and `right`.
 */
const lattice_element_t *lattice_join(arena_t *arena,
        const lattice_element_t *left, const lattice_element_t *right);

/**
 * @brief Computes the greatest lower bound of two lattice elements.
 *
 * @param arena Memory arena for allocating result elements.
 * @param left First lattice element.
 * @param right Second lattice element.
 * @return Constant pointer to the greatest lower bound of `left` and `right`.
 */
const lattice_element_t *lattice_meet(arena_t *arena,
        const lattice_element_t *left, const lattice_element_t *right);

/**
 * @brief Converts a lattice element to a human-readable string.
 *
 * The result is intended for graph/debug output.
 *
 * @param element Lattice element to convert.
 * @return String representation of the lattice element.
 */
string_value_t lattice_to_string(const lattice_element_t *element);
