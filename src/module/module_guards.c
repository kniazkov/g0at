/** @file module_guards.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Exact v1 scalar guards and their decidable intersection.
 */
#include "module_guards.h"

#include "module_json_internal.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t tag;
    int64_t integer;
    double real;
    const yyjson_val *text;
    const goat_module_string_v1 *view;
    uint64_t virtual_length;
} scalar;

static bool is(const yyjson_val *value, const char *text) {
    return yyjson_is_str(value) && yyjson_get_len(value) == strlen(text)
           && !memcmp(yyjson_get_str(value), text, strlen(text));
}

uint32_t goat_module_scalar_tag(const yyjson_val *type) {
    static const char *const names[] =
        {"null", "integer", "real", "string", "boolean", "character"};
    for (uint32_t i = 0; i < sizeof(names) / sizeof(*names); i++)
        if (is(type, names[i]))
            return i;
    return UINT32_MAX;
}

static uint32_t number(const yyjson_val *value) {
    int64_t result = 0;
    goat_json_integer(value, &result);
    return (uint32_t)result;
}

/* JSON reader already guarantees canonical valid UTF-8. */
static uint32_t next_scalar(const unsigned char **cursor) {
    const unsigned char *p = *cursor;
    uint32_t result = *p++;
    unsigned remaining = 0;
    if (result >= 0xf0) {
        result &= 7;
        remaining = 3;
    } else if (result >= 0xe0) {
        result &= 15;
        remaining = 2;
    } else if (result >= 0xc0) {
        result &= 31;
        remaining = 1;
    }
    while (remaining--)
        result = (result << 6) | (*p++ & 63);
    *cursor = p;
    return result;
}

static bool constant(const yyjson_val *record, scalar *out) {
    *out = (scalar){.tag = goat_module_scalar_tag(yyjson_obj_get(record, "type"))};
    const yyjson_val *value = yyjson_obj_get(record, "value");
    switch (out->tag) {
        case 0:
            return yyjson_is_null(value);
        case 1:
            return goat_json_integer(value, &out->integer);
        case 2: {
            if (!yyjson_is_str(value) || yyjson_get_len(value) != 16)
                return false;
            uint64_t bits = 0;
            const char *text = yyjson_get_str(value);
            for (size_t i = 0; i < 16; i++) {
                unsigned digit = (unsigned char)text[i];
                if (digit >= '0' && digit <= '9')
                    digit -= '0';
                else if (digit >= 'a' && digit <= 'f')
                    digit = digit - 'a' + 10;
                else
                    return false;
                bits = (bits << 4) | digit;
            }
            memcpy(&out->real, &bits, sizeof(bits));
            return true;
        }
        case 3:
            out->text = value;
            return yyjson_is_str(value);
        case 4:
            out->integer = yyjson_get_bool(value);
            return yyjson_is_bool(value);
        case 5: {
            if (!yyjson_is_str(value)
                || goat_json_scalar_count(yyjson_get_str(value), yyjson_get_len(value)) != 1)
                return false;
            const unsigned char *p = (const unsigned char *)yyjson_get_str(value);
            out->integer = next_scalar(&p);
            return true;
        }
        default:
            return false;
    }
}

static uint64_t string_length(scalar value) {
    if (value.text)
        return goat_json_scalar_count(yyjson_get_str(value.text), yyjson_get_len(value.text));
    return value.view ? value.view->length : value.virtual_length;
}

static bool strings_equal(scalar a, scalar b) {
    uint64_t length = string_length(a);
    if (length != string_length(b))
        return false;
    const unsigned char *pa = a.text ? (const unsigned char *)yyjson_get_str(a.text) : NULL;
    const unsigned char *pb = b.text ? (const unsigned char *)yyjson_get_str(b.text) : NULL;
    for (uint64_t i = 0; i < length; i++) {
        uint32_t ca = pa ? next_scalar(&pa) : a.view ? a.view->data[i] : 'x';
        uint32_t cb = pb ? next_scalar(&pb) : b.view ? b.view->data[i] : 'x';
        if (ca != cb)
            return false;
    }
    return true;
}

static bool satisfies(scalar value, const yyjson_val *predicate) {
    scalar bound;
    if (!constant(yyjson_obj_get(predicate, "constant"), &bound) || value.tag != bound.tag)
        return false;
    bool equal, less = false, greater = false;
    if (value.tag == 2) {
        equal = value.real == bound.real;
        less = value.real < bound.real;
        greater = value.real > bound.real;
    } else if (value.tag == 3) {
        equal = strings_equal(value, bound);
    } else {
        equal = value.integer == bound.integer;
        less = value.integer < bound.integer;
        greater = value.integer > bound.integer;
    }
    const yyjson_val *operation = yyjson_obj_get(predicate, "operation");
    if (is(operation, "equal"))
        return equal;
    if (is(operation, "notEqual"))
        return !equal;
    if (is(operation, "less"))
        return less;
    if (is(operation, "lessEqual"))
        return less || equal;
    if (is(operation, "greater"))
        return greater;
    return is(operation, "greaterEqual") && (greater || equal);
}

