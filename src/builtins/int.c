/** @file int.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Integer conversion with an optional, unconverted fallback value.
 */
#include "analysis/abstract_state.h"
#include "analysis/lattice.h"
#include "lib/allocate.h"
#include "model/thread.h"
#include "registry.h"

#include <math.h>

static bool ascii_space(wchar_t ch) {
    return ch == L' ' || (ch >= L'\t' && ch <= L'\r');
}

/** @brief Parses a whole signed decimal string, checking overflow before each digit. */
static int_value_t from_string(string_view_t text) {
    size_t begin = 0, end = text.length;
    while (begin < end && ascii_space(text.data[begin]))
        begin++;
    while (end > begin && ascii_space(text.data[end - 1]))
        end--;
    bool negative = begin < end && text.data[begin] == L'-';
    if (begin < end && (text.data[begin] == L'+' || negative))
        begin++;
    if (begin == end)
        return (int_value_t){false, 0};
    uint64_t limit = (uint64_t)INT64_MAX + negative, number = 0;
    for (size_t i = begin; i < end; i++) {
        wchar_t ch = text.data[i];
        if (ch < L'0' || ch > L'9')
            return (int_value_t){false, 0};
        unsigned digit = (unsigned)(ch - L'0');
        if (number > (limit - digit) / 10)
            return (int_value_t){false, 0};
        number = number * 10 + digit;
    }
    int64_t value = negative && number == (UINT64_C(1) << 63) ? INT64_MIN
                    : negative                                ? -(int64_t)number
                                                              : (int64_t)number;
    return (int_value_t){true, value};
}

/** @brief Excludes NaN, infinities and the rounded-up INT64_MAX boundary before casting. */
static int_value_t from_real(double value) {
    if (!isfinite(value) || value < -0x1p63 || value >= 0x1p63)
        return (int_value_t){false, 0};
    return (int_value_t){true, (int64_t)value};
}

static operation_result_t execute(object_t **args, uint16_t count, thread_t *thread) {
    int_value_t value = {false, 0};
    if (is_integer_object(args[0]))
        value = get_object_integer_value(args[0]);
    else if (args[0]->vtbl->type == TYPE_BOOLEAN)
        value = (int_value_t){true, get_object_boolean_value(args[0])};
    else if (args[0]->vtbl->type == TYPE_NUMBER)
        value = from_real(get_object_real_value(args[0]).value);
    else if (args[0]->vtbl->type == TYPE_STRING) {
        string_value_t text = convert_object_to_string(args[0]);
        value = from_string(VALUE_TO_VIEW(text));
        FREE_STRING(text);
    }
    if (value.has_value)
        return operation_success(create_integer_object(thread->process, value.value));
    object_t *fallback = count >= 2 ? args[1] : get_integer_zero();
    INCREF(fallback);
    return operation_success(fallback);
}

static const lattice_element_t *
interpret(abstract_state_t *state, const lattice_element_t *const *args, size_t count) {
    const lattice_element_t *fallback =
        count >= 2 ? args[1] : make_integer_constant_element(state->arena, 0);
    const lattice_element_t *arg = args[0];
    if (is_integer_lattice_element(arg))
        return arg;
    int_value_t value = {false, 0};
    switch (arg->type) {
        case LATTICE_TRUE:
        case LATTICE_FALSE:
            value = (int_value_t){true, arg->type == LATTICE_TRUE};
            break;
        case LATTICE_BOOLEAN:
            return make_integer_range_element(state->arena, 0, 1);
        case LATTICE_REAL_CONSTANT:
            value = from_real(((const real_constant_element_t *)arg)->value);
            break;
        case LATTICE_STRING_CONSTANT:
            value = from_string(((const string_constant_element_t *)arg)->value);
            break;
        case LATTICE_TOP:
        case LATTICE_NOT_NULL:
        case LATTICE_NUMERIC:
        case LATTICE_REAL:
        case LATTICE_STRING:
            return lattice_join(state->arena, make_integer_element(), fallback);
        case LATTICE_BOTTOM:
            return arg;
        default:
            return fallback;
    }
    return value.has_value ? make_integer_constant_element(state->arena, value.value) : fallback;
}

const builtin_function_t builtin_int = {.name = L"int",
                                        .min_args = 1,
                                        .effects = BUILTIN_EFFECT_NONE,
                                        .execute = execute,
                                        .interpret = interpret,
                                        .get_object = get_function_int};

object_t *get_function_int(void) {
    static builtin_function_object_t object;
    return get_builtin_function_object(&object, &builtin_int);
}
