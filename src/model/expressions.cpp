/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "expressions.h"

namespace goat {

    read_variable::read_variable(base_string *name) {
        this->name = name;
        name->add_reference();
    }

    read_variable::~read_variable() {
        name->release();
    }

    /**
     * @todo Exception if variable not found
     */
    variable read_variable::calc(scope *scope) {
        return *(scope->get_attribute(name));
    }
}
