/** @file unicode.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Unicode 17.0.0 default casing shared by compiler, VM and native runtime.
 */
#include "goat/unicode.h"

/** @brief Sparse scalar mapping with at most three output scalars. */
typedef struct {
    uint32_t scalar;
    unsigned length;
    gchar_t mapped[3];
} case_entry_t;

/** @brief Inclusive Unicode property range. */
typedef struct {
    uint32_t first, last;
} scalar_range_t;

#include "lib/strings/unicode_data.inc"
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static const case_entry_t *lookup(const case_entry_t *table, size_t count, uint32_t c) {
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (table[mid].scalar < c)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < count && table[lo].scalar == c ? table + lo : NULL;
}

static bool property(const scalar_range_t *table, size_t count, uint32_t c) {
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (table[mid].last < c)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < count && table[lo].first <= c;
}

int ISSPACE(gchar_t c) {
    return property(White_Space, COUNT(White_Space), c);
}

gchar_t TOUPPER(gchar_t c) {
    const case_entry_t *e = lookup(simple_upper, COUNT(simple_upper), c);
    return e ? e->mapped[0] : c;
}

gchar_t TOLOWER(gchar_t c) {
    const case_entry_t *e = lookup(simple_lower, COUNT(simple_lower), c);
    return e ? e->mapped[0] : c;
}

static bool final_sigma(const gchar_t *s, size_t n, size_t i) {
    size_t before = i;
    while (before && property(Case_Ignorable, COUNT(Case_Ignorable), s[before - 1]))
        --before;
    if (!before || !property(Cased, COUNT(Cased), s[before - 1]))
        return false;
    size_t after = i + 1;
    while (after < n && property(Case_Ignorable, COUNT(Case_Ignorable), s[after]))
        ++after;
    return after == n || !property(Cased, COUNT(Cased), s[after]);
}

size_t
goat_case_mapping(const gchar_t *s, size_t length, size_t index, bool upper, gchar_t out[3]) {
    if (!upper && s[index] == 0x3a3 && final_sigma(s, length, index)) {
        out[0] = 0x3c2;
        return 1;
    }
    const case_entry_t *e = upper ? lookup(full_upper, COUNT(full_upper), s[index])
                                  : lookup(full_lower, COUNT(full_lower), s[index]);
    if (!e) {
        out[0] = s[index];
        return 1;
    }
    MEMCPY(out, e->mapped, e->length);
    return e->length;
}
