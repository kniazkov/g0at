/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include <vector>
#include "static_object.h"
#include "dynamic_object.h"

namespace goat {

    class function_declaration;
    class scope;

    /**
     * @brief A function is an object that, in addition to storing data, can perform some action
     */
    class base_function : public virtual object {
    public:
        /**
         * @brief Execute function, i.e., perform some action
         * @param scope The scope in which the function is called
         * @param args Arguments of the function
         * @param ret_val [out] Return value
         */
        virtual void exec(scope *scope, std::vector<variable> &args, variable* ret_val) = 0;

        object_type get_object_type() const override;
        object *get_first_prototype() const override;
        std::wstring to_string_notation(const variable* var) const override;
        base_function *to_function() override;
    };

    /**
     * @brief A static function, i.e. one that is built into the interpreter
     */
    class static_function : public static_object, public base_function {
    };

    /**
     * @brief A function defined by the programmer
     */
    class user_defined_function : public dynamic_object, public base_function {
    public:
        /**
         * @brief Constructor
         * @param gc Pointer to data required for the garbage collector
         * @param declaration The declaration of the function
         */
        user_defined_function(gc_data* const gc, const function_declaration *declaration);

        void exec(scope *scope, std::vector<variable> &args, variable* ret_val) override;

    private:
        /**
         * @brief The declaration of the function (contains arguments and body)
         */
        const function_declaration *declaration;
    };

    /**
     * @return Pointer to the prototype of functions
     */
    object * get_function_prototype();
}
