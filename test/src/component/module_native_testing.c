/** @file module_native_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent platform-selection fixtures; OS loading is test-only.
 */
/* Assertions perform fixture setup as well as validation. Keep both active
 * when exercising an optimized Release core. */
#ifdef NDEBUG
#    undef NDEBUG
#endif
#include "lib/file_system.h"
#include "lib/wire.h"
#include "module_native.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#    include <windows.h>
#else
#    include <dlfcn.h>
#endif

static void version(goat_native_version v) {
    printf("{\"known\":%s,\"components\":[%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 "]}",
           v.known ? "true" : "false",
           v.components[0],
           v.components[1],
           v.components[2],
           v.components[3]);
}

static void host(void) {
    goat_native_environment e;
    assert(goat_native_environment_detect(&e));
    assert(e.bits == sizeof(void *) * 8);
    printf("{\"os\":%d,\"architecture\":%d,\"bits\":%u,\"little\":%s,\"runtime\":%d,"
           "\"features\":%" PRIu64 ",\"osVersion\":",
           e.os,
           e.architecture,
           e.bits,
           e.little_endian ? "true" : "false",
           e.runtime,
           e.cpu_features);
    version(e.os_version);
    printf(",\"runtimeVersion\":");
    version(e.runtime_version);
    puts("}");
}

static void api_checks(void) {
    goat_native_environment e = {0};
    size_t selected = 7;
    goat_native_error error;
    assert(goat_module_native_select(NULL, &e, NULL, 0, &selected, &error) == GOAT_NATIVE_ARGUMENT);
    assert(selected == SIZE_MAX && error.status == GOAT_NATIVE_ARGUMENT);
    assert(goat_module_native_select(NULL, NULL, NULL, 0, NULL, NULL) == GOAT_NATIVE_ARGUMENT);
    memset(&e, 0xaa, sizeof(e));
    assert(goat_native_environment_read(NULL, 0, &e) != GOAT_MODULE_JSON_OK);
    for (size_t i = 0; i < sizeof(e); i++)
        assert(((unsigned char *)&e)[i] == 0);
    assert(goat_native_environment_read("{}", 2, NULL) != GOAT_MODULE_JSON_OK);
    assert(!goat_native_environment_detect(NULL));
    goat_native_compatibility_result result = {123, 456};
    assert(goat_module_native_compatibility(NULL, &e, NULL, 0, &result) == GOAT_NATIVE_ARGUMENT);
    assert(!result.reasons && !result.missing_cpu_features);
    assert(goat_module_native_compatibility(NULL, &e, NULL, 0, NULL) == GOAT_NATIVE_ARGUMENT);
    assert(!goat_native_cpu_feature_name(0));
    assert(!goat_native_cpu_feature_name(GOAT_CPU_SSE2 | GOAT_CPU_AVX));
    assert(!goat_native_cpu_feature_name(UINT64_C(1) << 63));
    assert(!strcmp(goat_native_cpu_feature_name(GOAT_CPU_AVX2), "avx2"));
    for (unsigned reason = 1; reason <= GOAT_NATIVE_REASON_ABI; reason <<= 1)
        assert(strcmp(
            goat_native_compatibility_reason_string((goat_native_compatibility_reason)reason),
            "unknown native compatibility reason"));
    puts("Native metadata API checks passed");
}

/* The caller supplies a freshly compiled trusted fixture. Never load bytes from
 * arbitrary ZIP fixtures. Verify path content against the selected snapshot. */
static int
load_fixture(const char *path, const goat_module_container *container, yyjson_val *variant) {
    yyjson_val *library = yyjson_obj_get(variant, "library");
    goat_container_native selected = {0};
    for (size_t i = 0; i < goat_module_container_native_count(container); i++) {
        goat_container_native candidate;
        assert(goat_module_container_native_at(container, i, &candidate));
        if (candidate.name_length == yyjson_get_len(library)
            && !memcmp(candidate.name, yyjson_get_str(library), candidate.name_length))
            selected = candidate;
    }
    assert(selected.bytes.data);
    FILE *file = fopen(path, "rb");
    assert(file);
    unsigned char *bytes = malloc(selected.bytes.size + 1);
    assert(bytes);
    size_t count = FREAD(bytes, 1, selected.bytes.size + 1, file);
    assert(!FERROR(file));
    assert(FCLOSE(file) == 0);
    assert(count == selected.bytes.size && !memcmp(bytes, selected.bytes.data, count));
    free(bytes);
    typedef int (*function_type)(int);
    function_type function = NULL;
#ifdef _WIN32
    HMODULE module = LoadLibraryA(path);
    assert(module);
    FARPROC address = GetProcAddress(module, "goat_metadata_fixture");
    assert(sizeof(function) == sizeof(address));
    memcpy(&function, &address, sizeof(function));
#else
    void *module = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    assert(module);
    void *address = dlsym(module, "goat_metadata_fixture");
    assert(sizeof(function) == sizeof(address));
    memcpy(&function, &address, sizeof(function));
#endif
    assert(function);
    int result = function(19);
    assert(result == 42);
#ifdef _WIN32
    assert(FreeLibrary(module));
#else
    assert(dlclose(module) == 0);
#endif
    return result;
}

