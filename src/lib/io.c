/**
 * @file io.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementations of input-output operations for UTF-8 encoded files
 *  and standard input/output.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "io.h"
#include "allocate.h"
#include "string_ext.h"

bool init_io(void) {
    // platform-specific GPIO initialization...
    return true;
}

#ifdef _WIN32
#define PATH_SEPARATOR '\\'
#else
#define PATH_SEPARATOR '/'
#endif

char *normalize_path(const char* path) {
    if (path == NULL) {
        return NULL;
    }
    size_t len = strlen(path);
    char* normalized = (char*)ALLOC(len + 1);
    for (size_t index = 0; index < len; index++) {
        if (path[index] == '/' || path[index] == '\\') {
            normalized[index] = PATH_SEPARATOR;
        } else {
            normalized[index] = path[index];
        }
    }
    normalized[len] = '\0';
    return normalized;
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

void fprintf_utf8(FILE *file, const wchar_t *format, ...) {
    va_list args;
    va_start(args, format);
    string_value_t value = format_string_vargs(format, args);
    va_end(args);
    if (value.data) {
        char* encoded_buffer = encode_utf8(value.data);
        vfprintf(file, encoded_buffer, args);
        FREE(encoded_buffer);
        if (value.should_free) {
            FREE(value.data);
        }
    }
}

bool read_digital_input(int index) {
    return false;
}

void write_digital_output(int index, bool value) {
    return;
}
