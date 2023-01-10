/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include <cstdint>

namespace goat {
    
    /**
     * @brief A set of flags (Boolean values) that an object can have
     */
    class object_flags {

    public:
        /**
         * @brief Constructor
         * 
         * All flags are reset by default
         */
        object_flags() : x(0) {            
        }

        /**
         * @brief Returns a flag as to whether the object has been marked as alive
         *   by the garbage collector
         * @return Flag value
         */
        inline bool is_marked() {
            return read_bit(0);
        }

        /**
         * @brief Marks the object as alive
         */
        inline void mark() {
            write_bit(0, true);
        }

        /**
         * @brief Resets the value of the "marked" flag
         */
        inline void reset_marked_flag() {
            write_bit(0, false);
        }

    private:
        /**
         * @brief A 32-bit number will allow us to have 32 flags
         */
        uint32_t x;

        /**
         * @brief Reads the value of an individual bit
         * @param index Bit index
         * @returns Bit value 
         */
        inline bool read_bit(int index) {
            uint32_t m = 1 << index;
            return x & m > 0;
        }

        /**
         * @brief Writes the value of an individual bit
         * @param index Bit index
         * @param value Bit value 
         */
        inline void write_bit(int index, bool value) {
            uint32_t m = 1 << index;
            if (value) {
                x = x | m;
            } else {
                x = x & ~m;
            }
        }
    };
}
