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
 * @brief Gets the top lattice element singleton.
 *
 * @return Constant pointer to the top lattice element.
 */
const lattice_element_t *make_top_element();

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
