/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <cassert>
#include <sstream>
#include <stack>
#include <set>
#include "generic_object.h"

namespace goat {

    object_type generic_object::get_type() const {
        return object_type::generic;
    }

    std::wstring generic_object::to_string_notation() const {
        std::wstringstream stream;
        stream << L'{';
        bool flag = false;
        for (auto pair = attributes.begin(); pair != attributes.end(); pair++) {
            if (flag) {
                stream << ", ";
            }
            flag = true;
            stream << pair->first->to_string_notation() << L": "
                << pair->second.obj->to_string_notation();
        }
        stream << L'}';
        return stream.str();
    }

    generic_dynamic_object::generic_dynamic_object(gc_data *gc, object *proto)
            : dynamic_object(gc), proto(proto) {
        proto->add_reference();
    }

    generic_dynamic_object::~generic_dynamic_object() {
        proto->release();
    }

    object * generic_dynamic_object::get_first_prototype() const {
        return proto;
    }

    /**
     * @brief Data used for topological sorting of the object graph
     * 
     * Topological sorting is used to determine the traversal order of the prototype tree
     * in the case of multiple inheritance.
     */
    struct topological_sorting_data {
        /**
         * @brief Stack containing sorted objects
         * 
         * An object from which nothing is inherited will be put into this stack last.
         */
        std::stack<object*> stack;

        /**
         * @brief Set of already processed objects, needed to avoid putting
         *   already processed objects to the stack
         */
        std::set<object*> processed_objects;
    };

    /**
     * @brief Topological sorting algorithm
     * @param obj Root object
     * @param data Data used for topological sorting
     */
    static void topological_sorting(object *obj, topological_sorting_data *data) {
        if (data->processed_objects.find(obj) != data->processed_objects.end()) {
            return;
        }

        unsigned int count = obj->get_number_of_prototypes();
        for (unsigned int i = count; i > 0; i--) {
            topological_sorting(obj->get_prototype(i - 1), data);
        }

        data->stack.push(obj);
        data->processed_objects.insert(obj);
    }

    object_with_multiple_prototypes::object_with_multiple_prototypes(gc_data *gc,
            std::vector<object*> &proto) : dynamic_object(gc), proto(proto)  {
        assert(proto.size() > 1);
        topological_sorting_data data;
        for (size_t i = proto.size(); i > 0; i--) {
            object *obj = proto[i - 1];
            obj->add_reference();
            topological_sorting(obj, &data);
        }
        size_t stack_size = data.stack.size();
        topology.reserve(stack_size + 1);
        topology.push_back(this);
        for (size_t i = 0; i < stack_size; i++) {
            topology.push_back(data.stack.top());
            data.stack.pop();
        }
    }

    object_with_multiple_prototypes::~object_with_multiple_prototypes() {
        for (object *obj : proto) {
            obj->release();
        }
    }

    object * object_with_multiple_prototypes::get_prototype(unsigned int index) const {
        return index < proto.size() ? proto[index] : nullptr;
    }

    object * object_with_multiple_prototypes::get_first_prototype() const {
        return proto[0];
    }

    unsigned int object_with_multiple_prototypes::get_number_of_prototypes() const {
        return proto.size();
    }

    bool object_with_multiple_prototypes::is_instance_of(const object* possible_proto) const {
        for (object *obj : topology) {
            if (obj == possible_proto) {
                return true;
            }
        }
        return false;
    }

    variable * object_with_multiple_prototypes::get_attribute(object *key) {
        for (object *obj : topology) {
            variable *result = obj->get_own_attribute(key);
            if (result != nullptr) {
                return result;
            }
        }
        return nullptr;
    }
}
