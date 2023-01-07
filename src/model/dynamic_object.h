/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include "object_with_attributes.h"
#include "lib/spinlock.h"

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
     * 
     * All created objects are stored in a doubly linked list. Thus, the garbage collector
     * has access to all objects.
     */
    class gc_data {
    public:
        /**
         * @brief Constructor
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
         * @brief Mutex, allowing multi-threaded modification of the list of objects
         */
        spinlock mutex;

        /**
         * @brief Total number of dynamic objects
         */
        unsigned int count;

        /**
         * @brief First object in the object list
         */
        dynamic_object *first;

        /**
         * @brief Last object in the object list
         */
        dynamic_object *last;
    };

    /**
     * @brief Dynamic objects are created while the program is running
     * 
     * If they are no longer needed, the garbage collector deletes them.
     */
    class dynamic_object : public virtual object_with_attributes {
        friend class gc_data;
    public:
        /**
         * @brief Constructor
         * @param gc_ptr Pointer to data required for the garbage collector
         */
        dynamic_object(gc_data* const gc_ptr);

        /**
         * @brief Destructor
         */
        ~dynamic_object();

        void add_reference() override;
        void release() override;
        gc_data * get_garbage_collector_data() const override;
        void set_attribute(object *key, variable &value) override;

    private:
        /**
         * @brief Data required for the garbage collector
         */
        gc_data* const gc;

        /**
         * @brief Previous object in the object list
         */
        dynamic_object* previous;

        /**
         * @brief Next object in the object list
         */
        dynamic_object* next;

        /**
         * @brief The counter of objects referring to this object
         */
        unsigned int refs;
    };
}
