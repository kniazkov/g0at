/** @file native_runner_policy.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Shared native runner acceptance and exhaustive-continuation policies.
 */
#include "native_runner_policy.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

yyjson_val *nr_object_get(yyjson_val *object, const char *name) {
    yyjson_val *found = NULL, *key, *value;
    size_t index, count, size = strlen(name);
    yyjson_obj_foreach(object, index, count, key, value) {
        if (yyjson_get_len(key) == size && !memcmp(yyjson_get_str(key), name, size))
            found = value;
    }
    return found;
}

static bool fail(char *error, size_t size, const char *message) {
    if (error && size)
        snprintf(error, size, "%s", message);
    return false;
}

static bool scalar(const unsigned char *p, size_t n, size_t *at, uint32_t *value) {
    if (*at >= n)
        return false;
    unsigned c = p[(*at)++], left, min;
    if (c < 128) {
        *value = c;
        return true;
    }
    if (c >= 0xc2 && c <= 0xdf) {
        *value = c & 31;
        left = 1;
        min = 128;
    } else if (c >= 0xe0 && c <= 0xef) {
        *value = c & 15;
        left = 2;
        min = 2048;
    } else if (c >= 0xf0 && c <= 0xf4) {
        *value = c & 7;
        left = 3;
        min = 65536;
    } else
        return false;
    if (left > n - *at)
        return false;
    while (left--) {
        c = p[(*at)++];
        if ((c & 0xc0) != 0x80)
            return false;
        *value = (*value << 6) | (c & 63);
    }
    return *value >= min && *value <= 0x10ffff && !(*value >= 0xd800 && *value <= 0xdfff);
}

bool nr_output_equal(const void *actual, size_t an, const void *expected, size_t en) {
    const unsigned char *a = actual, *e = expected;
    size_t ai = 0, ei = 0;
    if ((an && !a) || (en && !e))
        return false;
    while (ai < an && ei < en) {
        unsigned ac = a[ai++], ec = e[ei++];
        if (ac == '\r' && ai < an && a[ai] == '\n') {
            ai++;
            ac = '\n';
        }
        if (ec == '\r' && ei < en && e[ei] == '\n') {
            ei++;
            ec = '\n';
        }
        if (ac != ec)
            return false;
    }
    return ai == an && ei == en;
}

static bool linebreak(uint32_t c) {
    return c == 10 || c == 13 || c == 11 || c == 12 || (c >= 28 && c <= 30) || c == 0x85
           || c == 0x2028 || c == 0x2029;
}

