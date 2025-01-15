/**
 * @file io.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementations of input-output operations for UTF-8 encoded files
 *  and standard input/output.
 */

#include <stdio.h>
#include <stdlib.h>
#include <locale.h>

#include "io.h"
#include "allocate.h"

bool init_io(void) {
    if (setlocale(LC_CTYPE, "") == NULL) {
        fprintf(stderr, "\nFailed to set locale for input-output operations.\n");
        return false;
    }

    // Platform-specific GPIO initialization...

    return true;
}

wchar_t* read_utf8_file(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (file == NULL) {
        return NULL;
    }
    
    char* buffer = NULL;
    wchar_t* wide_str = NULL;

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (file_size < 0) {
        goto cleanup;
    }

    buffer = (char*)ALLOC(file_size + 1);
    size_t bytes_read = fread(buffer, 1, file_size, file);
    buffer[bytes_read] = '\0';

    size_t wchar_len = mbstowcs(NULL, buffer, 0);
    if (wchar_len == (size_t)-1) { // invalid UTF-8 encoding
        goto cleanup;
    }

    wide_str = (wchar_t*)ALLOC((wchar_len + 1) * sizeof(wchar_t));
    mbstowcs(wide_str, buffer, wchar_len + 1);

cleanup:
    fclose(file);
    FREE(buffer);

    return wide_str;
}

bool write_utf8_file(const char* filename, const wchar_t* content) {
    FILE* file = fopen(filename, "w");
    if (file == NULL) {
        return false;
    }

    char* buffer = NULL;
    bool result = false;

    size_t len = wcslen(content);
    buffer = (char*)ALLOC((len * MB_CUR_MAX) + 1);
    size_t size_of_buffer = wcstombs(buffer, content, len * MB_CUR_MAX);
    if (size_of_buffer == (size_t)-1) {
        goto cleanup;
    }

    size_t bytes_written = fwrite(buffer, 1, size_of_buffer, file);
    result = bytes_written == size_of_buffer;

cleanup:
    fclose(file);
    FREE(buffer);

    return result;
}

void print_utf8(const wchar_t* content) {
    size_t len = wcslen(content);
    char* buffer = (char*)ALLOC((len * MB_CUR_MAX) + 1);
    wcstombs(buffer, content, len * MB_CUR_MAX);
    printf("%s", buffer);
    FREE(buffer);
}

bool read_digital_input(int index) {
    return false;
}

void write_digital_output(int index, bool value) {
    return;
}
