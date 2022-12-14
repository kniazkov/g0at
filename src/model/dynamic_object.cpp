/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "dynamic_object.h"

namespace goat {

    gc_data::gc_data() {
        count = 0;
    }

    void gc_data::add_object(dynamic_object* const obj) {
        count++;
    }

    void gc_data::remove_object(dynamic_object* const obj) {
        count--;
    }

    dynamic_object::dynamic_object(gc_data* const gc_ptr) : gc(gc_ptr), refs(1) {
        gc->add_object(this);
    }

    dynamic_object::~dynamic_object() {
        for (auto pair = children.begin(); pair != children.end(); pair++) {
            pair->first->release();
            pair->second.release();
        } 
    }

    void dynamic_object::add_reference() {
        refs++;
    }

    void dynamic_object::release() {
        if (!(--refs)) {
            gc->remove_object(this);
            delete this;
        }
    }

    bool dynamic_object::is_static() {
        return false;
    }

    void dynamic_object::set_child(object *key, variable &value) {
        set_child_unsafe(key, value);
    }
}
