/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

namespace goat {

    /**
     * @brief Formats the path so that it is UNIX style, i.e. path delimiters are forward slashes
     * @param path The path
     */
    void convert_path_delimiters_to_unix(char *path);

    /**
     * @brief Loads file and returns its contents as a string
     * @param file_name  The name of the file
     * @param success Optional, flag is set to <code>true</code> if the operation was successful
     */
    std::string load_file_to_string(const char *file_name, bool *success);
}
