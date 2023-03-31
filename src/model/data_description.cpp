/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "data_description.h"

namespace goat {

    data_descriptor::data_descriptor(bool modifiable, base_string *name, base_string *proto_name,
            expression *init_value) {
        this->modifiable = modifiable;
        this->name = name;
        name->add_reference();
        this->proto_name = proto_name;
        if (proto_name) {
            proto_name->add_reference();
        }
        this->init_value = init_value;
        if (init_value) {
            init_value->add_reference();
        }
        this->type = get_unknown_data_type();
    }

    data_descriptor::~data_descriptor() {
        name->release();
        if (proto_name) {
            proto_name->release();
        }
        if (init_value) {
            init_value->release();
        }
    }

    void data_descriptor::initialize_data_in_the_scope(scope *scope) {
        if (init_value) {
            variable value = init_value->calc(scope);
            scope->set_attribute(name, value);
            value.release();
        }
        else {
            scope->set_attribute(name, get_null_object());
        }
    }

    void data_descriptor::traverse_syntax_tree(element_visitor *visitor) {
        visitor->visit(this);
        if (init_value) {
            init_value->traverse_syntax_tree(visitor);
        }
    }

    const char * data_descriptor::get_class_name() const {
        return "data";
    }

    std::vector<child_descriptor> data_descriptor::get_children() const {
        std::vector<child_descriptor> list;
        if (init_value) {
            list.push_back({"init value", init_value});
        }
        return list;
    }

    std::vector<element_data_descriptor> data_descriptor::get_data() const {
        std::vector<element_data_descriptor> list;
        variable var;
        var.obj = name;
        list.push_back({"name", var});
        return list;
    }
}
