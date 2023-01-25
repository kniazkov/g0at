/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <string>
#include <cstring>
#include <fstream>
#include <streambuf>

namespace goat {

    inline char path_separator() {
    #ifdef _WIN32
        return '\\';
    #else
        return '/';
    #endif
    }

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

    std::string load_file_to_string(const char *file_name, bool *success) {
        size_t len = std::strlen(file_name);
        char *tmp = new char[len + 1];
        for (size_t i = 0; i < len; i++) {
            char c = file_name[i];
            tmp[i] = (c == '\\' || c == '/' ? path_separator() : c);
        }
        tmp[len] = 0;
        std::ifstream stream(tmp);
        std::string buff = "";
        if (stream.fail()) {
            if (success) {
                *success = false;
            }
        }
        else {
            stream.seekg(0, std::ios::end);   
            buff.reserve(stream.tellg());
            stream.seekg(0, std::ios::beg);
            buff.assign((std::istreambuf_iterator<char>(stream)),
                        std::istreambuf_iterator<char>());
            if (success) {
                *success = true;
            }
        }
        delete[] tmp;
        return buff;
    }
}
