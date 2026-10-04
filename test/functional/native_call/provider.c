/** @file provider.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Observable nonrecursive adapters for CALL integration tests.
 */
#include <goat/native_abi.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#    include <windows.h>
#endif

static int64_t calls;

static uint32_t GOAT_NATIVE_CALL identity(uint32_t version,
                                          uint32_t count,
                                          const goat_native_value_v1_t *args,
                                          goat_native_value_v1_t *result) {
    calls++;
    if (version != 1 || count != 1 || args[0].reserved)
        return GOAT_NATIVE_BAD_REQUEST;
    *result = args[0];
    return GOAT_NATIVE_OK;
}

static uint32_t GOAT_NATIVE_CALL difference(uint32_t version,
                                            uint32_t count,
                                            const goat_native_value_v1_t *args,
                                            goat_native_value_v1_t *result) {
    calls++;
    if (version != 1 || count != 2 || args[0].reserved || args[1].reserved)
        return GOAT_NATIVE_BAD_REQUEST;
    double a = args[0].type == GOAT_NATIVE_I64 ? (double)args[0].value.integer : args[0].value.real;
    double b = args[1].type == GOAT_NATIVE_I64 ? (double)args[1].value.integer : args[1].value.real;
    *result = (goat_native_value_v1_t){.type = GOAT_NATIVE_F64, .value.real = a - b};
    return GOAT_NATIVE_OK;
}

static uint32_t GOAT_NATIVE_CALL constant(uint32_t version,
                                          uint32_t count,
                                          const goat_native_value_v1_t *args,
                                          goat_native_value_v1_t *result) {
    calls++;
    if (version != 1 || count || args)
        return GOAT_NATIVE_BAD_REQUEST;
    *result = (goat_native_value_v1_t){.type = GOAT_NATIVE_I64, .value.integer = 42};
    return GOAT_NATIVE_OK;
}

static uint32_t GOAT_NATIVE_CALL failure(uint32_t version,
                                         uint32_t count,
                                         const goat_native_value_v1_t *args,
                                         goat_native_value_v1_t *result) {
    calls++;
    if (version != 1 || count != 1)
        return GOAT_NATIVE_BAD_REQUEST;
    int64_t mode = args[0].value.integer;
    if (mode < 6)
        return (uint32_t)mode;
    *result = (goat_native_value_v1_t){.type = GOAT_NATIVE_I64, .value.integer = 999};
    if (mode == 6)
        result->reserved = 1;
    if (mode == 7)
        result->type = GOAT_NATIVE_F64;
    return mode == 8 ? 99 : GOAT_NATIVE_OK;
}

static uint32_t GOAT_NATIVE_CALL counter(uint32_t version,
                                         uint32_t count,
                                         const goat_native_value_v1_t *args,
                                         goat_native_value_v1_t *result) {
    (void)version;
    (void)count;
    (void)args;
    *result = (goat_native_value_v1_t){.type = GOAT_NATIVE_I64, .value.integer = calls};
    return GOAT_NATIVE_OK;
}

static uint32_t GOAT_NATIVE_CALL limited(uint32_t version,
                                         uint32_t count,
                                         const goat_native_value_v1_t *args,
                                         goat_native_value_v1_t *result) {
    (void)version;
    (void)count;
    (void)args;
    (void)result;
    return GOAT_NATIVE_RESOURCE_LIMIT;
}

static const uint32_t integer[] = {GOAT_NATIVE_I64};
static const uint32_t real[] = {GOAT_NATIVE_F64};
static const uint32_t ir[] = {GOAT_NATIVE_I64, GOAT_NATIVE_F64};
static const uint32_t ri[] = {GOAT_NATIVE_F64, GOAT_NATIVE_I64};
static const goat_native_entry_v1_t entries[] = {
    {0, 1, 1, GOAT_NATIVE_I64, integer, identity, NULL, GOAT_NATIVE_PURE},
    {1, 1, 1, GOAT_NATIVE_F64, real, identity, NULL, GOAT_NATIVE_PURE},
    {2, 2, 2, GOAT_NATIVE_F64, ir, difference, NULL, GOAT_NATIVE_PURE},
    {3, 2, 2, GOAT_NATIVE_F64, ri, difference, NULL, GOAT_NATIVE_PURE},
    {4, 3, 0, GOAT_NATIVE_I64, NULL, constant, NULL, GOAT_NATIVE_PURE},
    {5, 4, 1, GOAT_NATIVE_I64, integer, failure, NULL, 0},
    {6, 5, 1, GOAT_NATIVE_I64, integer, identity, NULL, GOAT_NATIVE_PURE},
    {7, 90, 0, GOAT_NATIVE_I64, NULL, counter, NULL, 0},
    {8, 6, 1, GOAT_NATIVE_I64, integer, limited, NULL, GOAT_NATIVE_PURE}};
static const goat_native_module_v1_t module = {GOAT_NATIVE_ABI_VERSION,
                                               sizeof(goat_native_module_v1_t),
                                               sizeof(goat_native_value_v1_t),
                                               _Alignof(goat_native_value_v1_t),
                                               sizeof(goat_native_entry_v1_t),
                                               sizeof(void *),
                                               sizeof(entries) / sizeof(*entries),
                                               entries};

GOAT_NATIVE_API const goat_native_module_v1_t *GOAT_NATIVE_CALL
goat_native_query_v1(uint32_t version) {
    return version == 1 ? &module : NULL;
}

__attribute__((destructor)) static void unloaded(void) {
#ifdef _WIN32
    char path[4096];
    if (!GetEnvironmentVariableA("GOAT_CALL_UNLOAD", path, sizeof(path)))
        return;
#else
    const char *path = getenv("GOAT_CALL_UNLOAD");
    if (!path)
        return;
#endif
    FILE *file = fopen(path, "ab");
    if (file) {
        fputc('x', file);
        fclose(file);
    }
}
