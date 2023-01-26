/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include <cmath>

namespace goat {

    /**
     * @brief A unary functor, that is, a functor that performs some computation
     *   with a single argument
     * @tparam R Return type
     * @tparam A Type of argument
     */
    template <typename R, typename A> struct unary_functor {
        typedef R ret_type;
        typedef A arg_type;
    };

    /**
     * @brief Functor that performs the "unary plus" operation
     * @tparam R Return type
     * @tparam A Type of argument
     */
    template <typename R, typename A> struct fn_pos : public unary_functor <R, A> {
        static R calculate(const A &a) {
            return a;
        }
    };

    /**
     * @brief Functor that performs the "unary minus" operation
     * @tparam R Return type
     * @tparam A Type of argument
     */
    template <typename R, typename A> struct fn_neg : public unary_functor <R, A> {
        static R calculate(const A &a) {
            return -a;
        }
    };

    /**
     * @brief Functor that performs the "logical not" operation
     * @tparam R Return type
     * @tparam A Type of argument
     */
    template <typename R, typename A> struct fn_log_not : public unary_functor <R, A> {
        static R calculate(const A &a) {
            return !a;
        }
    };

    /**
     * @brief Functor that performs inversion
     * @tparam R Return type
     * @tparam A Type of argument
     */
    template <typename R, typename A> struct fn_inv : public unary_functor <R, A> {
        static R calculate(const A &a) {
            return ~a;
        }
    };

    /**
     * @brief Functor that performs increment
     * @tparam R Return type
     * @tparam A Type of argument
     */
    template <typename R, typename A> struct fn_inc : public unary_functor <R, A> {
        static R calculate(const A &a) {
            return a + 1;
        }
    };

    /**
     * @brief Functor that performs decrement
     * @tparam R Return type
     * @tparam A Type of argument
     */
    template <typename R, typename A> struct fn_dec : public unary_functor <R, A> {
        static R calculate(const A &a) {
            return a - 1;
        }
    };

    /**
     * @brief A binary functor, that is, a functor that performs some computation
     *   with two arguments
     * @tparam R Return type
     * @tparam X Type of the first (left) argument
     * @tparam Y Type of the second (right) argument
     */
    template <typename R, typename X, typename Y> struct binary_functor {
        typedef R ret_type;
        typedef X left_type;
        typedef Y right_type;
    };

    /**
     * @brief Functor that performs operation "+"
     * @tparam R Return type
     * @tparam X Type of the first (left) argument
     * @tparam Y Type of the second (right) argument
     */
    template <typename R, typename X, typename Y> struct fn_plus : public binary_functor <R, X, Y> {
        static R calculate(const X &x, const Y &y) {
            return x + y;
        }
    };

    /**
     * @brief Functor that performs operation "-"
     * @tparam R Return type
     * @tparam X Type of the first (left) argument
     * @tparam Y Type of the second (right) argument
     */
    template <typename R, typename X, typename Y> struct fn_minus : public binary_functor <R, X, Y> {
        static R calculate(const X &x, const Y &y) {
            return x - y;
        }
    };

    /**
     * @brief Functor that performs operation "*"
     * @tparam R Return type
     * @tparam X Type of the first (left) argument
     * @tparam Y Type of the second (right) argument
     */
    template <typename R, typename X, typename Y> struct fn_mul : public binary_functor <R, X, Y> {
        static R calculate(const X &x, const Y &y) {
            return x * y;
        }
    };

    /**
     * @brief Functor that performs operation "/"
     * @tparam R Return type
     * @tparam X Type of the first (left) argument
     * @tparam Y Type of the second (right) argument
     */
    template <typename R, typename X, typename Y> struct fn_div : public binary_functor <R, X, Y> {
        static R calculate(const X &x, const Y &y) {
            return x / y;
        }
    };

    /**
     * @brief Functor that performs operation "%" ("modulus", "remainder")
     * @tparam R Return type
     * @tparam X Type of the first (left) argument
     * @tparam Y Type of the second (right) argument
     */
    template <typename R, typename X, typename Y> struct fn_mod : public binary_functor <R, X, Y> {
        static R calculate(const X &x, const Y &y) {
            return x % y;
        }
    };

