/** @file unicode.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Portable Unicode scalar storage and locale-independent operations.
 */
#ifndef GOAT_UNICODE_H
#define GOAT_UNICODE_H
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <uchar.h>

/** @brief One UTF-32 Unicode scalar; never a UTF-16 code unit. */
typedef char32_t goat_char_t;
_Static_assert(CHAR_BIT == 8 && sizeof(goat_char_t) == 4, "Goat requires 8-bit bytes and UTF-32");
#ifndef __STDC_UTF_32__
#    error "Goat requires UTF-32 char32_t"
#endif
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
#    if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#        error "Goat v1 supports only little-endian targets"
#    endif
#elif !defined(_WIN32)
#    error "Unknown target byte order"
#endif

/** @brief Checks a Unicode scalar without normalization. */
static inline bool goat_is_scalar(uint32_t ch) {
    return ch <= 0x10ffff && !(ch >= 0xd800 && ch <= 0xdfff);
}

/** @brief Length of a terminated literal or identifier, not a general string view. */
static inline size_t goat_strlen(const goat_char_t *s) {
    const goat_char_t *p = s;
    while (*p)
        ++p;
    return (size_t)(p - s);
}

/** @brief Lexicographic comparison of exactly n elements, including zeros. */
static inline int goat_memcmp(const goat_char_t *a, const goat_char_t *b, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
    return 0;
}

/** @brief Copies exactly n elements. */
static inline goat_char_t *goat_memcpy(goat_char_t *a, const goat_char_t *b, size_t n) {
    if (n)
        memcpy(a, b, n * sizeof(*a));
    return a;
}

/** @brief Finds an element within a bounded range. */
static inline const goat_char_t *goat_memchr(const goat_char_t *s, goat_char_t c, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (s[i] == c)
            return s + i;
    return NULL;
}

/** @brief Compares terminated literals/identifiers. */
static inline int goat_strcmp(const goat_char_t *a, const goat_char_t *b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a < *b ? -1 : *a > *b ? 1 : 0;
}

/** @brief Bounded comparison of terminated identifiers. */
static inline int goat_strncmp(const goat_char_t *a, const goat_char_t *b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
        if (!a[i])
            return 0;
    }
    return 0;
}

/** @brief Finds an element in a terminated identifier. */
static inline const goat_char_t *goat_strchr(const goat_char_t *s, goat_char_t c) {
    do {
        if (*s == c)
            return s;
    } while (*s++);
    return NULL;
}

/** @brief Finds a terminated substring (diagnostic/test helper). */
static inline const goat_char_t *goat_strstr(const goat_char_t *s, const goat_char_t *part) {
    size_t n = goat_strlen(part);
    do {
        if (!goat_strncmp(s, part, n))
            return s;
    } while (*s++);
    return NULL;
}

/** @brief ASCII decimal digit, as required by the language number grammar. */
static inline int goat_is_digit(goat_char_t c) {
    return c >= U'0' && c <= U'9';
}

/** @brief Unicode 17.0.0 White_Space property. */
int goat_is_space(goat_char_t c);
/** @brief Simple one-scalar uppercase mapping. */
goat_char_t goat_simple_upper(goat_char_t c);
/** @brief Simple one-scalar lowercase mapping. */
goat_char_t goat_simple_lower(goat_char_t c);
/** @brief Full default case mapping at index; returns 1..3 scalars, including context rules. */
size_t goat_case_mapping(const goat_char_t *s,
                         size_t length,
                         size_t index,
                         bool upper,
                         goat_char_t out[3]);

#endif /* GOAT_UNICODE_H */
