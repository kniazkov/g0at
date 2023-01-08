/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <string>
#include "generic_object.h"
#include "exceptions.h"

namespace goat {

    goat_exception_data::goat_exception_data(object *obj) {
        this->obj = obj;
        obj->add_reference();
        refs = 1;
        buff = nullptr;
    }

    goat_exception_data::~goat_exception_data() {
        obj->release();
        delete[] buff;
    }

    goat_exception_wrapper::goat_exception_wrapper(object *obj) {
        data = new goat_exception_data(obj);
    }

    goat_exception_wrapper::goat_exception_wrapper(const goat_exception_wrapper &other) {
        data = other.data;
        data->refs++;
    }

    goat_exception_wrapper& goat_exception_wrapper::operator=(
            const goat_exception_wrapper &other) {
        if (data != other.data) {
            if (!(data->refs--)) {
                delete data;
            }
            data = other.data;
            data->refs++;
        }
        return *this;
    }

    goat_exception_wrapper::~goat_exception_wrapper() {
        if (!(--data->refs)) {
            delete data;
        }
    }

    const char * goat_exception_wrapper::what() const noexcept {
        std::wstring info = data->obj->to_string(nullptr);
        size_t size = info.size();
        data->buff = new char[size + 1];
        for (size_t i = 0; i < size; i++) {
            wchar_t ch = info[i];
            data->buff[i] = ch < 127 ? ch : '?';
        }
        data->buff[size] = '\0';
        return data->buff;
    }

    /* ----------------------------------------------------------------------------------------- */

    /**
     * @brief Prototype object for exceptions
     */
    class exception_prototype : public generic_static_object {
    public:
        /**
         * @brief Constructor
         */
        exception_prototype() {
        }
    };

    static exception_prototype exception_proto_instance;
    object * get_exception_prototype() {
        return &exception_proto_instance;
    }

    /**
     * @brief Illegal argument exception
     */
    class illegal_argument_exception : public generic_static_object {
    public:
        object * get_first_prototype() const override {
            return &exception_proto_instance;
        }

        std::wstring to_string(const variable* var) const override {
            return L"Illegal argument";
        }
    };

    static illegal_argument_exception illegal_argument_instance;
    object * get_illegal_agrument_exception() {
        return &illegal_argument_instance;
    }
}
