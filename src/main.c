/**
 * @file main.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Main entry point of the Goat interpreter.
 */

#include "lib/io.h"

int main(int argc, char** argv) {
    if (!init_io()) {
        return -1;
    }

    print_utf8(L"оно работает.");
    return 0;
}
