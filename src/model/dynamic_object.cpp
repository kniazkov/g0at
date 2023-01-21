/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <mutex>
#include "dynamic_object.h"

namespace goat {

    gc_data::gc_data() {
        count = 0;
        first = nullptr;
        last = nullptr;
    }

    void gc_data::add_object(dynamic_object* const obj) {
        const std::lock_guard<spinlock> lock(mutex);

        obj->previous = last;
        obj->next = nullptr;

        if (last) {
            last->next = obj;
        } else {
            first = obj;
        }
        last = obj;
        count++;
    }

    void gc_data::remove_object_unsafe(dynamic_object* const obj) {
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
    }

    void gc_data::remove_object(dynamic_object* const obj) {
        const std::lock_guard<spinlock> lock(mutex);
        remove_object_unsafe(obj);
    }

    void gc_data::sweep() {
        /*
            It is assumed that in addition some other mechanism (not a spinlock)
            will be used to lock the threads during cleaning. But this is not certain
        */
        const std::lock_guard<spinlock> lock(mutex);

        dynamic_object *obj = first;
        while (obj) {
            dynamic_object *next = obj->next;
            if (obj->flags.is_marked()) {
                obj->flags.reset_marked_flag();
            }
            else {
                remove_object_unsafe(obj);
                delete obj;
            }
            obj = next;
        }
    }

    /* ----------------------------------------------------------------------------------------- */

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

    bool dynamic_object::is_static() const {
        return false;
    }

    gc_data * dynamic_object::get_garbage_collector_data() const {
        return gc;
    }

    void dynamic_object::set_attribute(object *key, variable &value) {
        write_attribute(key, value);
    }
}