static bool intspace(uint32_t c) {
    return c == 9 || c == 10 || c == 11 || c == 12 || c == 13 || c == 32 || c == 0x85 || c == 0xa0
           || c == 0x1680 || (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029
           || c == 0x202f || c == 0x205f || c == 0x3000;
}

/* Independent Python Unicode decimal-zero inventory; no production Unicode helper. */
static bool digit(uint32_t c) {
    static const uint32_t zeros[] = {
        0x30,    0x660,   0x6f0,   0x7c0,   0x966,   0x9e6,   0xa66,   0xae6,   0xb66,   0xbe6,
        0xc66,   0xce6,   0xd66,   0xde6,   0xe50,   0xed0,   0xf20,   0x1040,  0x1090,  0x17e0,
        0x1810,  0x1946,  0x19d0,  0x1a80,  0x1a90,  0x1b50,  0x1bb0,  0x1c40,  0x1c50,  0xa620,
        0xa8d0,  0xa900,  0xa9d0,  0xa9f0,  0xaa50,  0xabf0,  0xff10,  0x104a0, 0x10d30, 0x11066,
        0x110f0, 0x11136, 0x111d0, 0x112f0, 0x11450, 0x114d0, 0x11650, 0x116c0, 0x11730, 0x118e0,
        0x11950, 0x11c50, 0x11d50, 0x11da0, 0x11f50, 0x16a60, 0x16ac0, 0x16b50, 0x1d7ce, 0x1d7d8,
        0x1d7e2, 0x1d7ec, 0x1d7f6, 0x1e140, 0x1e2f0, 0x1e4f0, 0x1e950, 0x1fbf0};
    for (size_t i = 0; i < sizeof(zeros) / sizeof(zeros[0]); i++)
        if (c >= zeros[i] && c < zeros[i] + 10)
            return true;
    return false;
}

static bool nonzero_digit(uint32_t c) {
    static const uint32_t zeros[] = {
        0x30,    0x660,   0x6f0,   0x7c0,   0x966,   0x9e6,   0xa66,   0xae6,   0xb66,   0xbe6,
        0xc66,   0xce6,   0xd66,   0xde6,   0xe50,   0xed0,   0xf20,   0x1040,  0x1090,  0x17e0,
        0x1810,  0x1946,  0x19d0,  0x1a80,  0x1a90,  0x1b50,  0x1bb0,  0x1c40,  0x1c50,  0xa620,
        0xa8d0,  0xa900,  0xa9d0,  0xa9f0,  0xaa50,  0xabf0,  0xff10,  0x104a0, 0x10d30, 0x11066,
        0x110f0, 0x11136, 0x111d0, 0x112f0, 0x11450, 0x114d0, 0x11650, 0x116c0, 0x11730, 0x118e0,
        0x11950, 0x11c50, 0x11d50, 0x11da0, 0x11f50, 0x16a60, 0x16ac0, 0x16b50, 0x1d7ce, 0x1d7d8,
        0x1d7e2, 0x1d7ec, 0x1d7f6, 0x1e140, 0x1e2f0, 0x1e4f0, 0x1e950, 0x1fbf0};
    for (size_t i = 0; i < sizeof(zeros) / sizeof(zeros[0]); i++)
        if (c == zeros[i])
            return false;
    return true;
}

static bool positive_integer(const unsigned char *s, size_t size) {
    size_t i = 0, digits = 0;
    uint32_t c;
    bool started = false, negative = false, any = false, nonzero = false, underscore = false,
         ended = false;
    while (i < size) {
        if (!scalar(s, size, &i, &c))
            return false;
        if (!started && intspace(c))
            continue;
        if (!started) {
            started = true;
            if (c == '+' || c == '-') {
                negative = c == '-';
                continue;
            }
        }
        if (ended) {
            if (!intspace(c))
                return false;
            continue;
        }
        if (digit(c)) {
            if (++digits > 4300)
                return false;
            any = true;
            nonzero |= nonzero_digit(c);
            underscore = false;
        } else if (c == '_' && any && !underscore)
            underscore = true;
        else if (intspace(c) && any && !underscore)
            ended = true;
        else
            return false;
    }
    return any && !underscore && !negative && nonzero;
}

typedef struct {
    const unsigned char *p;
    size_t n;
} slice_t;

static bool equals(slice_t s, const char *text) {
    return s.p && s.n == strlen(text) && !memcmp(s.p, text, s.n);
}

static const char *keys[] = {"mode", "preparation", "bound", "attempts", "succeeded", "retries"};

static bool report_line(const unsigned char *p, size_t n, slice_t fields[6]) {
    const unsigned char *eq = memchr(p, '=', n);
    if (!eq)
        return false;
    for (size_t i = 0; i < 6; i++)
        if ((size_t)(eq - p) == strlen(keys[i]) && !memcmp(p, keys[i], (size_t)(eq - p)))
            fields[i] = (slice_t){eq + 1, n - (size_t)(eq + 1 - p)};
    return true;
}

bool nr_check_report(const void *bytes,
                     size_t n,
                     yyjson_val *case_value,
                     bool native,
                     char *error,
                     size_t error_size) {
    const unsigned char *p = bytes;
    slice_t fields[6] = {{0}};
    size_t i = 0, start = 0;
    uint32_t c;
    if (n && !p)
        return fail(error, error_size, "missing report bytes");
    while (i < n) {
        size_t pos = i;
        if (!scalar(p, n, &i, &c))
            return fail(error, error_size, "report is not UTF-8");
        if (linebreak(c)) {
            if (!report_line(p + start, pos - start, fields))
                return fail(error, error_size, "report line lacks '='");
            if (c == 13 && i < n && p[i] == 10)
                i++;
            start = i;
        }
    }
    if (start < n && !report_line(p + start, n - start, fields))
        return fail(error, error_size, "report line lacks '='");
    if (!equals(fields[0], native ? "required" : "off"))
        return fail(error, error_size, "wrong report mode");
    uint64_t calls = 0;
    if (native) {
        yyjson_val *v = nr_object_get(case_value, "calls");
        if (!yyjson_is_uint(v) || !(calls = yyjson_get_uint(v)))
            return fail(error, error_size, "positive native calls required");
    }
    char text[32];
    snprintf(text, sizeof(text), "%llu", (unsigned long long)calls);
    if (!equals(fields[3], text) || !equals(fields[4], text) || !equals(fields[5], "0"))
        return fail(error, error_size, "wrong attempt/success/retry count");
    if (native) {
        if (!equals(fields[1], "ready") || !fields[2].p
            || !positive_integer(fields[2].p, fields[2].n))
            return fail(error, error_size, "native functions not ready/bound");
        yyjson_val *bound = nr_object_get(case_value, "bound");
        if (bound) {
            const char *wanted = NULL;
            if (yyjson_is_uint(bound)) {
                snprintf(text, sizeof(text), "%llu", (unsigned long long)yyjson_get_uint(bound));
                wanted = text;
            } else if (yyjson_is_str(bound)
                       && strlen(yyjson_get_str(bound)) == yyjson_get_len(bound))
                wanted = yyjson_get_str(bound);
            if (!wanted || !equals(fields[2], wanted))
                return fail(error, error_size, "wrong bound count");
        }
    }
    return true;
}

static const char *name_of(yyjson_val *obj) {
    yyjson_val *v = nr_object_get(obj, "name");
    const char *s = yyjson_get_str(v);
    return s && strlen(s) == yyjson_get_len(v) ? s : NULL;
}

static bool valid_name(const char *s) {
    if (!s || *s < 'a' || *s > 'z')
        return false;
    for (s++; *s; s++)
        if (!(*s >= 'a' && *s <= 'z') && !(*s >= '0' && *s <= '9') && *s != '_')
            return false;
    return true;
}

bool nr_manifest_validate(yyjson_val *cases,
                          nr_exists_fn exists,
                          void *user,
                          char *error,
                          size_t error_size) {
    if (!yyjson_is_arr(cases) || !yyjson_arr_size(cases) || !exists)
        return fail(error, error_size, "native list must be nonempty array");
    const char *allowed[] =
        {"name", "calls", "bound", "example", "driver", "generated", "output_contains", "check"};
    size_t i, n;
    yyjson_val *c;
    yyjson_arr_foreach(cases, i, n, c) {
        if (!yyjson_is_obj(c))
            return fail(error, error_size, "case must be an object");
        size_t k, count;
        yyjson_val *key, *value;
        yyjson_obj_foreach(c, k, count, key, value) {
            (void)value;
            bool known = false;
            for (size_t a = 0; a < 8; a++)
                known |= yyjson_get_len(key) == strlen(allowed[a])
                         && !memcmp(yyjson_get_str(key), allowed[a], strlen(allowed[a]));
            if (!known)
                return fail(error, error_size, "unknown manifest field");
        }
        const char *name = name_of(c);
        if (!valid_name(name))
            return fail(error, error_size, "invalid case name");
        for (size_t j = 0; j < i; j++)
            if (!strcmp(name, name_of(yyjson_arr_get(cases, j))))
                return fail(error, error_size, "duplicate case name");
        yyjson_val *check = nr_object_get(c, "check");
        if (check) {
            const char *path = yyjson_get_str(check);
            if (!path || strlen(path) != yyjson_get_len(check) || !exists(path, true, user))
                return fail(error, error_size, "missing check");
        } else {
            yyjson_val *calls = nr_object_get(c, "calls");
            if (!yyjson_is_uint(calls) || !yyjson_get_uint(calls))
                return fail(error, error_size, "positive integer calls required");
            size_t length = strlen(name) + 14;
            char *path = malloc(length);
            if (!path)
                return fail(error, error_size, "allocation failed");
            snprintf(path, length, "%s/program.goat", name);
            bool found = exists(path, false, user);
            free(path);
            if (!found)
                return fail(error, error_size, "missing program");
        }
    }
    return true;
}

bool nr_run_selected(yyjson_val *cases,
                     const char *const *only,
                     size_t only_count,
                     nr_visit_fn visit,
                     void *user,
                     size_t *total,
                     size_t *failed,
                     char *error,
                     size_t error_size) {
    if (!total || !failed)
        return fail(error, error_size, "missing totals");
    *total = *failed = 0;
    if (!yyjson_is_arr(cases) || !visit || (only_count && !only))
        return fail(error, error_size, "invalid selection");
    size_t n = yyjson_arr_size(cases);
    for (size_t i = 0; i < n; i++)
        if (!name_of(yyjson_arr_get(cases, i)))
            return fail(error, error_size, "case name missing");
    for (size_t j = 0; j < only_count; j++) {
        bool found = false;
        if (!only[j])
            return fail(error, error_size, "invalid selected name");
        for (size_t i = 0; i < n; i++)
            found |= !strcmp(only[j], name_of(yyjson_arr_get(cases, i)));
        if (!found)
            return fail(error, error_size, "unknown selected name");
    }
    for (size_t i = 0; i < n; i++) {
        yyjson_val *c = yyjson_arr_get(cases, i);
        bool selected = !only_count;
        for (size_t j = 0; j < only_count; j++)
            selected |= !strcmp(only[j], name_of(c));
        if (selected) {
            (*total)++;
            if (!visit(c, user))
                (*failed)++;
        }
    }
    if (*failed)
        return fail(error, error_size, "one or more cases failed");
    return true;
}
