/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

namespace goat {

    void convert_path_delimiters_to_unix(char *path) {
        char *c = path;
        while(*c) {
            if (*c == '\\') {
                *c = '/';
            }
            c++;
        }
        if (c != path && *(c - 1) == '/') {
            *(c - 1) = 0;
        }
    }
}
