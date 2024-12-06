/*
    Copyright 2025 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <stdio.h>
#include <stdlib.h>
#include "allocate.h"

void *allocate(int size) {
    void *ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "\nOut of memmory.");
        exit(EXIT_FAILURE);
    }
    return ptr;
}
