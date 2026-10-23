/**
 * @file io.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementations of input-output operations for UTF-8 encoded files and standard
 * input/output.
 */

#include "io.h"

#include "allocate.h"
#include "file_system.h"
#include "string_ext.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#    include <io.h>
#    include <windows.h>
#else
#    include <unistd.h>
#endif

/** @brief Whether the cursor on stdout is currently at the start of a line. */
static bool stdout_line_start = true;

bool init_io(void) {
    // platform-specific GPIO initialization...
    return true;
}

string_value_t read_utf8_file(const char *filename) {
    FILE *file = FOPEN(filename, "r");
    if (file == NULL) {
        return NULL_STRING_VALUE;
    }
    if (FSEEK(file, 0, SEEK_END) != 0) {
        FCLOSE(file);
        return NULL_STRING_VALUE;
    }
    long file_size = FTELL(file);
    if (file_size < 0 || (uintmax_t)file_size >= SIZE_MAX || FSEEK(file, 0, SEEK_SET) != 0) {
        FCLOSE(file);
        return NULL_STRING_VALUE;
    }
    char *buffer = (char *)ALLOC((size_t)file_size + 1);
    size_t bytes_read = FREAD(buffer, 1, file_size, file);
    buffer[bytes_read] = '\0';
    bool read_failed = FERROR(file) != 0;
    if (FCLOSE(file) != 0 || read_failed) {
        FREE(buffer);
        return NULL_STRING_VALUE;
    }
    string_value_t result = bytes_read ? decode_utf8_n(buffer, bytes_read) : EMPTY_STRING_VALUE;
    FREE(buffer);
    return result;
}

bool write_utf8_file(const char *filename, const gchar_t *content) {
    FILE *file = FOPEN(filename, "w");
    if (file == NULL) {
        return false;
    }
    size_t size_of_buffer;
    char *buffer = encode_utf8_ex(content, &size_of_buffer);
    size_t bytes_written = FWRITE(buffer, 1, size_of_buffer, file);
    bool result = bytes_written == size_of_buffer;
    if (FCLOSE(file) != 0) {
        result = false;
    }
    FREE(buffer);
    return result;
}

void print_utf8_view(string_view_t value) {
    size_t size;
    char *buffer = encode_utf8_view(value, &size);
    if (!buffer)
        return;
    FWRITE(buffer, 1, size, stdout);
    FREE(buffer);
    size_t length = value.length;
    const gchar_t *content = value.data;
    if (length > 0) {
        stdout_line_start = content[length - 1] == U'\n';
    }
}

void print_utf8(const gchar_t *content) {
    print_utf8_view((string_view_t){content, STRLEN(content)});
}

bool stdout_at_line_start(void) {
    return stdout_line_start;
}

void set_stdout_line_start(bool value) {
    stdout_line_start = value;
}

bool stdin_is_console(void) {
#ifdef _WIN32
    HANDLE handle = (HANDLE)_get_osfhandle(_fileno(stdin));
    if (handle != INVALID_HANDLE_VALUE) {
        DWORD mode;
        return GetConsoleMode(handle, &mode) != 0;
    }
    return false;
#else
    return isatty(fileno(stdin)) != 0;
#endif
}

bool stdout_is_console(void) {
#ifdef _WIN32
    HANDLE handle = (HANDLE)_get_osfhandle(_fileno(stdout));
    if (handle != INVALID_HANDLE_VALUE) {
        DWORD mode;
        return GetConsoleMode(handle, &mode) != 0;
    }
    return false;
#else
    return isatty(fileno(stdout)) != 0;
#endif
}

void fprintf_utf8(FILE *file, const gchar_t *format, ...) {
    va_list args;
    va_start(args, format);
    string_value_t value = format_string_vargs(format, args);
    va_end(args);
    if (value.data) {
        size_t size;
        char *encoded_buffer = encode_utf8_view(VALUE_TO_VIEW(value), &size);
        if (encoded_buffer)
            FWRITE(encoded_buffer, 1, size, file);
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
            if (!read || ch == U'\n')
                break;
            uint32_t scalar = ch;
            if (ch >= 0xd800 && ch <= 0xdbff) {
                wchar_t low;
                if (!ReadConsoleW(handle, &low, 1, &read, NULL) || !read || low < 0xdc00
                    || low > 0xdfff) {
                    FREE_STRING(result);
                    return NULL_STRING_VALUE;
                }
                scalar = 0x10000 + ((ch - 0xd800) << 10) + low - 0xdc00;
            }
            if (!goat_is_scalar(scalar)) {
                FREE_STRING(result);
                return NULL_STRING_VALUE;
            }
            result = append_char(&builder, scalar);
        }
        if (result.length && result.data[result.length - 1] == U'\r') {
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
    while ((ch = FGETC(file)) != EOF && ch != '\n') {
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
    string_value_t result = !valid || FERROR(file) ? NULL_STRING_VALUE
                            : length               ? decode_utf8_n(buffer, length)
                                                   : EMPTY_STRING_VALUE;
    FREE(buffer);
    return result;
}

string_value_t read_utf8_file_view(string_view_t filename) {
    char *path = encode_utf8_path(filename);
    if (!path)
        return NULL_STRING_VALUE;
    string_value_t result = read_utf8_file(path);
    FREE(path);
    return result;
}
