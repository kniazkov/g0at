/** @file driver.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Numeric edge cases across the real shared-library ABI.
 */
#include <assert.h>
#include <float.h>
#include <goat/native_abi.h>
#include <math.h>

static const goat_native_module_v1_t *module;

static goat_native_value_v1_t integer(int64_t n) {
    return (goat_native_value_v1_t){.type = GOAT_NATIVE_I64, .value.integer = n};
}

static goat_native_value_v1_t real(double n) {
    return (goat_native_value_v1_t){.type = GOAT_NATIVE_F64, .value.real = n};
}

static goat_native_value_v1_t
call(uint64_t function, goat_native_value_v1_t a, goat_native_value_v1_t b) {
    for (uint32_t i = 0; i < module->entry_count; i++) {
        const goat_native_entry_v1_t *entry = &module->entries[i];
        if (entry->function_id != function || entry->parameter_types[0] != a.type
            || (entry->parameter_count == 2 && entry->parameter_types[1] != b.type))
            continue;
        goat_native_value_v1_t args[] = {a, b}, result = {0};
        assert(entry->invoke(1, entry->parameter_count, args, &result) == GOAT_NATIVE_OK);
        assert(result.type == entry->return_type && !result.reserved);
        return result;
    }
    assert(0);
    return integer(0);
}

int main(void) {
    module = goat_native_query_v1(GOAT_NATIVE_ABI_VERSION);
    assert(module && module->entry_count == 19);
    assert(call(1, integer(INT64_MAX), integer(1)).value.integer == INT64_MAX);
    assert(call(2, integer(INT64_MIN), integer(1)).value.integer == INT64_MIN);
    assert(call(3, integer(INT64_MAX), integer(2)).value.integer == INT64_MAX);
    assert(call(6, integer(INT64_MIN), integer(0)).value.integer == INT64_MAX);
    for (unsigned f = 1; f <= 3; f++) {
        double expected = f == 1 ? 2.5 : f == 2 ? 1.5 : 1.0;
        assert(call(f, integer(2), real(0.5)).value.real == expected);
        assert(call(f, real(2), integer(1)).value.real == (f == 1 ? 3 : f == 2 ? 1 : 2));
        assert(isnan(call(f, real(NAN), real(1)).value.real));
    }
    assert(call(4, integer(INT64_C(9007199254740993)), real(0x1p53)).value.integer == 0);
    assert(call(4, real(0x1p53), integer(INT64_C(9007199254740993))).value.integer == 1);
    assert(call(4, integer(INT64_MAX), real(0x1p63)).value.integer == 1);
    assert(call(4, real(-0x1p63), integer(INT64_MIN)).value.integer == 0);
    assert(call(4, integer(INT64_MIN), real(-INFINITY)).value.integer == 0);
    assert(call(4, real(NAN), integer(0)).value.integer == 0);
    assert(call(4, integer(0), real(NAN)).value.integer == 0);
    double z = call(1, real(-0.0), real(-0.0)).value.real;
    assert(z == 0 && signbit(z));
    z = call(2, real(-0.0), real(0.0)).value.real;
    assert(z == 0 && signbit(z));
    z = call(3, real(-0.0), real(2.0)).value.real;
    assert(z == 0 && signbit(z));
    assert(isinf(call(3, real(DBL_MAX), real(2.0)).value.real));
    assert(isnan(call(1, real(INFINITY), real(-INFINITY)).value.real));
    assert(call(3, real(DBL_MIN), real(0.5)).value.real == 0x1p-1023);
    assert(call(1, real(0x1p-1074), real(0x1p-1074)).value.real == 0x1p-1073);
    assert(call(5, real(0x1p53), integer(0)).value.real == 0);
    assert(call(1, integer(INT64_C(9007199254740993)), real(-0x1p53)).value.real == 0);
    assert(signbit(call(6, real(0.0), integer(0)).value.real));
    return 0;
}