    /**
     * @brief Functor that performs the "exponentiation" operation
     * @tparam R Return type
     * @tparam X Type of the first (left) argument
     * @tparam Y Type of the second (right) argument
     */
    template <typename R, typename X, typename Y> struct fn_exp : public binary_functor <R, X, Y> {
        static R calculate(const X &x, const Y &y) {
            return (R)pow(x, y);
        }
    };

    /**
     * @brief Functor that performs the "equals" operation
     * @tparam R Return type
     * @tparam X Type of the first (left) argument
     * @tparam Y Type of the second (right) argument
     */
    template <typename R, typename X, typename Y> struct fn_equals : public binary_functor <R, X, Y> {
        static R calculate(const X &x, const Y &y) {
            return x == y;
        }
    };

    /**
     * @brief Functor that performs the "not equal" operation
     * @tparam R Return type
     * @tparam X Type of the first (left) argument
     * @tparam Y Type of the second (right) argument
     */
    template <typename R, typename X, typename Y> struct fn_not_equal : public binary_functor <R, X, Y> {
        static R calculate(const X &x, const Y &y) {
            return x != y;
        }
    };

    /**
     * @brief Functor that performs operation "<" ("less")
     * @tparam R Return type
     * @tparam X Type of the first (left) argument
     * @tparam Y Type of the second (right) argument
     */
    template <typename R, typename X, typename Y> struct fn_less : public binary_functor <R, X, Y> {
        static R calculate(const X &x, const Y &y) {
            return x < y;
        }
    };

    /**
     * @brief Functor that performs operation "<=" ("less oe equal")
     * @tparam R Return type
     * @tparam X Type of the first (left) argument
     * @tparam Y Type of the second (right) argument
     */
    template <typename R, typename X, typename Y> struct fn_less_or_equal : public binary_functor <R, X, Y> {
        static R calculate(const X &x, const Y &y) {
            return x <= y;
        }
    };

    /**
     * @brief Functor that performs operation ">" ("greater")
     * @tparam R Return type
     * @tparam X Type of the first (left) argument
     * @tparam Y Type of the second (right) argument
     */
    template <typename R, typename X, typename Y> struct fn_greater : public binary_functor <R, X, Y> {
        static R calculate(const X &x, const Y &y){
            return x > y;
        }
    };

    /**
     * @brief Functor that performs operation ">=" ("greater or equal")
     * @tparam R Return type
     * @tparam X Type of the first (left) argument
     * @tparam Y Type of the second (right) argument
     */
    template <typename R, typename X, typename Y> struct fn_greater_or_equal 
            : public binary_functor <R, X, Y> {
        static R calculate(const X &x, const Y &y) {
            return x >= y;
        }
    };

    /**
     * @brief Functor that performs the "bitwise and" operation
     * @tparam R Return type
     * @tparam X Type of the first (left) argument
     * @tparam Y Type of the second (right) argument
     */
    template <typename R, typename X, typename Y> struct fn_bitwise_and : public binary_functor <R, X, Y> {
        static R calculate(const X &x, const Y &y) {
            return x & y;
        }
    };

    /**
     * @brief Functor that performs the "bitwise or" operation
     * @tparam R Return type
     * @tparam X Type of the first (left) argument
     * @tparam Y Type of the second (right) argument
     */
    template <typename R, typename X, typename Y> struct fn_bitwise_or : public binary_functor <R, X, Y> {
        static R calculate(const X &x, const Y &y) {
            return x | y;
        }
    };

    /**
     * @brief Functor that performs the "exclusive or" operation
     * @tparam R Return type
     * @tparam X Type of the first (left) argument
     * @tparam Y Type of the second (right) argument
     */
    template <typename R, typename X, typename Y> struct fn_bitwise_xor : public binary_functor <R, X, Y> {
        static R calculate(const X &x, const Y &y) {
            return x ^ y;
        }
    };
}
