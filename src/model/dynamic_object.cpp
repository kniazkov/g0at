/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "dynamic_object.h"

namespace goat {

    gc_data::gc_data() {
        count = 0;
        first = nullptr;
        last = nullptr;
    }

    void gc_data::add_object(dynamic_object* const obj) {
        mutex.lock();

        obj->previous = last;
        obj->next = nullptr;

        if (last) {
            last->next = obj;
        } else {
            first = obj;
        }
        last = obj;
        count++;

        mutex.unlock();
    }

    void gc_data::remove_object(dynamic_object* const obj) {
        mutex.lock();

        if (obj->previous) {
            obj->previous->next = obj->next;
        } else {
            first = obj->next;
        }

        if (obj->next) {
            obj->next->previous = obj->previous;
        } else {
            last = obj->previous;
        }

        count--;

        mutex.unlock();
    }

    dynamic_object::dynamic_object(gc_data* const gc_ptr) : gc(gc_ptr), refs(1) {
        gc->add_object(this);
    }

    dynamic_object::~dynamic_object() {
        for (auto pair = attributes.begin(); pair != attributes.end(); pair++) {
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

    gc_data * dynamic_object::get_garbage_collector_data() const {
        return gc;
    }

    void dynamic_object::set_attribute(object *key, variable &value) {
        write_attribute(key, value);
    }
}
