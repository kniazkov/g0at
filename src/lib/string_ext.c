/**
 * @file string_ext.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Extension of the standard C library for working with strings.
 */

#include "string_ext.h"
#include "allocate.h"

wchar_t *WSTRDUP(const wchar_t *wstr) {
    if (wstr == NULL) {
        return (wchar_t *)CALLOC(sizeof(wchar_t));
    }

    size_t len = wcslen(wstr);
    size_t mem_len = (len + 1) * sizeof(wchar_t);
    wchar_t *new_str = (wchar_t *)ALLOC(mem_len);
    memcpy(new_str, wstr, mem_len);
    return new_str;
}
