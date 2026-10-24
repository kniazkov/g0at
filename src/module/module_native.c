/** @file module_native.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Deterministic native variant selection using verified metadata only.
 */
#include "module_native.h"

#include "module_abi.h"
#include "module_json_internal.h"

#include <string.h>

typedef struct {
    const char *name;
    uint64_t bit;
} feature;

static const feature features[] = {
    {"i686", GOAT_CPU_I686},   {"sse2", GOAT_CPU_SSE2},     {"sse3", GOAT_CPU_SSE3},
    {"ssse3", GOAT_CPU_SSSE3}, {"sse4_1", GOAT_CPU_SSE4_1}, {"sse4_2", GOAT_CPU_SSE4_2},
    {"avx", GOAT_CPU_AVX},     {"avx2", GOAT_CPU_AVX2},     {"fma", GOAT_CPU_FMA},
    {"bmi1", GOAT_CPU_BMI1},   {"bmi2", GOAT_CPU_BMI2},     {"avx512f", GOAT_CPU_AVX512F},
    {"fp", GOAT_CPU_FP},       {"neon", GOAT_CPU_NEON},     {"aes", GOAT_CPU_AES},
    {"sha1", GOAT_CPU_SHA1},   {"sha2", GOAT_CPU_SHA2},     {"crc32", GOAT_CPU_CRC32},
    {"lse", GOAT_CPU_LSE},     {"sve", GOAT_CPU_SVE},       {"sve2", GOAT_CPU_SVE2}};

static bool is(const yyjson_val *v, const char *text) {
    return yyjson_is_str(v) && yyjson_get_len(v) == strlen(text)
           && !memcmp(yyjson_get_str(v), text, strlen(text));
}

static uint32_t number(const yyjson_val *v) {
    int64_t n = 0;
    goat_json_integer(v, &n); /* Validated uint32, including RAW decimal values. */
    return (uint32_t)n;
}

static goat_native_version version(yyjson_val *v) {
    goat_native_version result = {.known = true};
    for (size_t i = 0; i < yyjson_arr_size(v); i++)
        result.components[i] = number(yyjson_arr_get(v, i));
    return result;
}

static bool version_at_least(goat_native_version actual,
                             goat_native_version required,
                             bool zero_is_unrestricted) {
    if (zero_is_unrestricted
        && !(required.components[0] | required.components[1] | required.components[2]
             | required.components[3]))
        return true; /* [0] explicitly means no additional runtime version floor. */
    if (!actual.known)
        return false;
    for (size_t i = 0; i < 4; i++)
        if (actual.components[i] != required.components[i])
            return actual.components[i] > required.components[i];
    return true;
}

/* A bit known for the other architecture is contradictory metadata, unlike a
 * future nonempty name, which is structurally valid but never satisfied here. */
static bool target(yyjson_val *v, goat_native_environment *out, bool *unknown) {
    memset(out, 0, sizeof(*out));
    *unknown = false;
    out->os = is(yyjson_obj_get(v, "os"), "linux") ? GOAT_OS_LINUX : GOAT_OS_WINDOWS;
    yyjson_val *arch = yyjson_obj_get(v, "architecture");
    out->architecture = is(arch, "x86")      ? GOAT_ARCH_X86
                        : is(arch, "x86_64") ? GOAT_ARCH_X86_64
                                             : GOAT_ARCH_AARCH64;
    out->bits = number(yyjson_obj_get(v, "bits"));
    out->little_endian = true;
    out->os_version = version(yyjson_obj_get(v, "minimumOs"));
    yyjson_val *runtime = yyjson_obj_get(v, "runtime"), *family = yyjson_obj_get(runtime, "family");
    out->runtime = is(family, "glibc")    ? GOAT_RUNTIME_GLIBC
                   : is(family, "musl")   ? GOAT_RUNTIME_MUSL
                   : is(family, "msvcrt") ? GOAT_RUNTIME_MSVCRT
                                          : GOAT_RUNTIME_UCRT;
    out->runtime_version = version(yyjson_obj_get(runtime, "minimumVersion"));
    yyjson_val *names = yyjson_obj_get(v, "cpuFeatures");
    for (size_t i = 0; i < yyjson_arr_size(names); i++) {
        yyjson_val *name = yyjson_arr_get(names, i);
        bool found = false;
        for (size_t j = 0; j < sizeof(features) / sizeof(*features); j++)
            if (is(name, features[j].name)) {
                out->cpu_features |= features[j].bit;
                found = true;
                break;
            }
        *unknown |= !found;
    }
    uint64_t x86 = GOAT_CPU_I686 | GOAT_CPU_SSE2 | GOAT_CPU_SSE3 | GOAT_CPU_SSSE3 | GOAT_CPU_SSE4_1
                   | GOAT_CPU_SSE4_2 | GOAT_CPU_AVX | GOAT_CPU_AVX2 | GOAT_CPU_FMA | GOAT_CPU_BMI1
                   | GOAT_CPU_BMI2 | GOAT_CPU_AVX512F;
    uint64_t arm =
        GOAT_CPU_FP | GOAT_CPU_NEON | GOAT_CPU_CRC32 | GOAT_CPU_LSE | GOAT_CPU_SVE | GOAT_CPU_SVE2;
    return !(out->cpu_features & (out->architecture == GOAT_ARCH_AARCH64 ? x86 : arm));
}