static bool
accepts(const yyjson_val *first, const yyjson_val *second, size_t argument, scalar value) {
    const yyjson_val *guards[] = {first, second};
    for (size_t g = 0; g < 2; g++) {
        const yyjson_val *conditions = yyjson_obj_get(guards[g], "conditions");
        for (size_t i = 0; i < yyjson_arr_size(conditions); i++) {
            const yyjson_val *predicate = yyjson_arr_get(conditions, i);
            if (number(yyjson_obj_get(predicate, "argument")) == argument
                && !satisfies(value, predicate))
                return false;
        }
    }
    return true;
}

static bool character(int64_t value) {
    return value >= 0 && value <= 0x10ffff && !(value >= 0xd800 && value <= 0xdfff);
}

/* Every nonempty interval contains an endpoint or its representable neighbor.
 * Excluded constants add only finitely many holes. NaN is a separate witness;
 * signed zeros compare equal. Strings have equality/exclusion only.
 */
static bool
witness(const yyjson_val *first, const yyjson_val *second, size_t argument, uint32_t tag) {
    scalar value = {.tag = tag};
    if (tag == 0)
        return accepts(first, second, argument, value);
    if (tag == 4) {
        if (accepts(first, second, argument, value))
            return true;
        value.integer = 1;
        return accepts(first, second, argument, value);
    }
    const int64_t integers[] = {INT64_MIN, INT64_MAX, 0};
    const int64_t characters[] = {0, 0x10ffff, 0xd7ff, 0xe000};
    const double reals[] = {0.0, INFINITY, -INFINITY, NAN};
    size_t count = tag == 1 ? 3 : tag == 2 || tag == 5 ? 4 : 0;
    for (size_t i = 0; i < count; i++) {
        if (tag == 1)
            value.integer = integers[i];
        else if (tag == 2)
            value.real = reals[i];
        else
            value.integer = characters[i];
        if (accepts(first, second, argument, value))
            return true;
    }
    uint64_t maximum_string = 0;
    const yyjson_val *guards[] = {first, second};
    for (size_t g = 0; g < 2; g++) {
        const yyjson_val *conditions = yyjson_obj_get(guards[g], "conditions");
        for (size_t i = 0; i < yyjson_arr_size(conditions); i++) {
            const yyjson_val *predicate = yyjson_arr_get(conditions, i);
            if (number(yyjson_obj_get(predicate, "argument")) != argument)
                continue;
            if (!constant(yyjson_obj_get(predicate, "constant"), &value))
                return false;
            if (accepts(first, second, argument, value))
                return true;
            if (tag == 3) {
                uint64_t length = string_length(value);
                if (length > maximum_string)
                    maximum_string = length;
            } else if (tag == 2) {
                double original = value.real;
                value.real = nextafter(original, -INFINITY);
                if (accepts(first, second, argument, value))
                    return true;
                value.real = nextafter(original, INFINITY);
                if (accepts(first, second, argument, value))
                    return true;
            } else {
                int64_t original = value.integer;
                if (original > INT64_MIN) {
                    value.integer = original - 1;
                    if ((tag != 5 || character(value.integer))
                        && accepts(first, second, argument, value))
                        return true;
                }
                if (original < INT64_MAX) {
                    value.integer = original + 1;
                    if ((tag != 5 || character(value.integer))
                        && accepts(first, second, argument, value))
                        return true;
                }
            }
        }
    }
    if (tag == 3) {
        value = (scalar){.tag = 3, .virtual_length = maximum_string + 1};
        return accepts(first, second, argument, value);
    }
    return false;
}

bool goat_module_guards_overlap(const yyjson_val *first, const yyjson_val *second) {
    const yyjson_val *a = yyjson_obj_get(first, "argumentTypes");
    const yyjson_val *b = yyjson_obj_get(second, "argumentTypes");
    if (yyjson_arr_size(a) != yyjson_arr_size(b))
        return false;
    for (size_t i = 0; i < yyjson_arr_size(a); i++) {
        uint32_t tag = goat_module_scalar_tag(yyjson_arr_get(a, i));
        if (tag == UINT32_MAX || tag != goat_module_scalar_tag(yyjson_arr_get(b, i))
            || !witness(first, second, i, tag))
            return false;
    }
    return true;
}

