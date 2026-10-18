/** @file number_format.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Shared VM and native real-number text formatting.
 */
#include "number_format.h"

#include <stdio.h>
#include <string.h>

void double_to_string(double value, char *buffer, size_t buffer_size) {
    if ((value >= 1e-10 && value <= 1e10) || (value <= -1e-10 && value >= -1e10)) {
        char temp_buffer[32];
        snprintf(temp_buffer, sizeof(temp_buffer), "%.11f", value);
        char *dot = strchr(temp_buffer, '.');
        if (dot) {
            char *end = dot + strlen(dot) - 1;
            while (end > dot && *end == '0') {
                *end-- = '\0';
            }
            if (*end == '.') {
                *(end + 1) = '0';
            }
        }
        snprintf(buffer, buffer_size, "%s", temp_buffer);
    } else {
        snprintf(buffer, buffer_size, "%g", value);
    }
}
