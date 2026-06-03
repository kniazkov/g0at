/**
 * @file lattice.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Lattice elements used by abstract interpretation.
 *
 * Abstract interpretation evaluates a program over abstract values instead of
 * concrete runtime values. An abstract value describes a set of possible
 * concrete values: for example, "some integer", "a real constant 3.14", or
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
 * added as the static analyzer grows, because apparently the war against
 * uncertainty starts with an enum and misplaced optimism.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * @enum lattice_type_t
 * @brief Basic abstract value kinds in the analysis lattice.
 *
 * The order of enum values does not define the lattice ordering. It only names
 * the currently supported abstract domains.
 */
typedef enum {
    /**
     * @brief Unknown value of any possible type.
     */
    LATTICE_TOP = 0,

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
    LATTICE_INTEGER_INTERVAL,

    /**
     * @brief Any real value.
     */
    LATTICE_REAL,

    /**
     * @brief Null value.
     */
    LATTICE_NULL,

    /**
     * @brief Boolean value.
     */
    LATTICE_BOOLEAN,

    /**
     * @brief Impossible, contradictory, or unreachable value.
     */
    LATTICE_BOTTOM,
} lattice_type_t;

/**
 * @struct int_interval_t
 * @brief Closed interval of integer values.
 *
 * Represents all integers in the range [min, max]. The interval is expected to
 * be valid, meaning min <= max, unless it is stored inside a bottom element.
 */
typedef struct {
    /**
     * @brief Inclusive lower bound.
     */
    int64_t min;

    /**
     * @brief Inclusive upper bound.
     */
    int64_t max;
} int_interval_t;

/**
 * @struct lattice_element_t
 * @brief Single abstract value in the analysis lattice.
 *
 * Describes a set of possible runtime values. The `type` field selects the
 * abstract domain, while `value` optionally stores a precise constant or a
 * domain-specific payload such as an integer interval.
 *
 * If `is_constant` is true, the corresponding union field contains an exact
 * known value for the selected type. If false, the element describes a broader
 * set of values of that type.
 *
 * If `can_be_null` is true, null is included as an additional possible value.
 * This allows nullable facts such as "integer or null" without immediately
 * widening the element all the way to TOP. Small mercies, aggressively typed.
 */
typedef struct {
    /**
     * @brief Abstract value kind.
     */
    lattice_type_t type;

    /**
     * @brief Type-specific payload or exact constant value.
     */
    union {
        /**
         * @brief Exact integer value when `type` is LATTICE_INTEGER and
         *        `is_constant` is true.
         */
        int64_t int_value;

        /**
         * @brief Integer interval when `type` is LATTICE_INTEGER_INTERVAL.
         */
        int_interval_t int_interval;

        /**
         * @brief Exact real value when `type` is LATTICE_REAL and
         *        `is_constant` is true.
         */
        double real_value;

        /**
         * @brief Exact boolean value when `type` is LATTICE_BOOLEAN and
         *        `is_constant` is true.
         */
        bool bool_value;
    } value;

    /**
     * @brief Whether this element stores an exact constant value.
     */
    bool is_constant;

    /**
     * @brief Whether null is also a possible value for this element.
     */
    bool can_be_null;
} lattice_element_t;

/**
 * @brief Computes the least upper bound of two lattice elements.
 *
 * @param left First lattice element.
 * @param right Second lattice element.
 * @return Least upper bound of `left` and `right`.
 */
lattice_element_t lattice_join(lattice_element_t left, lattice_element_t right);

/**
 * @brief Computes the greatest lower bound of two lattice elements.
 *
 * @param left First lattice element.
 * @param right Second lattice element.
 * @return Greatest lower bound of `left` and `right`.
 */
lattice_element_t lattice_meet(lattice_element_t left, lattice_element_t right);
