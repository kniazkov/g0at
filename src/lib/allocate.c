/**
 * @file allocate.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Memory allocation utility for the project.
 */

#include <stdio.h>
#include "allocate.h"

void *allocate(int size) {
    void *ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "\nOut of memmory.");
        exit(EXIT_FAILURE);
    }
    return ptr;
}
