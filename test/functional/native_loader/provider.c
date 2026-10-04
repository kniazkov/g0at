/** @file provider.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Valid and malformed trusted fixtures for native metadata validation.
 */
#include <goat/native_abi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <windows.h>
#endif

static const char *environment(const char *name, char *buffer, size_t size) {
#ifdef _WIN32
    DWORD n = GetEnvironmentVariableA(name, buffer, (DWORD)size);
    return n && n < size ? buffer : "";
#else
    (void)buffer;
    (void)size;
    const char *value = getenv(name);
    return value ? value : "";
#endif
}

__attribute__((destructor)) static void unloaded(void) {
    char buffer[4096];
    const char *path = environment("GOAT_TEST_UNLOAD", buffer, sizeof(buffer));
    if (path[0]) {
        FILE *file = fopen(path, "ab");
        if (file) {
            fputc('x', file);
            fclose(file);
        }
    }
}

#ifndef NO_QUERY
static uint32_t GOAT_NATIVE_CALL identity(uint32_t version,
                                          uint32_t count,
                                          const goat_native_value_v1_t *args,
                                          goat_native_value_v1_t *result) {
    if (version != 1)
        return GOAT_NATIVE_ABI_MISMATCH;
    if (!count || !args || !result)
        return GOAT_NATIVE_BAD_REQUEST;
    *result = args[0];
    return GOAT_NATIVE_OK;
}

static uint32_t integer_type[] = {GOAT_NATIVE_I64};
static uint32_t real_type[] = {GOAT_NATIVE_F64};
static goat_native_entry_v1_t entries[2];
static goat_native_module_v1_t module;

GOAT_NATIVE_API const goat_native_module_v1_t *GOAT_NATIVE_CALL
goat_native_query_v1(uint32_t version) {
#    ifdef NEED_DEPENDENCY
    extern int provider_dependency(void);
    if (provider_dependency() != 42)
        return NULL;
#    endif
    if (version != 1)
        return NULL;
    integer_type[0] = GOAT_NATIVE_I64;
    real_type[0] = GOAT_NATIVE_F64;
    entries[0] = (goat_native_entry_v1_t){3,
                                          7,
                                          1,
                                          GOAT_NATIVE_I64,
                                          integer_type,
                                          identity,
                                          "fixture.identity",
                                          GOAT_NATIVE_PURE};
    entries[1] = (goat_native_entry_v1_t){9,
                                          7,
                                          1,
                                          GOAT_NATIVE_F64,
                                          real_type,
                                          identity,
                                          "fixture.identity",
                                          GOAT_NATIVE_PURE};
    module = (goat_native_module_v1_t){1,
                                       sizeof(module),
                                       sizeof(goat_native_value_v1_t),
                                       _Alignof(goat_native_value_v1_t),
                                       sizeof(goat_native_entry_v1_t),
                                       sizeof(void *),
                                       2,
                                       entries};
    char buffer[128];
    int which = atoi(environment("GOAT_TEST_METADATA", buffer, sizeof(buffer)));
    switch (which) {
        case 1:
            return NULL;
        case 2:
            module.abi_version++;
            break;
        case 3:
            module.struct_size = 8;
            break;
        case 4:
            module.value_size++;
            break;
        case 5:
            module.value_alignment++;
            break;
        case 6:
            module.entry_size++;
            break;
        case 7:
            module.pointer_size++;
            break;
        case 8:
            module.entries = NULL;
            break;
        case 9:
            module.entry_count = 0;
            break;
        case 10:
            module.entry_count = 65537;
            break;
        case 11:
            entries[1].invoke = NULL;
            break;
        case 12:
            entries[1].return_type = 123;
            break;
        case 13:
            entries[1].parameter_types = NULL;
            break;
        case 14:
            entries[1].parameter_count = 0;
            break;
        case 15:
            real_type[0] = 123;
            break;
        case 16:
            entries[1].parameter_count = 65536;
            break;
        case 17:
            entries[1].specialization_id = 3;
            break;
        case 18:
            real_type[0] = GOAT_NATIVE_I64;
            break;
        case 19:
            entries[1].binding_name = "other";
            break;
        case 20:
            entries[1].binding_name = "\xc0\x80";
            break;
        case 21:
            entries[1].binding_name = "";
            break;
        case 22:
            entries[1].flags = 2;
            break;
        case 23:
            entries[1].function_id = 8;
            break;
        case 24:
            entries[1].parameter_types = (const uint32_t *)(uintptr_t)1;
            break;
        case 25:
            module.entries = (const goat_native_entry_v1_t *)(uintptr_t)1;
            break;
        case 26:
            return (const goat_native_module_v1_t *)(uintptr_t)1;
        case 27: {
            static char name[4098];
            memset(name, 'x', sizeof(name) - 1);
            entries[1].binding_name = name;
            break;
        }
        case 28:
            entries[1].binding_name = "\xed\xa0\x80";
            break;
        case 29:
            entries[1].binding_name = "\xf4\x90\x80\x80";
            break;
        case 30:
            entries[1].binding_name = "\xe2\x82";
            break;
        case 31:
            entries[1].binding_name = "\xe2!a";
            break;
        case 32:
            entries[1].binding_name = "\x80";
            break;
        case 33: {
            static uint32_t types[UINT16_MAX];
            static goat_native_entry_v1_t large[257];
            for (unsigned i = 0; i < UINT16_MAX; i++)
                types[i] = GOAT_NATIVE_I64;
            for (unsigned i = 0; i < 257; i++) {
                large[i] = entries[0];
                large[i].specialization_id = i;
                large[i].function_id = i;
                large[i].binding_name = NULL;
                large[i].parameter_count = UINT16_MAX;
                large[i].parameter_types = types;
            }
            module.entry_count = 257;
            module.entries = large;
            break;
        }
        case 40:
            module.entry_count = 0;
            module.entries = NULL;
            break;
        case 41:
            entries[0].binding_name = entries[1].binding_name = "fixture.\xc3\xa9";
            entries[0].function_id = entries[1].function_id = 8;
            integer_type[0] = GOAT_NATIVE_F64;
            real_type[0] = GOAT_NATIVE_I64;
            entries[0].flags = entries[1].flags = 0;
            break;
    }
    return &module;
}
#endif
