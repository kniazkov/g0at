/**
 * @file io.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementations of input-output operations for UTF-8 encoded files
 *  and standard input/output.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "io.h"
#include "allocate.h"
#include "string_ext.h"

bool init_io(void) {
    // platform-specific GPIO initialization...
    return true;
}

string_value_t read_utf8_file(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        return (string_value_t){ NULL, 0, false };
    }
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (file_size < 0) {
        fclose(file);
        return (string_value_t){ NULL, 0, false };
    }
    char *buffer = (char*)ALLOC(file_size + 1);
    size_t bytes_read = fread(buffer, 1, file_size, file);
    buffer[bytes_read] = '\0';
    fclose(file);
    string_value_t result = decode_utf8(buffer);
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
    fclose(file);
    FREE(buffer);
    return result;
}

void print_utf8(const wchar_t *content) {
    char* buffer = encode_utf8(content);
    printf("%s", buffer);
    FREE(buffer);
}

void printf_utf8(const wchar_t *format, ...) {
    char* utf8_format = encode_utf8(format);
    va_list args;
    va_start(args, format);
    vprintf(utf8_format, args);
    va_end(args);
    FREE(utf8_format);
}

void err_printf_utf8(const wchar_t *format, ...) {
    char* utf8_format = encode_utf8(format);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, utf8_format, args);
    va_end(args);
    FREE(utf8_format);
}

bool read_digital_input(int index) {
    return false;
}

void write_digital_output(int index, bool value) {
    return;
}
