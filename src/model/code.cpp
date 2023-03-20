/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "code.h"
#include "exceptions.h"
#include "lib/utf8_encoder.h"

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

    /* ----------------------------------------------------------------------------------------- */

    void element_visitor::visit(expression_variable *expr) {
    }

    void element_visitor::visit(variable_declaration *stmt) {
    }

    /* ----------------------------------------------------------------------------------------- */

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

    void element::traverse_syntax_tree(element_visitor *visitor) {
        // Do nothing by default
    }

    const char * element::get_node_color() const {
        return "black";
    }

    const char * element::get_background_color() const {
        return nullptr;
    }

    std::string element::generate_graph_description() {
        std::stringstream stream;
        stream << "digraph program {" << std::endl
            << "  node [shape=box style=\"filled,rounded\" penwidth=0.9 nojustify=true fillcolor=white]" 
            << std::endl << "  edge [penwidth=0.7]" 
            << std::endl << std::endl;
        unsigned int counter = 0;
        std::unordered_map<element*, unsigned int> all_indexes;
        generate_node_description(stream, &counter, all_indexes);
        stream << "}" << std::endl;
        return stream.str();
    }

    unsigned int element::generate_node_description(std::stringstream &stream,
            unsigned int *counter, std::unordered_map<element*, unsigned int> &all_indexes) {
        unsigned int index = ++(*counter);
        all_indexes[this] = index;
        stream << "  node_" << index << " [label=<<b>" << get_class_name() << "</b>";
        for (auto item : get_data()) {
            stream << "<br/>" << item.name << ": <font color=\"mediumblue\">"
                << encode_utf8(item.value.to_string_notation()) << "</font>";
        }
        stream << "> color=" << get_node_color();
        const char *bgcolor = get_background_color();
        if (bgcolor) {
            stream << " fillcolor=" << bgcolor;            
        }
        stream << "];" << std::endl;
        auto children = get_children();
        for (unsigned int k = 0; k < children.size(); k++) {
            auto child = children[k];
            unsigned int child_index = 
                child.obj->generate_node_description(stream, counter, all_indexes);
            stream << "  node_" << index << " -> node_" << child_index << " [label=\"  ";
            if (child.name) {
                stream << child.name;
            } else {
                stream << k;
            }
            stream << "\"];" << std::endl;
        }
        generate_additional_edges(stream, index, all_indexes);
        return index;
    }

    void element::generate_additional_edges(std::stringstream &stream,
            unsigned int index, std::unordered_map<element*, unsigned int> &all_indexes) {
        // do nothing by default
    }
}
