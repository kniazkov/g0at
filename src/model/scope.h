/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include "generic_object.h"

namespace goat {

    /**
     * @brief Scope is the data that is accessible from a particular point in the code
     *   being executed.
     * 
     * All data operations (declaring variables, changing variable values, reading variables)
     * take place in the scope. As with everything in the Goat programming language,
     * the scope itself is an object.
     */
    class scope : public virtual object {
    };

    /**
     * @return Pointer to the root scope
     */
    scope * get_root_scope();

    /**
     * @brief Creates the main scope from which code execution begins
     * @param gc Data required for the garbage collector
     * @return Pointer to the main scope (must be released manually!)
     */
    scope * create_main_scope(gc_data *gc);
}