int main(int argc, char **argv) {
    if (argc == 1) {
        api_checks();
        return 0;
    }
    if (!strcmp(argv[1], "host")) {
        host();
        return 0;
    }
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    const size_t capacity = 32 * 1024 * 1024;
    unsigned char *input = malloc(capacity);
    assert(input);
    size_t size = FREAD(input, 1, capacity, stdin);
    assert(size >= 8 && size < capacity && !FERROR(stdin));
    size_t profile_size = read_le32(input);
    assert(profile_size <= size - 8);
    goat_native_environment e;
    goat_module_json_error json_error =
        goat_native_environment_read((char *)input + 4, profile_size, &e);
    if (json_error) {
        printf("{\"profileError\":%d}\n", json_error);
        free(input);
        return 0;
    }
    if (!strcmp(argv[1], "unknown-os"))
        e.os = GOAT_OS_UNKNOWN;
    if (!strcmp(argv[1], "unknown-runtime"))
        e.runtime = GOAT_RUNTIME_UNKNOWN;
    if (!strcmp(argv[1], "unknown-os-version"))
        e.os_version.known = false;
    if (!strcmp(argv[1], "unknown-runtime-version"))
        e.runtime_version.known = false;
    if (!strcmp(argv[1], "unknown-arch"))
        e.architecture = GOAT_ARCH_UNKNOWN;
    if (!strcmp(argv[1], "big-endian"))
        e.little_endian = false;
    if (!strcmp(argv[1], "no-features"))
        e.cpu_features = 0;
    if (!strcmp(argv[1], "detect") || !strcmp(argv[1], "load"))
        assert(goat_native_environment_detect(&e));
    size_t abi_count = read_le32(input + 4 + profile_size);
    size_t offset = profile_size + 8;
    assert(abi_count <= (size - offset) / 4);
    uint32_t *abis = abi_count ? malloc(abi_count * sizeof(*abis)) : NULL;
    assert(!abi_count || abis);
    for (size_t i = 0; i < abi_count; i++, offset += 4)
        abis[i] = read_le32(input + offset);
    goat_module_container *container = NULL;
    goat_container_error error;
    goat_container_status cs =
        goat_module_container_read(input + offset, size - offset, SIZE_MAX, &container, &error);
    memset(input, 0xaa, size);
    free(input);
    if (cs) {
        assert(!container);
        printf("{\"containerError\":%d,\"jsonError\":%d}\n", cs, error.json_error);
        free(abis);
        return 0;
    }
    size_t selected = 123;
    goat_native_error native_error;
    assert(goat_module_native_select(container, &e, NULL, 1, &selected, &native_error)
           == GOAT_NATIVE_ARGUMENT);
    assert(selected == SIZE_MAX && native_error.variant_index == SIZE_MAX);
    assert(goat_module_native_select(container, NULL, NULL, 0, &selected, NULL)
           == GOAT_NATIVE_ARGUMENT);
    assert(goat_module_native_select(container, &e, NULL, 0, NULL, NULL) == GOAT_NATIVE_ARGUMENT);
    goat_native_status status =
        goat_module_native_select(container, &e, abis, abi_count, &selected, &native_error);
    assert(status == native_error.status);
    size_t repeated = 456;
    assert(goat_module_native_select(container, &e, abis, abi_count, &repeated, NULL) == status);
    assert(repeated == selected);
    yyjson_val *variants =
        yyjson_obj_get(yyjson_doc_get_root(goat_module_container_metadata(container)), "native");
    printf("{\"compatibility\":[");
    size_t matches = 0;
    for (size_t i = 0; i < yyjson_arr_size(variants); ++i) {
        goat_native_compatibility_result result = {0};
        goat_native_status match = goat_module_native_compatibility(yyjson_arr_get(variants, i),
                                                                    &e,
                                                                    abis,
                                                                    abi_count,
                                                                    &result);
        if (match == GOAT_NATIVE_OK) {
            ++matches;
            assert(!result.reasons && !result.missing_cpu_features);
        } else if (match == GOAT_NATIVE_NO_MATCH) {
            assert(result.reasons);
            assert(!!(result.reasons & GOAT_NATIVE_REASON_CPU) == !!result.missing_cpu_features);
        } else {
            assert(match == GOAT_NATIVE_INVALID_METADATA);
            assert(status == GOAT_NATIVE_INVALID_METADATA);
        }
        printf("%s{\"status\":%d,\"reasons\":%u,\"missingCpu\":%" PRIu64 "}",
               i ? "," : "",
               match,
               result.reasons,
               result.missing_cpu_features);
    }
    if (status != GOAT_NATIVE_INVALID_METADATA)
        assert(status
               == (matches == 0   ? GOAT_NATIVE_NO_MATCH
                   : matches == 1 ? GOAT_NATIVE_OK
                                  : GOAT_NATIVE_AMBIGUOUS));
    free(abis);
    printf("],\"status\":%d,\"selected\":", status);
    if (status) {
        assert(selected == SIZE_MAX);
        puts("null}");
    } else {
        yyjson_val *root = yyjson_doc_get_root(goat_module_container_metadata(container));
        yyjson_val *variant = yyjson_arr_get(yyjson_obj_get(root, "native"), selected);
        assert(variant);
        printf("%zu,\"library\":\"%s\",\"mainHash\":\"%s\"",
               selected,
               yyjson_get_str(yyjson_obj_get(variant, "library")),
               yyjson_get_str(yyjson_obj_get(root, "mainHash")));
        if (!strcmp(argv[1], "load")) {
            assert(argc == 3);
            printf(",\"nativeResult\":%d", load_fixture(argv[2], container, variant));
        }
        puts("}");
    }
    goat_module_container_free(container);
    return 0;
}
