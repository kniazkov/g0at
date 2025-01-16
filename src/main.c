/**
 * @file main.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Main entry point of the Goat interpreter.
 */

#include "lib/allocate.h"
#include "lib/io.h"
#include "lib/string_ext.h"
#include <stdio.h>

int main(int argc, char** argv) {
    if (!init_io()) {
        return -1;
    }

    char *str = encode_utf8(L"оно работает it works. %d");
    string_value_t wstr = decode_utf8(str);

    write_utf8_file("test.txt", wstr.data);

    string_value_t wstr2 = read_utf8_file("test.txt");
    
    //print_utf8(L"оно работает.");
    printf_utf8(wstr2.data, 1024);

    FREE(str);
    FREE(wstr.data);
    FREE(wstr2.data);

    printf("\n\n%d", get_allocated_memory_size());
    return 0;
}
