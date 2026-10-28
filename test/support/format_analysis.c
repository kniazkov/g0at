/** @file format_analysis.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent facts and summary oracle; no production validators.
 */
#include "test/support/format_analysis.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fail(char *error, size_t size, const char *message) {
    if (error && size)
        snprintf(error, size, "%s", message);
    return false;
}

static bool is_kind(const char *kind, const char *expected) {
    return kind && strcmp(kind, expected) == 0;
}

/* Validate scalar UTF-8, including strings constructed directly by a caller. */
static bool scalar_count(yyjson_mut_val *value, size_t *count) {
    if (!yyjson_mut_is_str(value))
        return false;
    const unsigned char *s = (const unsigned char *)yyjson_mut_get_str(value);
    size_t length = yyjson_mut_get_len(value), n = 0;
    for (size_t i = 0; i < length;) {
        unsigned char first = s[i++];
        uint32_t code;
        unsigned trailing;
        if (first < 0x80) {
            code = first;
            trailing = 0;
        } else if (first >= 0xc2 && first <= 0xdf) {
            code = first & 31;
            trailing = 1;
        } else if (first >= 0xe0 && first <= 0xef) {
            code = first & 15;
            trailing = 2;
        } else if (first >= 0xf0 && first <= 0xf4) {
            code = first & 7;
            trailing = 3;
        } else {
            return false;
        }
        if (length - i < trailing)
            return false;
        for (unsigned j = 0; j < trailing; ++j) {
            unsigned char next = s[i++];
            if ((next & 0xc0) != 0x80)
                return false;
            code = (code << 6) | (next & 63);
        }
        if ((trailing == 1 && code < 0x80) || (trailing == 2 && code < 0x800)
            || (trailing == 3 && code < 0x10000) || code > 0x10ffff
            || (code >= 0xd800 && code <= 0xdfff))
            return false;
        ++n;
    }
    *count = n;
    return true;
}

bool format_scalar(const char *kind, yyjson_mut_val *value, char *error, size_t error_size) {
    bool valid = false;
    size_t count;
    if (is_kind(kind, "integer")) {
        valid = yyjson_mut_is_sint(value)
                || (yyjson_mut_is_uint(value) && yyjson_mut_get_uint(value) <= INT64_MAX);
    } else if (is_kind(kind, "real")) {
        valid = yyjson_mut_is_str(value) && yyjson_mut_get_len(value) == 16;
        if (valid) {
            const char *s = yyjson_mut_get_str(value);
            for (size_t i = 0; i < 16; ++i)
                if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
                    valid = false;
        }
    } else if (is_kind(kind, "character")) {
        valid = scalar_count(value, &count) && count == 1;
    } else if (is_kind(kind, "string")) {
        valid = scalar_count(value, &count);
    } else if (is_kind(kind, "boolean")) {
        valid = yyjson_mut_is_bool(value);
    } else if (is_kind(kind, "null")) {
        valid = yyjson_mut_is_null(value);
    }
    return valid || fail(error, error_size, "invalid scalar constant");
}

bool format_facts(yyjson_mut_doc *doc,
                  yyjson_mut_val *value,
                  yyjson_mut_val *defs,
                  char *error,
                  size_t error_size) {
    if (!format_json_shape(value, yyjson_mut_obj_get(defs, "facts"), defs, error, error_size))
        return false;
    const char *kind = yyjson_mut_get_str(yyjson_mut_obj_get(value, "type"));
    if (is_kind(kind, "null") && yyjson_mut_get_bool(yyjson_mut_obj_get(value, "nonNull")))
        return fail(error, error_size, "contradictory nonNull");
    yyjson_mut_val *constant = yyjson_mut_obj_get(value, "value");
    if (constant) {
        if (!format_scalar(kind, constant, error, error_size))
            return false;
        if (is_kind(kind, "null"))
            yyjson_mut_obj_remove_key(value, "value");
    }
    yyjson_mut_val *range = yyjson_mut_obj_get(value, "range");
    if (range) {
        if (!is_kind(kind, "integer"))
            return fail(error, error_size, "range requires integer");
        int64_t low = yyjson_mut_get_sint(yyjson_mut_obj_get(range, "min"));
        int64_t high = yyjson_mut_get_sint(yyjson_mut_obj_get(range, "max"));
        if (low > high)
            return fail(error, error_size, "reversed range");
        if (constant) {
            int64_t number = yyjson_mut_get_sint(constant);
            if (number < low || number > high)
                return fail(error, error_size, "constant outside range");
            yyjson_mut_obj_remove_key(value, "range");
        } else if (low == high) {
            if (!yyjson_mut_obj_add_sint(doc, value, "value", low))
                return fail(error, error_size, "out of memory");
            yyjson_mut_obj_remove_key(value, "range");
        } else if (low == INT64_MIN && high == INT64_MAX) {
            yyjson_mut_obj_remove_key(value, "range");
        }
    }
    yyjson_mut_val *elements = yyjson_mut_obj_get(value, "elements");
    if (elements) {
        if (!is_kind(kind, "array"))
            return fail(error, error_size, "elements requires array");
        if (!format_facts(doc, elements, defs, error, error_size))
            return false;
        if (!yyjson_mut_obj_size(elements)
            || yyjson_mut_get_bool(yyjson_mut_obj_get(value, "empty")))
            yyjson_mut_obj_remove_key(value, "elements");
    }
    if (yyjson_mut_obj_get(value, "empty") && !is_kind(kind, "array"))
        return fail(error, error_size, "empty requires array");
    if (kind && !is_kind(kind, "null"))
        yyjson_mut_obj_remove_key(value, "nonNull");
    return true;
}

