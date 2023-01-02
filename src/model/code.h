/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include "object.h"

namespace goat {

    /**
     * @brief A code element representing a syntactic construct
     * 
     * These are not static objects, however, they controlled by the simplified
     * garbage collection system that is based only on reference counting.
     */
    class element : public object {
    public:
        /**
         * @brief Constructor
         */
        element() : refs(1) {
        }
        
        void add_reference() override;
        void release() override;
        bool is_static() override;
        void set_attribute(object *key, variable &value) override;
        object_type get_type() const override;
        std::wstring to_string_notation(const variable* var) const override;

    private:
        /**
         * @brief The counter of objects referring to this object
         */
        unsigned int refs;
    };
}
