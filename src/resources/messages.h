/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include <string>

namespace goat {

    /**
     * @brief Сlass containing messages that are shown to the user when certain situations
     *   (mainly exceptions) occur
     */
    struct messages {
        virtual const std::wstring & msg_brackets_do_not_match() const = 0;
        virtual const std::wstring & msg_closing_bracket_without_opening() const = 0;
        virtual const std::wstring & msg_expected_property_name() const = 0;
        virtual const std::wstring & msg_expected_type_name() const = 0;
        virtual const std::wstring & msg_file_not_found() const = 0;
        virtual const std::wstring & msg_function_arguments_already_defined() const = 0;
        virtual const std::wstring & msg_function_body_already_defined() const = 0;
        virtual const std::wstring & msg_function_body_must_be_after_arguments() const = 0;
        virtual const std::wstring & msg_illegal_argument() const = 0;
        virtual const std::wstring & msg_interpreter_description() const = 0;
        virtual const std::wstring & msg_multiple_declarations_are_not_allowed() const = 0;
        virtual const std::wstring & msg_not_closed_bracket() const = 0;
        virtual const std::wstring & msg_operation_not_supported() const = 0;
        virtual const std::wstring & msg_reference_error() const = 0;
        virtual const std::wstring & msg_reference_error_clarified() const = 0;
        virtual const std::wstring & msg_unable_to_parse_token_sequence() const = 0;
        virtual const std::wstring & msg_unhandled_exception() const = 0;
        virtual const std::wstring & msg_unknown_symbol() const = 0;
        virtual const std::wstring & msg_variable_name_is_expected() const = 0;
    };

    /**
     * @brief Returns a set of messages in the language currently set (English by default)
     */
    messages * get_messages();

    /**
     * @brief Sets the current language for messages
     * @param lang Language name, for example, 'ru'
     */
    void set_message_language(const char *lang);
}
