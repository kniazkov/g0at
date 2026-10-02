/**
 * @file io.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementations of input-output operations for UTF-8 encoded files and standard
 * input/output.
 */

#include "io.h"

#include "allocate.h"
#include "string_ext.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#    include <io.h>
#    include <windows.h>
#endif

bool init_io(void) {
    // platform-specific GPIO initialization...
    return true;
}

string_value_t read_utf8_file(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        return NULL_STRING_VALUE;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL_STRING_VALUE;
    }
    long file_size = ftell(file);
    if (file_size < 0 || (uintmax_t)file_size >= SIZE_MAX || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL_STRING_VALUE;
    }
    char *buffer = (char *)ALLOC((size_t)file_size + 1);
    size_t bytes_read = fread(buffer, 1, file_size, file);
    buffer[bytes_read] = '\0';
    bool read_failed = ferror(file) != 0;
    if (fclose(file) != 0 || read_failed) {
        FREE(buffer);
        return NULL_STRING_VALUE;
    }
    string_value_t result = bytes_read ? decode_utf8(buffer) : EMPTY_STRING_VALUE;
    FREE(buffer);
    return result;
}

bool write_utf8_file(const char *filename, const wchar_t *content) {
    FILE *file = fopen(filename, "w");
    if (file == NULL) {
        return false;
    }
    size_t size_of_buffer;
    char *buffer = encode_utf8_ex(content, &size_of_buffer);
    size_t bytes_written = fwrite(buffer, 1, size_of_buffer, file);
    bool result = bytes_written == size_of_buffer;
    if (fclose(file) != 0) {
        result = false;
    }
    FREE(buffer);
    return result;
}

void print_utf8(const wchar_t *content) {
    char *buffer = encode_utf8(content);
    printf("%s", buffer);
    FREE(buffer);
}

void fprintf_utf8(FILE *file, const wchar_t *format, ...) {
    va_list args;
    va_start(args, format);
    string_value_t value = format_string_vargs(format, args);
    va_end(args);
    if (value.data) {
        char *encoded_buffer = encode_utf8(value.data);
        fputs(encoded_buffer, file);
        FREE(encoded_buffer);
        FREE_STRING(value);
    }
}

bool read_digital_input(int index) {
    return false;
}

void write_digital_output(int index, bool value) {
    return;
}

string_value_t read_input_line(FILE *file) {
#ifdef _WIN32
    HANDLE handle = (HANDLE)_get_osfhandle(_fileno(file));
    DWORD mode;
    if (handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &mode)) {
        string_builder_t builder;
        init_string_builder(&builder, 0);
        string_value_t result = EMPTY_STRING_VALUE;
        for (;;) {
            wchar_t ch;
            DWORD read;
            if (!ReadConsoleW(handle, &ch, 1, &read, NULL)) {
                FREE_STRING(result);
                return NULL_STRING_VALUE;
            }
            if (!read || ch == L'\n')
                break;
            result = append_char(&builder, ch);
        }
        if (result.length && result.data[result.length - 1] == L'\r') {
            builder.data[--builder.length] = 0;
            result.length--;
        }
        return result;
    }
#endif
    size_t capacity = 128, length = 0;
    char *buffer = ALLOC(capacity);
    bool valid = true;
    int ch;
    while ((ch = fgetc(file)) != EOF && ch != '\n') {
        if (!ch)
            valid = false;
        if (length == capacity - 1) {
            if (capacity > SIZE_MAX / 2) {
                FREE(buffer);
                return NULL_STRING_VALUE;
            }
            capacity *= 2;
            char *larger = ALLOC(capacity);
            memcpy(larger, buffer, length);
            FREE(buffer);
            buffer = larger;
        }
        buffer[length++] = (char)ch;
    }
    if (ch == '\n' && length && buffer[length - 1] == '\r')
        length--;
    buffer[length] = 0;
    string_value_t result = !valid || ferror(file) ? NULL_STRING_VALUE
                            : length               ? decode_utf8(buffer)
                                                   : EMPTY_STRING_VALUE;
    FREE(buffer);
    return result;
}
