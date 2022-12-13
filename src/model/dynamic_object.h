/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include "object.h"

namespace goat {
    
    class dynamic_object;

    /**
     * @brief Data required for the garbage collector
     * 
     * The interpreter uses two garbage collection systems at once: one based on
     * reference counting and the other on tracing. Using reference counting allows us
     * to reduce the lifetime of an object - short-lived objects will be deleted as soon
     * as the execution thread exits the scope. However, this method does not guarantee
     * destruction in all cases (cyclic references will not be processed). 
     */
    class gc_data {
    public:
        /**
         * Constructor
         */
        gc_data();

        /**
         * @brief Adds the object to the list, so that it starts to be considered
         *   by the garbage collector
         * @param obj The object
         */
        void add_object(dynamic_object* const obj);

        /**
         * @brief Removes the object from the list of objects
         * @param obj The object
         */
        void remove_object(dynamic_object* const obj);

        /**
         * @return Total number of dynamic objects
         */
        unsigned int get_count() {
            return count;
        }

    private:
        /**
         * @brief Total number of dynamic objects
         */
        unsigned int count;
    };

    /**
     * @brief Dynamic objects are created while the program is running
     * 
     * If they are no longer needed, the garbage collector deletes them.
     */
    class dynamic_object : public object {
    public:
        /**
         * @brief Constructor
         * @param gc_ptr Pointer to data required for the garbage collector
         */
        dynamic_object(gc_data* const gc_ptr);

        /**
         * Destructor
         */
        ~dynamic_object();

        void add_reference() override;
        void release() override;
        bool is_static() override;
        void set_child(object *key, variable &value) override;

    private:
        /**
         * Data required for the garbage collector
         */
        gc_data* const gc;

        /**
         * @brief The counter of objects referring to this object
         */
        unsigned int refs;
    };
}
