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

wchar_t* read_utf8_file(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (file == NULL) {
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (file_size < 0) {
        fclose(file);
        return NULL;
    }

    char* buffer = (char*)ALLOC(file_size + 1);
    if (buffer == NULL) {
        fclose(file);
        return NULL;
    }

    size_t bytes_read = fread(buffer, 1, file_size, file);
    fclose(file);
    buffer[bytes_read] = '\0';

    if (setlocale(LC_CTYPE, "") == NULL) {
        FREE(buffer);
        return NULL;
    }

    size_t wchar_len = mbstowcs(NULL, buffer, 0);
    if (wchar_len == (size_t)-1) {
        FREE(buffer);
        return NULL;  // invalid UTF-8 encoding
    }

    wchar_t* wide_str = (wchar_t*)ALLOC((wchar_len + 1) * sizeof(wchar_t));
    if (wide_str == NULL) {
        FREE(buffer);
        return NULL;
    }

    mbstowcs(wide_str, buffer, wchar_len + 1);
    return wide_str;
}