static uint64_t requirements(goat_native_environment required) {
    uint64_t bits = required.cpu_features;
    if (required.architecture == GOAT_ARCH_AARCH64) {
        bits |= GOAT_CPU_FP | GOAT_CPU_NEON;
        if (bits & GOAT_CPU_SVE2)
            bits |= GOAT_CPU_SVE;
    } else {
        bits |= required.architecture == GOAT_ARCH_X86 ? GOAT_CPU_I686 : GOAT_CPU_SSE2;
        if (bits & (GOAT_CPU_AVX2 | GOAT_CPU_FMA | GOAT_CPU_AVX512F))
            bits |= GOAT_CPU_AVX;
        if (bits & GOAT_CPU_SSE4_2)
            bits |= GOAT_CPU_SSE4_1;
        if (bits & GOAT_CPU_SSE4_1)
            bits |= GOAT_CPU_SSSE3;
        if (bits & GOAT_CPU_SSSE3)
            bits |= GOAT_CPU_SSE3;
        if (bits & (GOAT_CPU_SSE3 | GOAT_CPU_AVX | GOAT_CPU_AES | GOAT_CPU_SHA1 | GOAT_CPU_SHA2))
            bits |= GOAT_CPU_SSE2;
    }
    return bits;
}

goat_module_json_error
goat_native_environment_read(const char *data, size_t size, goat_native_environment *out) {
    if (!out)
        return GOAT_MODULE_JSON_INVALID_FIELD;
    memset(out, 0, sizeof(*out));
    yyjson_doc *doc = NULL;
    goat_module_json_error error = goat_module_json_read(data, size, GOAT_MODULE_TARGET, &doc);
    if (error)
        return error;
    goat_native_environment result;
    bool unknown;
    if (target(yyjson_doc_get_root(doc), &result, &unknown))
        *out = result;
    else
        error = GOAT_MODULE_JSON_INVALID_FIELD;
    yyjson_doc_free(doc);
    return error;
}

static goat_native_status
finish(goat_native_error *error, goat_native_status status, size_t index) {
    if (error)
        *error = (goat_native_error){status, index};
    return status;
}

static bool variant_valid(yyjson_val *variant) {
    goat_native_environment required;
    bool unknown;
    if (!target(yyjson_obj_get(variant, "target"), &required, &unknown))
        return false;
    yyjson_val *library = yyjson_obj_get(variant, "library");
    const char *suffix = required.os == GOAT_OS_WINDOWS ? ".dll" : ".so";
    size_t length = yyjson_get_len(library), n = strlen(suffix);
    if (length < n || memcmp(yyjson_get_str(library) + length - n, suffix, n))
        return false;
    goat_module_abi_error abi = goat_module_abi_validate(variant);
    return abi == GOAT_ABI_OK || abi == GOAT_ABI_UNSUPPORTED;
}