static unsigned types(yyjson_mut_val *facts) {
    const char *names[] = {"null",
                           "boolean",
                           "integer",
                           "real",
                           "character",
                           "string",
                           "function",
                           "array",
                           "object"};
    const char *kind = yyjson_mut_get_str(yyjson_mut_obj_get(facts, "type"));
    unsigned mask = 0;
    if (!kind)
        mask = (1u << 9) - 1;
    else if (is_kind(kind, "numeric"))
        mask = (1u << 2) | (1u << 3);
    else
        for (unsigned i = 0; i < 9; ++i)
            if (is_kind(kind, names[i]))
                mask = 1u << i;
    if (yyjson_mut_get_bool(yyjson_mut_obj_get(facts, "nonNull")))
        mask &= ~1u;
    return mask;
}

static void bounds(yyjson_mut_val *facts, int64_t *low, int64_t *high) {
    yyjson_mut_val *value = yyjson_mut_obj_get(facts, "value");
    if (value) {
        *low = *high = yyjson_mut_get_sint(value);
    } else {
        yyjson_mut_val *range = yyjson_mut_obj_get(facts, "range");
        *low = range ? yyjson_mut_get_sint(yyjson_mut_obj_get(range, "min")) : INT64_MIN;
        *high = range ? yyjson_mut_get_sint(yyjson_mut_obj_get(range, "max")) : INT64_MAX;
    }
}

bool format_facts_intersect(yyjson_mut_val *a, yyjson_mut_val *b) {
    unsigned common = types(a) & types(b);
    if (common & (1u << 2)) {
        int64_t amin, amax, bmin, bmax;
        bounds(a, &amin, &amax);
        bounds(b, &bmin, &bmax);
        if (amin <= bmax && bmin <= amax)
            return true;
        common &= ~(1u << 2);
    }
    if (!common)
        return false;
    yyjson_mut_val *av = yyjson_mut_obj_get(a, "value");
    yyjson_mut_val *bv = yyjson_mut_obj_get(b, "value");
    return !av || !bv || format_json_equal(av, bv);
}

static bool consistent_summaries(yyjson_mut_val *summaries, size_t arity) {
    size_t count = yyjson_mut_arr_size(summaries);
    for (size_t i = 0; i < count; ++i) {
        yyjson_mut_val *a = yyjson_mut_arr_get(summaries, i);
        yyjson_mut_val *ar = yyjson_mut_obj_get(a, "result");
        if (!ar)
            continue;
        for (size_t j = i + 1; j < count; ++j) {
            yyjson_mut_val *b = yyjson_mut_arr_get(summaries, j);
            yyjson_mut_val *br = yyjson_mut_obj_get(b, "result");
            if (!br || format_facts_intersect(ar, br))
                continue;
            yyjson_mut_val *aa = yyjson_mut_obj_get(yyjson_mut_obj_get(a, "when"), "arguments");
            yyjson_mut_val *ba = yyjson_mut_obj_get(yyjson_mut_obj_get(b, "when"), "arguments");
            bool overlap = true;
            for (size_t k = 0; k < arity && overlap; ++k)
                overlap =
                    format_facts_intersect(yyjson_mut_arr_get(aa, k), yyjson_mut_arr_get(ba, k));
            if (overlap)
                return false;
        }
    }
    return true;
}

