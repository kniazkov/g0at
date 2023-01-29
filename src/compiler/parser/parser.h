/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include <vector>
#include <unordered_set>
#include <unordered_map>
#include "token_iterator.h"

namespace goat {

    class object;
    class gc_data;
    class statement;
    class program;

    /**
     * @brief Data needed for parsing
     */
    struct parser_data {
        /**
         * @brief Data required for the garbage collector (needed to create dynamic objects)
         */
        gc_data *gc;

        /**
         * @brief Set of objects that are created during parsing (needed to mark dynamic objects)
         */
        std::unordered_set<object*> *objects;

        /**
         * @brief Copies of the names of the source files from which the program is compiled
         */
        std::vector<const char*> *file_names_list;

        /**
         * @brief Map that keeps a match between the file names retrieved from the scanner
         *   and the copies of the file names that are stored in the syntax tree after parsing
         */
        std::unordered_map<const char*, const char*> file_names_map;

        /**
         * @brief Copies the file name
         * @param name The file name
         * @return A copy of the file name
         */
        const char * copy_file_name(const char *name);
    };

    /**
     * @brief Parses the whole program
     * @param gc Data required for the garbage collector
     * @param iter Iterator by token
     * @return A parsed program ready for execution
     */
    program * parse_program(gc_data *gc, token_iterator *iter);

    /**
     * @brief Tries to parse the list of tokens as a statement
     * @param data Data needed for parsing
     * @param iter Iterator by token
     * @return A statement or <code>nullptr</code> if there are no more tokens
     */
    statement * parse_statement(parser_data *data, token_iterator *iter);
}
