/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <cassert>
#include "data_description.h"

namespace goat {

    /**
     * @brief Simple descriptor that consists of only one identifier, for example:
     *   'var i:Integer;' - here 'Integer' is a simple descriptor
     */
    class simple_prototype_descriptor : public prototype_descriptor {
    public:
        /**
         * @brief Constructor.
         * @param identifier Identifier representing the descriptor
         */
        simple_prototype_descriptor(std::wstring identifier) 
                : identifier(identifier), obj(nullptr) {
        }

        /**
         * Destructor.
         */
        ~simple_prototype_descriptor() {
            if (obj) {
                obj->release();
            }
        }

        object * to_object(gc_data* const gc) override {
            if (!obj) {
                obj = new dynamic_string(gc, identifier);
            } 
            return obj;
        }

    private:
        /**
         * @brief Identifier representing the descriptor
         */
        std::wstring identifier;

        /**
         * @brief Cached representation of a descriptor as a Goat object
         */
        object *obj;
    };

    /* ----------------------------------------------------------------------------------------- */

    data_descriptor::data_descriptor(bool modifiable, base_string *name, expression *init_value) {
        this->modifiable = modifiable;
        this->name = name;
        this->extends_from = nullptr;
        this->init_value = init_value;
        this->type = get_unknown_data_type();

        name->add_reference();
        if (init_value) {
            init_value->add_reference();
        }
    }

    data_descriptor::data_descriptor(bool modifiable, base_string *name, expression *init_value,
            std::vector<std::wstring> proto_list) {
        this->modifiable = modifiable;
        this->name = name;
        if (proto_list.empty()) {
            this->extends_from = nullptr;            
        } else if (proto_list.size() == 1) {
            this->extends_from = new simple_prototype_descriptor(proto_list[0]);
        } else {
            assert(false); // will be implemented
        }
        this->init_value = init_value;
        this->type = get_unknown_data_type();

        name->add_reference();
        if (init_value) {
            init_value->add_reference();
        }
    }

    data_descriptor::~data_descriptor() {
        name->release();
        if (extends_from) {
            delete extends_from;
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
        if (extends_from) {
            var.obj = extends_from->to_object(name->get_garbage_collector_data());
            list.push_back({"extends", var});        
        }
        return list;
    }
}
