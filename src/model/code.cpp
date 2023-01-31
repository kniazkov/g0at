/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "code.h"
#include "exceptions.h"

namespace goat {

    /**
     * @brief The number of elements created during source code compilation.
     *   Used for debugging purposes to look for memory leaks during unit testing
     */
    unsigned int elements_counter = 0;

    unsigned int get_number_of_elements() {
        return elements_counter;
    }

    void reset_number_of_elements() {
        elements_counter = 0;
    }

    element::element() : refs(1) {
        elements_counter++;
    }

    element::~element() {
        elements_counter--;
    }

    void element::add_reference() {
        refs++;
    }

    void element::release() {
        if (!(--refs)) {
            delete this;
        }
    }

    bool element::is_static() const {
        return false;
    }

    gc_data * element::get_garbage_collector_data() const {
        return nullptr;
    }

    object_type element::get_type() const {
        return object_type::code;
    }

    /**
     * @todo: Notation for elements
     */
    std::wstring element::to_string_notation(const variable* var) const {
        return L"{}";
    }
    
    void element::set_attribute(object *key, variable &value) {
        throw runtime_exception(get_operation_not_supported_exception());
    }

    const char * element::get_node_color() const {
        return "black";
    }

    std::string element::generate_graph_description() {
        std::stringstream stream;
        stream << "digraph program {" << std::endl;
        unsigned int counter = 0;
        generate_node_description(stream, &counter);
        stream << "}" << std::endl;
        return stream.str();
    }

    unsigned int element::generate_node_description(std::stringstream &stream,
            unsigned int *counter) {
        unsigned int index = ++(*counter);
        stream << "  node_" << index << " [label=\"" << get_class_name() << "\" color=\""
            << get_node_color() << "\"];" << std::endl;
        auto children = get_children();
        for (unsigned int k = 0; k < children.size(); k++) {
            auto child = children[k];
            unsigned int child_index = child.obj->generate_node_description(stream, counter);
            stream << "  node_" << index << " -> node_" << child_index << " [label=\"  ";
            if (child.name) {
                stream << child.name;
            } else {
                stream << k;
            }
            stream << "\"];" << std::endl;
        }
        return index;
    }
}
