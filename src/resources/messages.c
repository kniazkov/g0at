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
    .memory_leak = L"Memory leak of %zu bytes detected",
    .no_input_file = L"Input file not specified",
    .unknown_option = L"Unknown option: '%a'",
    .missing_specification = L"Missing value for parameter '%a'",
    .bad_graph_file = L"The graph image file must be of type PNG or SVG",
    .no_graphviz = L"The GraphViz tool required for graph visualization is not installed on the system",
    .graphviz_failed = L"The GraphViz tool failed to generate a graph image",
    .duplicate_parameter = L"Duplicate parameter '%a' found",
    .cannot_read_source_file = L"Could not read the source code file at '%a'",
    .compilation_error = L"Compilation error in '%a', %zu.%zu: %s",
    .unknown_symbol = L"Unknown symbol '%c'",
    .unclosed_quotation_mark = L"Unmatched quote found in string literal; expected closing quote",
    .invalid_escape_sequence = L"Invalid escape sequence '\\%c' in string literal",
    .unclosed_opening_bracket = L"Unclosed opening bracket: expected a closing bracket to match '%c'",
    .missing_opening_bracket = L"Missing opening bracket corresponding to '%c'",
    .brackets_do_not_match = L"Closing bracket '%c' does not match the opening bracket '%c'",
    .not_a_statement = L"Token '%s' is not a statement allowed here",
    .expected_expression = L"Unexpected token '%s', expression expected at this position",
    .expected_lvalue = L"Left-hand side expression expected, got '%s' instead",
    .expected_comma_between_args = L"Expected comma between function arguments",
    .expected_expr_after_comma = L"Expected expression after comma in argument list",
    .expected_var_declaration = L"Variable declaration expected after 'var' keyword",
    .expected_const_declaration = L"Constant declaration expected after 'const' keyword",
    .expected_var_after_comma = L"Expected another variable declaration after comma",
    .expected_const_after_comma = L"Expected another constant declaration after comma",
    .invalid_var_declaration_syntax = L"Expected either variable name or name with value assignment, but got '%s'",
    .invalid_const_declaration_syntax = L"Expected name with value assignment, but got '%s'",
    .invalid_function_argument = L"Function argument must be an identifier, but got '%s'",
};

/**
 * @var russian
 * @brief The message structure for Russian language.
 * 
 * This structure contains the Russian language version of the error messages, which 
 * are used during the parsing and compilation process.
 */
static messages_t russian = {
    .memory_leak = L"Обнаружена утечка памяти в %zu байт",
    .no_input_file = L"Не указан входной файл",
    .unknown_option = L"Неизвестный параметр командной строки: '%a'",
    .missing_specification = L"Отсутствует значение для параметра '%a'",
    .bad_graph_file = L"Файл, в который сохраняется изображение графа, должен быть типа PNG или SVG",
    .no_graphviz = L"Утилита GraphViz, необходимая для генерации изображения графа, не установлена в системе",
    .graphviz_failed = L"Утилита GraphViz не смогла сгенерировать изображение графа",
    .duplicate_parameter = L"Параметр '%a' повторяется",
    .cannot_read_source_file = L"Не удалось прочесть исходный файл '%a'",
    .compilation_error = L"Ошибка компиляции в файле '%a', %zu.%zu: %s",
    .unknown_symbol = L"Неизвестный символ '%c'",
    .unclosed_quotation_mark = L"В строковом литерале пропущена закрывающая кавычка",
    .invalid_escape_sequence = L"Неправильная управляющая последовательность '\\%c' в строковом литерале",
    .unclosed_opening_bracket = L"Нет закрывающей скобки, соответствующей '%c'",
    .missing_opening_bracket = L"Нет открывающей скобки, соответствующей '%c'",
    .brackets_do_not_match = L"Закрывающая скобка '%c' не соответствует открывающей '%c'",
    .not_a_statement = L"Токен '%s' здесь недопустим, ожидается инструкция языка",
    .expected_expression = L"Токен '%s' здесь недопустим, в этом месте ожидается выражение",
    .expected_lvalue = L"Ожидается левостороннее выражение, но получено '%s'",
    .expected_comma_between_args = L"Ожидается запятая между аргументами функции",
    .expected_expr_after_comma = L"Ожидается выражение после запятой в списке аргументов",
    .expected_var_declaration = L"После ключевого слова 'var' ожидается определение переменной",
    .expected_const_declaration = L"После ключевого слова 'const' ожидается определение константы",
    .expected_var_after_comma = L"После запятой ожидается определение следующей переменной",
    .expected_const_after_comma = L"После запятой ожидается определение следующей константы",
    .invalid_var_declaration_syntax = L"Ожидается либо имя переменной, либо имя с присваиванием значения, но получено '%s'",
    .invalid_const_declaration_syntax = L"Ожидается имя с присваиванием значения, но получено '%s'",
    .invalid_function_argument = L"Аргумент функции должен быть идентификатором, но получено '%s'",
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
    for (i = 0; i < MAX_LANG_NAME_LEN - 1 && lang[i] != '\0'; i++) {
        buff[i] = tolower(lang[i]);
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