typedef struct {
    yyjson_mut_val *value;
    char *canonical;
} summary_item_t;

static int compare_summaries(const void *left, const void *right) {
    const summary_item_t *a = left, *b = right;
    return strcmp(a->canonical, b->canonical);
}

bool format_analysis(yyjson_mut_doc *doc,
                     yyjson_mut_val *value,
                     size_t arity,
                     yyjson_mut_val *defs,
                     char *error,
                     size_t error_size) {
    if (!format_json_shape(value, yyjson_mut_obj_get(defs, "analysis"), defs, error, error_size))
        return false;
    yyjson_mut_val *summaries = yyjson_mut_obj_get(value, "summaries");
    size_t count = yyjson_mut_arr_size(summaries);
    summary_item_t *items = calloc(count ? count : 1, sizeof(*items));
    if (!items)
        return fail(error, error_size, "out of memory");
    bool success = false;
    for (size_t i = 0; i < count; ++i) {
        yyjson_mut_val *summary = yyjson_mut_arr_get(summaries, i);
        yyjson_mut_val *when = yyjson_mut_obj_get(summary, "when");
        yyjson_mut_val *args = yyjson_mut_obj_get(when, "arguments");
        if (args) {
            if (yyjson_mut_arr_size(args) != arity) {
                fail(error, error_size, "analysis arity");
                goto done;
            }
            bool informative = false;
            for (size_t j = 0; j < arity; ++j) {
                yyjson_mut_val *arg = yyjson_mut_arr_get(args, j);
                if (!format_facts(doc, arg, defs, error, error_size))
                    goto done;
                informative |= yyjson_mut_obj_size(arg) != 0;
            }
            if (!informative)
                yyjson_mut_obj_remove_key(when, "arguments");
        }
        yyjson_mut_val *result = yyjson_mut_obj_get(summary, "result");
        if (result && yyjson_mut_obj_get(summary, "normalReturn")) {
            fail(error, error_size, "no normal return with result");
            goto done;
        }
        if (result) {
            if (!format_facts(doc, result, defs, error, error_size))
                goto done;
            if (!yyjson_mut_obj_size(result))
                yyjson_mut_obj_remove_key(summary, "result");
        }
        yyjson_mut_val *effects = yyjson_mut_obj_get(summary, "effects");
        if (effects) {
            yyjson_mut_val *may = yyjson_mut_obj_get(effects, "may");
            const char *names[] = {"externalRead", "externalWrite", "input", "output"};
            yyjson_mut_val *ordered[4] = {NULL, NULL, NULL, NULL};
            for (size_t j = 0; j < yyjson_mut_arr_size(may); ++j) {
                yyjson_mut_val *effect = yyjson_mut_arr_get(may, j);
                for (size_t k = 0; k < 4; ++k)
                    if (yyjson_mut_equals_str(effect, names[k]))
                        ordered[k] = effect;
            }
            yyjson_mut_arr_clear(may);
            for (size_t k = 0; k < 4; ++k)
                if (ordered[k])
                    yyjson_mut_arr_append(may, ordered[k]);
            if (!yyjson_mut_arr_size(may)
                && yyjson_mut_get_bool(yyjson_mut_obj_get(effects, "unknown")))
                yyjson_mut_obj_remove_key(summary, "effects");
        }
        items[i].value = summary;
        items[i].canonical = format_json_canonical(summary, NULL);
        if (!items[i].canonical) {
            fail(error, error_size, "canonical summary allocation");
            goto done;
        }
    }
    qsort(items, count, sizeof(*items), compare_summaries);
    for (size_t i = 1; i < count; ++i) {
        if (!strcmp(items[i - 1].canonical, items[i].canonical)) {
            fail(error, error_size, "duplicate normalized summary");
            goto done;
        }
    }
    if (!consistent_summaries(summaries, arity)) {
        fail(error, error_size, "contradictory normal-result summaries");
        goto done;
    }
    yyjson_mut_arr_clear(summaries);
    for (size_t i = 0; i < count; ++i)
        if (yyjson_mut_obj_size(items[i].value) != 1)
            yyjson_mut_arr_append(summaries, items[i].value);
    if (!yyjson_mut_arr_size(summaries))
        yyjson_mut_set_null(value);
    success = true;
done:
    for (size_t i = 0; i < count; ++i)
        free(items[i].canonical);
    free(items);
    return success;
}