bool goat_module_guards_validate(const yyjson_val *specializations) {
    if (!yyjson_is_arr(specializations))
        return false;
    for (size_t i = 0; i < yyjson_arr_size(specializations); i++) {
        const yyjson_val *entry = yyjson_arr_get(specializations, i);
        const yyjson_val *signature = yyjson_obj_get(entry, "signature");
        const yyjson_val *arguments = yyjson_obj_get(signature, "arguments");
        const yyjson_val *guard = yyjson_obj_get(entry, "when");
        const yyjson_val *types = yyjson_obj_get(guard, "argumentTypes");
        if (!is(yyjson_obj_get(signature, "id"), "goat.adapter.v1")
            || goat_module_scalar_tag(yyjson_obj_get(signature, "result")) == UINT32_MAX
            || !yyjson_is_arr(arguments) || !yyjson_is_arr(types)
            || yyjson_arr_size(arguments) != yyjson_arr_size(types))
            return false;
        char symbol[40];
        snprintf(symbol,
                 sizeof(symbol),
                 "goat_fn_%" PRIu32 "_v1",
                 number(yyjson_obj_get(entry, "specializationId")));
        if (!is(yyjson_obj_get(entry, "symbol"), symbol))
            return false;
        for (size_t j = 0; j < yyjson_arr_size(arguments); j++) {
            uint32_t tag = goat_module_scalar_tag(yyjson_arr_get(arguments, j));
            if (tag == UINT32_MAX || tag != goat_module_scalar_tag(yyjson_arr_get(types, j)))
                return false;
        }
        const yyjson_val *conditions = yyjson_obj_get(guard, "conditions");
        for (size_t j = 0; j < yyjson_arr_size(conditions); j++) {
            const yyjson_val *predicate = yyjson_arr_get(conditions, j);
            uint32_t index = number(yyjson_obj_get(predicate, "argument"));
            scalar bound;
            if (index >= yyjson_arr_size(arguments)
                || !constant(yyjson_obj_get(predicate, "constant"), &bound)
                || bound.tag != goat_module_scalar_tag(yyjson_arr_get(arguments, index)))
                return false;
            const yyjson_val *op = yyjson_obj_get(predicate, "operation");
            if (!is(op, "equal") && !is(op, "notEqual")
                && (bound.tag != 1 && bound.tag != 2 && bound.tag != 5))
                return false;
        }
        for (size_t j = 0; j < i; j++) {
            const yyjson_val *other = yyjson_arr_get(specializations, j);
            if (number(yyjson_obj_get(entry, "specializationId"))
                == number(yyjson_obj_get(other, "specializationId")))
                return false;
            if (number(yyjson_obj_get(entry, "functionId"))
                    == number(yyjson_obj_get(other, "functionId"))
                && goat_module_guards_overlap(guard, yyjson_obj_get(other, "when")))
                return false;
        }
    }
    return true;
}

bool goat_module_guard_matches(const yyjson_val *specialization,
                               size_t argument_count,
                               const goat_module_value_v1 *arguments) {
    if (argument_count && !arguments)
        return false;
    const yyjson_val *guard = yyjson_obj_get(specialization, "when");
    const yyjson_val *types = yyjson_obj_get(guard, "argumentTypes");
    for (size_t i = 0; i < yyjson_arr_size(types); i++) {
        goat_module_value_v1 abi = {0};
        if (i < argument_count)
            abi = arguments[i];
        scalar value = {.tag = abi.tag};
        if (abi.reserved || value.tag != goat_module_scalar_tag(yyjson_arr_get(types, i)))
            return false;
        switch (value.tag) {
            case 0:
                if (abi.payload)
                    return false;
                break;
            case 1:
                memcpy(&value.integer, &abi.payload, sizeof(value.integer));
                break;
            case 2:
                memcpy(&value.real, &abi.payload, sizeof(value.real));
                break;
            case 3:
                if (!abi.payload || abi.payload > UINTPTR_MAX)
                    return false;
                value.view = (const goat_module_string_v1 *)(uintptr_t)abi.payload;
                if ((!value.view->data && value.view->length) || value.view->length > SIZE_MAX)
                    return false;
                break;
            case 4:
                if (abi.payload > 1)
                    return false;
                value.integer = (int64_t)abi.payload;
                break;
            case 5:
                if (abi.payload > 0x10ffff || !character((int64_t)abi.payload))
                    return false;
                value.integer = (int64_t)abi.payload;
                break;
            default:
                return false;
        }
        if (!accepts(guard, NULL, i, value))
            return false;
    }
    return true;
}
