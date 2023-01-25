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
}
