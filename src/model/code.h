/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include <sstream>
#include "object.h"

namespace goat {

    class element;

    /**
     * @brief Descriptor of child elements
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
     * @brief Descriptor of additional element data
     */
    struct element_data_descriptor {
        /**
         * @brief The name of additional data
         */
        const char *name;

        /**
         * @brief The value
         */
        variable value;
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

        void add_reference() override;
        void release() override;
        bool is_static() const override;
        gc_data * get_garbage_collector_data() const override;
        void set_attribute(object *key, variable &value) override;
        object_type get_type() const override;
        std::wstring to_string_notation(const variable* var) const override;

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

        /**
         * @brief Forms a list of children to traverse the syntax tree
         * @return A list containing descriptors of additional data
         */
        virtual std::vector<element_data_descriptor> get_data() const = 0;

        /**
         * @brief Returns the color of the node when rendering the syntax tree
         * @return The node color (bleck by default)
         */
        virtual const char * get_node_color() const;
        
        /**
         * @brief Generates a graph (syntax tree) description in DOT format
         * @return Graph description in DOT format
         * 
         * This allows the syntax tree to be visualized by different utilities (e.g. Graphviz).
         */
        std::string generate_graph_description();

        /**
         * @brief Generates a desription in DOT format for this element
         * @param stream The stream in which to write the result
         * @param counter The node counter
         * @return The node index
         */
        virtual unsigned int generate_node_description(std::stringstream &stream,
            unsigned int *counter);

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