goat_native_status goat_module_native_validate_diagnostics(const goat_module_container *container,
                                                           goat_native_error *error,
                                                           goat_native_diagnostic callback,
                                                           void *context) {
    if (!container) {
        if (callback)
            callback(GOAT_NATIVE_ARGUMENT, SIZE_MAX, context);
        return finish(error, GOAT_NATIVE_ARGUMENT, SIZE_MAX);
    }
    yyjson_val *variants =
        yyjson_obj_get(yyjson_doc_get_root(goat_module_container_metadata(container)), "native");
    size_t first = SIZE_MAX;
    for (size_t i = 0; i < yyjson_arr_size(variants); i++) {
        if (!variant_valid(yyjson_arr_get(variants, i))) {
            if (first == SIZE_MAX)
                first = i;
            if (callback)
                callback(GOAT_NATIVE_INVALID_METADATA, i, context);
        }
    }
    return finish(error, first == SIZE_MAX ? GOAT_NATIVE_OK : GOAT_NATIVE_INVALID_METADATA, first);
}

goat_native_status goat_module_native_validate(const goat_module_container *container,
                                               goat_native_error *error) {
    return goat_module_native_validate_diagnostics(container, error, NULL, NULL);
}

goat_native_status goat_module_native_select(const goat_module_container *container,
                                             const goat_native_environment *environment,
                                             const uint32_t *supported_abis,
                                             size_t abi_count,
                                             size_t *selected_index,
                                             goat_native_error *error) {
    if (selected_index)
        *selected_index = SIZE_MAX;
    if (!container || !environment || !selected_index || (!supported_abis && abi_count))
        return finish(error, GOAT_NATIVE_ARGUMENT, SIZE_MAX);
    goat_native_status validation = goat_module_native_validate(container, error);
    if (validation != GOAT_NATIVE_OK)
        return validation;
    yyjson_val *variants =
        yyjson_obj_get(yyjson_doc_get_root(goat_module_container_metadata(container)), "native");
    size_t selected = SIZE_MAX, ambiguous = SIZE_MAX;
    for (size_t i = 0; i < yyjson_arr_size(variants); i++) {
        yyjson_val *v = yyjson_arr_get(variants, i);
        goat_native_environment required;
        bool unknown;
        if (!target(yyjson_obj_get(v, "target"), &required, &unknown))
            return finish(error, GOAT_NATIVE_INVALID_METADATA, i);
        yyjson_val *library = yyjson_obj_get(v, "library");
        const char *suffix = required.os == GOAT_OS_WINDOWS ? ".dll" : ".so";
        size_t length = yyjson_get_len(library), n = strlen(suffix);
        if (length < n || memcmp(yyjson_get_str(library) + length - n, suffix, n))
            return finish(error, GOAT_NATIVE_INVALID_METADATA, i);
        uint32_t abi = number(yyjson_obj_get(v, "goatAbiVersion"));
        bool abi_supported = false;
        for (size_t j = 0; j < abi_count; j++)
            abi_supported |= supported_abis[j] == abi;
        uint64_t needed = requirements(required);
        if (unknown || !abi_supported || environment->os != required.os
            || environment->architecture != required.architecture
            || environment->bits != required.bits || !environment->little_endian
            || environment->runtime != required.runtime
            || !version_at_least(environment->os_version, required.os_version, false)
            || !version_at_least(environment->runtime_version, required.runtime_version, true)
            || (environment->cpu_features & needed) != needed)
            continue;
        if (selected != SIZE_MAX)
            ambiguous = i;
        else
            selected = i;
    }
    /* Continue through every record before reporting ambiguity: a later malformed
     * foreign variant must not disappear behind an early selection result. */
    if (ambiguous != SIZE_MAX)
        return finish(error, GOAT_NATIVE_AMBIGUOUS, ambiguous);
    if (selected == SIZE_MAX)
        return finish(error, GOAT_NATIVE_NO_MATCH, SIZE_MAX);
    *selected_index = selected;
    return finish(error, GOAT_NATIVE_OK, selected);
}

const char *goat_native_status_string(goat_native_status status) {
    switch (status) {
        case GOAT_NATIVE_OK:
            return "ok";
        case GOAT_NATIVE_ARGUMENT:
            return "invalid native selection argument";
        case GOAT_NATIVE_INVALID_METADATA:
            return "invalid native platform metadata";
        case GOAT_NATIVE_NO_MATCH:
            return "no compatible native variant";
        case GOAT_NATIVE_AMBIGUOUS:
            return "ambiguous compatible native variants";
    }
    return "unknown native selection error";
}
