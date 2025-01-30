/**
 * @file messages.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implements message handling with language selection.
 * 
 * This file provides functionality for managing localized error messages in different languages.
 * The majority of the messages are related to errors encountered during the parsing of source code 
 * and compilation to bytecode. It supports selecting between English and Russian languages, 
 * and retrieving messages in the selected language.
 */

#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#include "messages.h"

/**
 * @var english
 * @brief The message structure for English language.
 * 
 * This structure contains the English language version of the error messages, which 
 * are used during the parsing and compilation process.
 */
static messages_t english = {
    .no_input_file = L"input file not specified",
    .unknown_option = L"unknown option: '%a'",
    .unknown_symbol = L"unknown symbol '%c'",
    .unclosed_quotation_mark = L"unmatched quote found in string literal; expected closing quote",
    .invalid_escape_sequence = L"invalid escape sequence '\\%c' in string literal",
    .unclosed_opening_bracket = L"unclosed opening bracket: expected a closing bracket to match '%c'",
    .missing_opening_bracket = L"missing opening bracket corresponding to '%c'",
    .brackets_do_not_match = L"closing bracket '%c' does not match the opening bracket '%c'",
    .not_a_statement = L"token '%s' is not a statement allowed here",
};

/**
 * @var russian
 * @brief The message structure for Russian language.
 * 
 * This structure contains the Russian language version of the error messages, which 
 * are used during the parsing and compilation process.
 */
static messages_t russian = {
    .no_input_file = L"не указан входной файл",
    .unknown_option = L"неизвестный параметр командной строки: '%a'",
    .unknown_symbol = L"неизвестный символ '%c'",
    .unclosed_quotation_mark = L"в строковом литерале пропущена закрывающая кавычка",
    .invalid_escape_sequence = L"неправильная управляющая последовательность '\\%c' в строковом литерале",
    .unclosed_opening_bracket = L"нет закрывающей скобки, соответствующей '%c'",
    .missing_opening_bracket = L"нет открывающей скобки, соответствующей '%c'",
    .brackets_do_not_match = L"закрывающая скобка '%c' не соответствует открывающей '%c'",
    .not_a_statement = L"токен '%s' здесь недопустим, ожидается инструкция языка",
};

/**
 * @var messages
 * @brief Pointer to the current message structure.
 * 
 * This pointer is used to access the appropriate set of localized error messages 
 * based on the selected language. It can point to either the `english` or `russian` 
 * message structure, depending on the current language setting.
 */
static messages_t *messages = &english;

const messages_t *get_messages() {
    return messages;
}

#define MAX_LANG_NAME_LEN 16

void set_language(const char *lang) {
    char buff[MAX_LANG_NAME_LEN];
    int i;
    for (i = 0; i < MAX_LANG_NAME_LEN - 1; i++) {
        buff[i] == tolower(lang[i]);
    }
    buff[i] = '\0';
    
    if (strcmp("ru", buff) == 0) {
        messages = &russian;
    } else {
        messages = &english;
    }
}

void init_messages() {
    const char *lang = getenv("GOAT_LANGUAGE");
    if (lang != NULL) {
        set_language(lang);
    }
}
