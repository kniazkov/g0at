/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "functions.h"
#include "generic_object.h"

namespace goat {
    /**
     * Prototype object for functions
     */
    class prototype : public generic_static_object {
    public:
        /**
         * @brief Constructor
         */
        prototype() {
        }
    };

    static prototype proto_instance;

    object * get_function_prototype() {
        return &proto_instance;
    }

    object_type base_function::get_object_type() const {
        return object_type::generic;
    }

    object * base_function::get_first_prototype() const {
        return &proto_instance;
    }

    /**
     * @todo Notation for functions?
     */
    std::wstring base_function::to_string_notation(const variable* var) const {
        return L"function";
    }

    base_function * base_function::to_function() {
        return this;
    }    
}
