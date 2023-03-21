/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <cstring>
#include <sstream>
#include "generic_object.h"
#include "exceptions.h"
#include "lib/format_string.h"
#include "lib/utf8_encoder.h"
#include "resources/messages.h"

namespace goat {

    runtime_exception_data::runtime_exception_data(object *obj) {
        this->obj = obj;
        obj->add_reference();
        refs = 1;
        buff = nullptr;
    }

    runtime_exception_data::~runtime_exception_data() {
        obj->release();
        delete[] buff;
    }

    runtime_exception::runtime_exception(object *obj) {
        data = new runtime_exception_data(obj);
    }

    runtime_exception::runtime_exception(const runtime_exception &other) {
        data = other.data;
        data->refs++;
    }

    runtime_exception& runtime_exception::operator=(
            const runtime_exception &other) {
        if (data != other.data) {
            if (!(data->refs--)) {
                delete data;
            }
            data = other.data;
            data->refs++;
        }
        return *this;
    }

    runtime_exception::~runtime_exception() {
        if (!(--data->refs)) {
            delete data;
        }
    }

    const char * runtime_exception::what() const noexcept {
        if (!data->buff) {
            std::string message = encode_utf8(data->obj->to_string(nullptr));
            size_t size = message.size();
            data->buff = new char[size + 1];
            std::memcpy(data->buff, message.c_str(), (size + 1) * sizeof(char));
        }
        return data->buff;
    }

    void runtime_exception::add_stack_trace_data(stack_trace_data &item) {
        data->stack_trace.push_back(item);
    }

    std::string runtime_exception::get_report() const {
        std::stringstream stream;
        stream << encode_utf8(data->obj->to_string(nullptr)) << std::endl;
        for (stack_trace_data &item : data->stack_trace) {
            stream << "  > " << item.file_name << ", " << item.line << std::endl;
        }
        return stream.str();
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
            return get_messages()->msg_illegal_argument();
        }
    };

    static illegal_argument_exception illegal_argument_instance;
    object * get_illegal_agrument_exception() {
        return &illegal_argument_instance;
    }

    /**
     * @brief Operation not supported exception
     */
    class operation_not_supported_exception : public generic_static_object {
    public:
        object * get_first_prototype() const override {
            return &exception_proto_instance;
        }

        std::wstring to_string(const variable* var) const override {
            return get_messages()->msg_operation_not_supported();
        }
    };

    static operation_not_supported_exception operation_not_supported_instance;
    object * get_operation_not_supported_exception() {
        return &operation_not_supported_instance;
    }

    /**
     * @brief Reference error exception
     */
    class reference_error_exception : public generic_static_object {
    public:
        object * get_first_prototype() const override {
            return &exception_proto_instance;
        }

        std::wstring to_string(const variable* var) const override {
            return get_messages()->msg_reference_error();
        }
    };

    static reference_error_exception reference_error_instance;
    object * get_reference_error_exception() {
        return &reference_error_instance;
    }

    /**
     * @brief Reference error (clarified) exception
     */
    class reference_error_clarified_exception : public generic_dynamic_object {
    public:
        /**
         * @brief Constructor
         * @param gc Data required for the garbage collector
         * @param name A name that is not defined
         */
        reference_error_clarified_exception(gc_data * const gc, std::wstring name) :
            generic_dynamic_object(gc, &reference_error_instance), name(name) {
        }

        std::wstring to_string(const variable* var) const override {
            return format_string(get_messages()->msg_reference_error_clarified(), name);
        }
    
    private:
        /**
         * @brief A name that is not defined
         */
        std::wstring name;
    };

    object * create_reference_error_clarified_exception(gc_data * const gc, std::wstring name) {
        return new reference_error_clarified_exception(gc, name);
    }
}
