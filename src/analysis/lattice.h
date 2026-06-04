/**
 * @file lattice.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Lattice elements used by abstract interpretation.
 *
 * Abstract interpretation evaluates a program over abstract values instead of
 * concrete runtime values. An abstract value describes a set of possible
 * concrete values: for example, "some integer", "a real constant 3.14", "some string", or
 * "an integer in the interval [0, 10]". This allows the analyzer to reason
 * about code without executing it.
 *
 * The abstract values form a lattice. A lattice is a partially ordered set with
 * two important operations:
 * - join: combines information from multiple control-flow paths;
 * - meet: intersects information when constraints are applied.
 *
 * In this file, the top element represents maximum uncertainty, meaning any
 * value may be possible. The bottom element represents an impossible or
 * unreachable value. Elements between them represent progressively more precise
 * facts about a value.
 *
 * The lattice is intentionally small for now. New abstract value kinds can be
 * added as the static analyzer grows.
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
typedef struct {
    /**
     * @brief Abstract value kind.
     */
    lattice_type_t type;
} lattice_element_t;

/**
 * @struct integer_range_lattice_element_t
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
} integer_range_lattice_element_t;

/**
 * @struct integer_constant_lattice_element_t
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
} integer_constant_lattice_element_t;

/**
 * @struct real_constant_lattice_element_t
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
} real_constant_lattice_element_t;

/**
 * @struct string_constant_lattice_element_t
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
} string_constant_lattice_element_t;

/**
 * @struct typed_array_lattice_element_t
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
} typed_array_lattice_element_t;

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
