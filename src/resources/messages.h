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
        virtual const std::wstring & msg_illegal_argument() const = 0;
        virtual const std::wstring & msg_unknown_symbol() const = 0;
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
