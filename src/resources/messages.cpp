/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <ctype.h>
#include <cstring>
#include "messages.h"

namespace goat {

    const std::wstring en_brackets_do_not_match(L"The closing bracket '{0}' does not match the opening bracket '{1}'");
    const std::wstring en_closing_bracket_without_opening(L"The closing bracket '{0}' without opening one");
    const std::wstring en_file_not_found(L"File not found");
    const std::wstring en_illegal_argument(L"Illegal argument");
    const std::wstring en_interpreter_description(L"The Goat programming language interpreter, v. 0");
    const std::wstring en_not_closed_bracket(L"The bracket '{0}' was not closed");
    const std::wstring en_operation_not_supported(L"Operation not supported");
    const std::wstring en_reference_error(L"Reference error");
    const std::wstring en_unable_to_parse_token_sequence(L"Unable to parse the token sequence");
    const std::wstring en_unknown_symbol(L"Unknown symbol");
    
    /**
     * @brief Messages in English
     */
    static struct messages_en : public messages {
        const std::wstring & msg_brackets_do_not_match() const override {
            return en_brackets_do_not_match;
        }
        const std::wstring & msg_closing_bracket_without_opening() const override {
            return en_closing_bracket_without_opening;
        }
        const std::wstring & msg_file_not_found() const override {
            return en_file_not_found;
        }
        const std::wstring & msg_illegal_argument() const override {
            return en_illegal_argument;
        }
        const std::wstring & msg_interpreter_description() const override {
            return en_interpreter_description;
        }
        const std::wstring & msg_not_closed_bracket() const override {
            return en_not_closed_bracket;
        };
        const std::wstring & msg_operation_not_supported() const override {
            return en_operation_not_supported;
        };
        const std::wstring & msg_reference_error() const override {
            return en_reference_error;
        };
        const std::wstring & msg_unable_to_parse_token_sequence() const override {
            return en_unable_to_parse_token_sequence;
        }
        const std::wstring & msg_unknown_symbol() const override {
            return en_unknown_symbol;
        }
    } messages_en_instance;


    const std::wstring ru_brackets_do_not_match(L"Закрывающая скобка '{0}' не соответствует открывающей '{1}'");
    const std::wstring ru_closing_bracket_without_opening(L"Закрывающая скобка '{0}' не имеет парной открывающей скобки");
    const std::wstring ru_file_not_found(L"Файл не найден");
    const std::wstring ru_illegal_argument(L"Недопустимый аргумент");
    const std::wstring ru_interpreter_description(L"Интерпретатор языка программирования Goat, v. 0");
    const std::wstring ru_not_closed_bracket(L"Открывающая скобка '{0}' не имеет парной закрывающей скобки");
    const std::wstring ru_operation_not_supported(L"Операция не поддерживается");
    const std::wstring ru_reference_error(L"Ошибка при обращении по ссылке");
    const std::wstring ru_unable_to_parse_token_sequence(L"Нераспознаваемая последовательность токенов");
    const std::wstring ru_unknown_symbol(L"Неизвестный символ");
    
    /**
     * @brief Messages in Russian
     */
    static struct messages_ru : public messages {
        const std::wstring & msg_brackets_do_not_match() const override {
            return ru_brackets_do_not_match;
        }
        const std::wstring & msg_closing_bracket_without_opening() const override {
            return ru_closing_bracket_without_opening;
        }
        const std::wstring & msg_file_not_found() const override {
            return ru_file_not_found;
        }
        const std::wstring & msg_illegal_argument() const override {
            return ru_illegal_argument;
        }
        const std::wstring & msg_interpreter_description() const override {
            return ru_interpreter_description;
        }
        const std::wstring & msg_not_closed_bracket() const override {
            return ru_not_closed_bracket;
        };
        const std::wstring & msg_operation_not_supported() const override {
            return ru_operation_not_supported;
        };
        const std::wstring & msg_reference_error() const override {
            return ru_reference_error;
        };
        const std::wstring & msg_unable_to_parse_token_sequence() const override {
            return ru_unable_to_parse_token_sequence;
        }
        const std::wstring & msg_unknown_symbol() const override {
            return ru_unknown_symbol;
        }
    } messages_ru_instance;

    static messages * instance = &messages_en_instance;

    messages * get_messages() {
        return instance;
    }

    void set_message_language(const char *lang) {
        size_t len = std::strlen(lang);
        char *name = new char[len + 1];
        for (size_t i = 0; i < len; i++) {
            name[i] = std::tolower(lang[i]);
        }
        name[len] = 0;
        if (0 == std::strcmp(name, "en") || 0 == std::strcmp(name, "english")) {
            instance = &messages_en_instance;
        }
        else if (0 == std::strcmp(name, "ru") 
                || 0 == std::strcmp(name, "rus") || 0 == std::strcmp(name, "russian")) {
            instance = &messages_ru_instance;
        }
        delete name;
    }
}
