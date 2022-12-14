/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

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
        for (auto pair = children.begin(); pair != children.end(); pair++) {
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

    generic_dynamic_object::generic_dynamic_object(gc_data *gc, prototype_list *pl) :
            dynamic_object(gc) {
        if (pl->count == 1) {
            (proto.data.obj = pl->data[0])->add_reference();
        } else if (pl->count > 1) {
            proto.count = pl->count;
            proto.data.list = new object*[pl->count];
            for (unsigned int i = 0; i < pl->count; i++) {
                proto.data.list[i] = pl->data[i];
                proto.data.list[i]->add_reference();
            }
        }
    }

    generic_dynamic_object::~generic_dynamic_object() {
        if (proto.count > 1) {
            for (unsigned int i = 0; i < proto.count; i++) {
                proto.data.list[i]->release();
            }
            delete proto.data.list;
        } else {
            proto.data.obj->release();
        }
    }
}
