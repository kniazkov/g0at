/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <sstream>
#include <cstring>
#include "exceptions.h"
#include "lib/utf8_encoder.h"

namespace goat {

    compiler_exception_data::compiler_exception_data(position *pos, std::wstring message) {
        this->pos = *pos;
        this->message = message;
        this->refs = 0;
        this->buff = nullptr;
    }

    compiler_exception_data::~compiler_exception_data() {
        delete[] buff;
    }

    compiler_exception::compiler_exception(compiler_exception_data *data) {
        this->data = data;
        data->refs++;
    }

    compiler_exception::compiler_exception(const compiler_exception &other) {
        data = other.data;
        data->refs++;
    }

    compiler_exception& compiler_exception::operator=(const compiler_exception &other) {
        if (data != other.data) {
            if (!(data->refs--)) {
                delete data;
            }
            data = other.data;
            data->refs++;
        }
        return *this;
    }

    compiler_exception::~compiler_exception() {
        if (!(--data->refs)) {
            delete data;
        }
    }

    const char* compiler_exception::what() const noexcept {
        if (!data->buff) {
            std::wstringstream stream;
            stream << data->pos.file_name << ", " << data->pos.line << '.' << data->pos.column << 
                ": " << data->message;
            std::string message = encode_utf8(stream.str());
            size_t size = message.size();
            data->buff = new char[size + 1];
            std::memcpy(data->buff, message.c_str(), (size + 1) * sizeof(char));
        }
        return data->buff;
    }
}
