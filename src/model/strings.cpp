/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <cassert>
#include <sstream>
#include "strings.h"
#include "generic_object.h"

namespace goat {
    /**
     * Prototype object for objects that store a string
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

    object * get_string_prototype() {
        return &proto_instance;
    }

    base_string::base_string(const std::wstring &value) {
        this->value = value;
    }

    object_type base_string::get_object_type() const {
        return object_type::string;
    }

    bool base_string::less(const object* const other) const {
        std::wstring other_value;
        bool other_is_a_string = other->get_string_value(&other_value);
        assert(other_is_a_string);
        return value < other_value;
    }

    object * base_string::get_first_prototype() const {
        return &proto_instance;
    }

    std::wstring base_string::to_string(const variable* var) const {
        return value;
    }

    /**
     * @todo Escape entities
     */
    std::wstring base_string::to_string_notation(const variable* var) const {
        std::wstringstream stream;
        stream << L'"' << value << L'"';
        return stream.str();
    }

    bool base_string::get_string_value(std::wstring* const value_ptr) const {
        *value_ptr = value;
        return true;
    }

    variable base_string::do_addition(gc_data* const gc,
            const variable* left, const variable* right) const {
        std::wstring right_value = right->to_string();
        variable var;
        var.obj = new dynamic_string(gc, value + right_value);
        return var;
    }    
}
