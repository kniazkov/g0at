/** @file windows_command_line.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Windows CRT argument quoting, including quotes and trailing backslashes.
 */
#include "windows_command_line.h"

#include "allocate.h"

#include <string.h>

char *create_windows_command_line(const char *const *arguments) {
    size_t size = 1;
    for (size_t i = 0; arguments[i]; i++) {
        size_t length = strlen(arguments[i]);
        if (length > 16380 || size + length * 2 + 3 > 32767)
            return NULL;
        size += length * 2 + 3;
    }
    char *text = ALLOC(size), *out = text;
    for (size_t i = 0; arguments[i]; i++) {
        if (i)
            *out++ = ' ';
        *out++ = '"';
        const char *in = arguments[i];
        while (*in) {
            size_t slashes = 0;
            while (*in == '\\') {
                slashes++;
                in++;
            }
            size_t count = (*in == '"' || !*in) ? slashes * 2 : slashes;
            while (count--)
                *out++ = '\\';
            if (*in == '"')
                *out++ = '\\';
            if (*in)
                *out++ = *in++;
        }
        *out++ = '"';
    }
    *out = '\0';
    return text;
}
