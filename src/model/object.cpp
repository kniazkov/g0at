/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <cassert>
#include "object.h"
#include "root_object.h"
#include "numbers.h"
#include "exceptions.h"

namespace goat {

    void variable::set_integer_value(int64_t value) {
        obj = get_integer_handler();
        data.int_value = value;
    }

    void variable::set_real_value(double value) {
        obj = get_real_handler();
        data.double_value = value;
    }

    /* ----------------------------------------------------------------------------------------- */
    
    object::object() {
    }

    object::~object() {
    }

    bool object::less(const object* const other) const {
        /*
           In general, an object is less than another if its address in memory is less:
        */
        return this < other;
    }

    object * object::get_prototype(unsigned int index) const {
        return index ? nullptr : get_first_prototype();
    }

    object * object::get_first_prototype() const {
        /*
            By default, the prototype for any object is the root object:
        */
        return get_root_object();
    }

    unsigned int object::get_number_of_prototypes() const {
        /*
            By default, the object has one prototype:
        */
        return 1;
    }

    bool object::is_instance_of(const object *proto) const {
        if (this == proto) {
            return true;
        }
        return get_first_prototype()->is_instance_of(proto);
    }

    std::wstring object::to_string(const variable* var) const {
        return to_string_notation(var);
    }

    variable * object::get_attribute(object *key) {
        /*
            If the object does not have its own attributes,
            need to look for them in the prototype:
        */
        return get_first_prototype()->get_attribute(key);
    }

    variable * object::get_own_attribute(object *key) {
        return nullptr;
    }

    bool object::get_string_value(std::wstring* const value_ptr) const {
        return false;
    }

    bool object::get_integer_value(const variable* var, int64_t* const value_ptr) const {
        return false;
    }

    bool object::get_real_value(const variable* var, double* const value_ptr) const {
        return false;
    }

    base_function * object::to_function() {
        return nullptr;
    }

    variable object::do_addition(gc_data* const gc,
            const variable* left, const variable* right) const {
        throw runtime_exception(get_operation_not_supported_exception());        
    }

    variable object::do_subtraction(gc_data* const gc,
            const variable* left, const variable* right) const {
        throw runtime_exception(get_operation_not_supported_exception());        
    }

    variable object::do_multiplication(gc_data* const gc,
            const variable* left, const variable* right) const {
        throw runtime_exception(get_operation_not_supported_exception());
    }
}
