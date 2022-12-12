/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include "object.h"

namespace goat {

    /**
     * @brief A static object always exists in the system, so it is not counted
     *   by the garbage collector
     */
    class static_object : public object {
    public:
        void add_ref() override;
        void release() override;
    };
}
