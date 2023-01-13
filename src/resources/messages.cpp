/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <ctype.h>
#include <cstring>
#include "messages.h"

namespace goat {

    const std::wstring en_illegal_argument(L"Illegal argument");
    const std::wstring en_unknown_symbol(L"Unknown symbol");
    
    /**
     * @brief Messages in English
     */
    static struct messages_en : public messages {
        const std::wstring & msg_illegal_argument() const override {
            return en_illegal_argument;
        }
        const std::wstring & msg_unknown_symbol() const override {
            return en_unknown_symbol;
        }
    } messages_en_instance;


    const std::wstring ru_illegal_argument(L"Недопустимый аргумент");
    const std::wstring ru_unknown_symbol(L"Неизвестный символ");
    
    /**
     * @brief Messages in Russian
     */
    static struct messages_ru : public messages {
        const std::wstring & msg_illegal_argument() const override {
            return ru_illegal_argument;
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
        if (len == 2) {
            if (std::tolower(lang[0]) == 'e' && std::tolower(lang[0]) == 'n') {
                instance = &messages_en_instance;
            }
            else if (std::tolower(lang[0]) == 'r' && std::tolower(lang[0]) == 'u') {
                instance = &messages_ru_instance;
            }
        }
    }
}
