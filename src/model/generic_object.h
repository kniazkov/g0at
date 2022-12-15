/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include <vector>
#include "object_with_attributes.h"
#include "static_object.h"
#include "dynamic_object.h"

namespace goat {

    /**
     * @brief An "ordinary" object that does not have any special properties
     *   (such as built-in data types)
     */
    class generic_object : public virtual object_with_attributes {
    public:
        object_type get_type() const override;
        std::wstring to_string_notation() const override;
    };

    /**
     * @brief A predefined object permanently present in the system
     */
    class generic_static_object : public static_object, public generic_object {
    };

    /**
     * @brief An object whose structure is set by the programmer in the program code
     */
    class generic_dynamic_object : public dynamic_object, public generic_object {
    public:
        /**
         * Constructor
         * @param gc Data required for the garbage collector
         * @param proto The prototype of the object
         */
        generic_dynamic_object(gc_data *gc, object *proto);

        /**
         * Destructor
         */
        ~generic_dynamic_object();

        object* get_first_prototype() const override;

    private:
        /**
         * @brief The prototype of the object
         */
        object *proto;
    };

    /**
     * @brief A dynamic object (i.e. an object whose structure is set by the programmer
     *   in the program code) that has two or more prototypes
     */
    class object_with_multiple_prototypes : public dynamic_object, public generic_object {
    public:
        /**
         * Constructor
         * @param gc Data required for the garbage collector
         * @param proto The list of prototypes of the object
         */
        object_with_multiple_prototypes(gc_data *gc, std::vector<object*> &proto);

        /**
         * Destructor
         */
        ~object_with_multiple_prototypes();

        object* get_prototype(unsigned int index) const override;
        object* get_first_prototype() const override;
        unsigned int get_number_of_prototypes() const override;
        bool is_instance_of(const object* proto) const override;

    private:
        /**
         * @brief The list of prototypes of the object
         */
        std::vector<object*> proto;
    };
}
