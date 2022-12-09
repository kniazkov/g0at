/*
    Copyright 2022 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include <string>
#include <map>
#include <cstdint>

namespace goat {

    class object;

    /**
     * @brief Primitive data processed by the interpreter
     * 
     * Primitive data types, regardless of their size, always occupy one "cell" 
     * (in most cases 8 bytes). This makes it easier to operate the stack. Therefore, all these 
     * types are combined into the union. The interpretation of the data (which type in particular 
     * case) depends on the object processing the data.
     */
    union raw_data {
        int64_t int_value;
        double double_value;
    };

    /**
     * @brief Variable that stores the data
     * 
     * A variable consists of two components: the processing object and the data. If a variable
     * stores a primitive (e.g., an integer), then the object that processes it is static
     * (i.e., not stored in the heap). This allows not to use dynamic memory for primitives,
     * which greatly speeds up the interpreter. If the data are not primitive, then they are stored
     * inside the object, and the memory of such an object is allocated from the heap.
     */
    struct variable {
        object *obj;
        raw_data data;
    };

    /**
     * @brief The basic essence of the Goat object-oriented language
     * 
     * Almost everything in the Goat programming language - data, functions, operators,
     * syntactic constructs, scopes, and so on - are objects. An object can contain other objects,
     * as well as be a prototype for other objects.
     */
    class object {
    private:
        /// Set of child objects
        std::map<object*, variable> children;
    public:       
        /**
         * @brief Returns the string representation of the object,
         *   which is used for printing and debugging purposes
         * @return A string representation of an object 
         */
        virtual std::wstring to_string();

        /**
         * @brief Represents an object in Goat notation, that is, as a string
         *   that can be transformed back into a Goat object
         * @return A string representation of an object in Goat notation
         */
        virtual std::wstring to_string_notation();
    };

    object * get_empty_object();
}
