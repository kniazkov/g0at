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

    /**
     * @return System path separator
     */
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

    /**
     * @brief Normalizes file name, sets path delimiters to match system path delimiters
     * @param file_name The file name
     * @return Normalized file name (must be freed)
     */
    static char * normalize_file_name(const char *file_name) {
        size_t len = std::strlen(file_name);
        char *result = new char[len + 1];
        for (size_t i = 0; i < len; i++) {
            char c = file_name[i];
            result[i] = (c == '\\' || c == '/' ? path_separator() : c);
        }
        result[len] = 0;
        return result;
    }

    std::string load_file_to_string(const char *file_name, bool *success) {
        char *tmp = normalize_file_name(file_name);
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

    void write_string_to_file(const char *file_name, std::string data) {
        char *tmp = normalize_file_name(file_name);
        std::ofstream stream(tmp);
        stream << data;
        stream.close();
        delete[] tmp;
    }
}
