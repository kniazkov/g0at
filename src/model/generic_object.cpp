/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <cassert>
#include <sstream>
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

    object_with_multiple_prototypes::object_with_multiple_prototypes(gc_data *gc,
            std::vector<object*> &proto) : dynamic_object(gc), proto(proto)  {
        assert(proto.size() > 1);
        for (object *obj : proto) {
            obj->add_reference();
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
        if (this == possible_proto) {
            return true;
        }
        for (object *obj : proto) {
            if (obj->is_instance_of(possible_proto)) {
                return true;
            }
        }
        return false;
    }
}
