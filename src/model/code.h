/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include "object.h"

namespace goat {

    class element;

    /**
     * @brief Descriptor of child elements of objects
     * 
     * An element may contain other (child) elements. A set of such elements is a syntax tree.
     */
    struct child_descriptor {
        /**
         * @brief The name of the child
         */
        const char *name;

        /**
         * @brief The child element
         */
        element *obj;
    };

    /**
     * @brief A code element representing a syntactic construct
     * 
     * These are not static objects, however, they controlled by the simplified
     * garbage collection system that is based only on reference counting.
     */
    class element : public object {
    public:
        /**
         * @brief Constructor
         */
        element();

        /**
         * @brief Destructor
         */
        ~element();

        /**
         * @brief Returns the name of the element type.
         *   Using this name, the element can be constructed with the factory
         * @return The name of the type of the element
         */
        virtual const char * get_class_name() const = 0;

        /**
         * @brief Forms a list of children to traverse the syntax tree
         * @return A list of children
         */
        virtual std::vector<child_descriptor> get_children() const = 0;
        
        void add_reference() override;
        void release() override;
        bool is_static() const override;
        gc_data * get_garbage_collector_data() const override;
        void set_attribute(object *key, variable &value) override;
        object_type get_type() const override;
        std::wstring to_string_notation(const variable* var) const override;

    private:
        /**
         * @brief The counter of objects referring to this object
         */
        unsigned int refs;
    };

    /**
     * @brief Returns the number of elements created during source code compilation.
     *   Used for debugging purposes to look for memory leaks during unit testing
     * @return Number of elements
     */
    unsigned int get_number_of_elements();

    /**
     * @brief Clears the element counter
     */
    void reset_number_of_elements();
}
