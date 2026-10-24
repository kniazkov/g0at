/** @file module_abi_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Controlled module ABI fixtures; no untrusted native library is loaded.
 */
#include "lib/file_system.h"
#include "module_abi.h"
#include "module_guards.h"

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

typedef struct fixture_state {
    unsigned allocations, releases, bulk_releases, calls;
    uint32_t adapter_status;
    goat_module_value_v1 output;
} fixture_state;

static void *GOAT_MODULE_CALL allocate(void *state, uint64_t size) {
    fixture_state *s = state;
    assert(size <= SIZE_MAX);
    s->allocations++;
    void *p = malloc(size ? (size_t)size : 1);
    assert(p);
    return p;
}

static void GOAT_MODULE_CALL release(void *state, void *pointer) {
    ((fixture_state *)state)->releases++;
    free(pointer);
}

static void GOAT_MODULE_CALL release_all(void *state) {
    ((fixture_state *)state)->bulk_releases++;
}

static uint32_t GOAT_MODULE_CALL is_space(uint32_t value) {
    return value == 32;
}

static uint32_t GOAT_MODULE_CALL upper(uint32_t value) {
    return value == 'a' ? 'A' : value;
}

static uint32_t GOAT_MODULE_CALL lower(uint32_t value) {
    return value == 'A' ? 'a' : value;
}

static goat_module_services_v1 services =
    {sizeof(goat_module_services_v1), 0, allocate, release, release_all, is_space, upper, lower};

static uint32_t GOAT_MODULE_CALL adapter(uint32_t version,
                                         const goat_module_context_v1 *context,
                                         uint32_t count,
                                         const goat_module_value_v1 *arguments,
                                         goat_module_value_v1 *result) {
    assert(version == 1);
    assert(!result->tag && !result->reserved && !result->payload);
    assert(!count || arguments);
    fixture_state *state = context->state;
    state->calls++;
    *result = state->output;
    return state->adapter_status;
}

typedef struct readable_range {
    const void *base;
    size_t size;
} readable_range;

static bool readable(void *state, const void *pointer, size_t length) {
    readable_range *ranges = state;
    uintptr_t p = (uintptr_t)pointer;
    for (size_t i = 0; ranges[i].base; i++) {
        uintptr_t base = (uintptr_t)ranges[i].base;
        if (p >= base && p - base <= ranges[i].size && length <= ranges[i].size - (p - base))
            return true;
    }
    return false;
}

static void api_checks(void) {
    assert(sizeof(goat_module_value_v1) == 16 && _Alignof(goat_module_value_v1) == 8);
    assert(sizeof(goat_module_string_v1) == 16 && _Alignof(goat_module_string_v1) == 8);
    assert(offsetof(goat_module_value_v1, reserved) == 4);
    assert(offsetof(goat_module_value_v1, payload) == 8);
    assert(offsetof(goat_module_string_v1, length) == 8);
    assert(offsetof(goat_module_services_v1, allocate) == 8);
    assert(offsetof(goat_module_services_v1, to_lower) == 8 + 5 * sizeof(void (*)(void)));
    assert(sizeof(goat_module_context_v1) == 2 * sizeof(void *));
    yyjson_doc *doc = (yyjson_doc *)(uintptr_t)1;
    assert(goat_module_descriptor_read(NULL, 0, &doc) == GOAT_ABI_DESCRIPTOR && !doc);
    assert(goat_module_descriptor_read(NULL, 0, NULL) == GOAT_ABI_ARGUMENT);
    assert(goat_module_abi_validate(NULL) == GOAT_ABI_ARGUMENT);
    assert(!goat_module_value_valid(NULL, NULL, NULL));
    goat_module_value_v1 value = {0};
    assert(goat_module_value_valid(&value, NULL, NULL));
    value.payload = 1;
    assert(!goat_module_value_valid(&value, NULL, NULL));
    for (uint32_t tag = GOAT_MODULE_INTEGER; tag <= GOAT_MODULE_REAL; tag++) {
        value.tag = tag;
        value.payload = UINT64_MAX;
        assert(goat_module_value_valid(&value, NULL, NULL));
    }
    value = (goat_module_value_v1){GOAT_MODULE_BOOLEAN, 0, 1};
    assert(goat_module_value_valid(&value, NULL, NULL));
    value.payload = 2;
    assert(!goat_module_value_valid(&value, NULL, NULL));
    value = (goat_module_value_v1){GOAT_MODULE_CHARACTER, 0, 0x10ffff};
    assert(goat_module_value_valid(&value, NULL, NULL));
    const uint64_t bad_chars[] = {0xd800, 0xdfff, 0x110000, UINT64_C(0x100000041)};
    for (size_t i = 0; i < sizeof(bad_chars) / sizeof(*bad_chars); i++) {
        value.payload = bad_chars[i];
        assert(!goat_module_value_valid(&value, NULL, NULL));
    }
    value = (goat_module_value_v1){99, 0, 0};
    assert(!goat_module_value_valid(&value, NULL, NULL));
    value = (goat_module_value_v1){GOAT_MODULE_INTEGER, 1, 0};
    assert(!goat_module_value_valid(&value, NULL, NULL));

    uint32_t scalars[] = {'A', 0, 0x1f600};
    goat_module_string_v1 view = {.data = scalars, .length = 3};
    readable_range ranges[] = {{&view, sizeof(view)}, {scalars, sizeof(scalars)}, {NULL, 0}};
    value = (goat_module_value_v1){GOAT_MODULE_STRING, 0, (uint64_t)(uintptr_t)&view};
    assert(goat_module_value_valid(&value, readable, ranges));
    assert(!goat_module_value_valid(&value, NULL, NULL));
    view.length = 4;
    assert(!goat_module_value_valid(&value, readable, ranges));
    view.length = UINT64_MAX;
    assert(!goat_module_value_valid(&value, readable, ranges));
    view.length = 3;
    scalars[1] = 0xd800;
    assert(!goat_module_value_valid(&value, readable, ranges));
    scalars[1] = 0;
    view.data = NULL;
    assert(!goat_module_value_valid(&value, readable, ranges));
    view.length = 0;
    assert(goat_module_value_valid(&value, readable, ranges));
#if UINTPTR_MAX == UINT32_MAX
    view.reserved = 1;
    assert(!goat_module_value_valid(&value, readable, ranges));
    view.reserved = 0;
    value.payload |= UINT64_C(1) << 32;
    assert(!goat_module_value_valid(&value, readable, ranges));
#endif
    value.payload = 8;
    assert(!goat_module_value_valid(&value, readable, ranges));
    value.payload = (uint64_t)(uintptr_t)&view + 1;
    assert(!goat_module_value_valid(&value, readable, ranges));

    fixture_state state = {0};
    goat_module_context_v1 context = {&services, &state};
    assert(goat_module_context_valid(&context));
    assert(!goat_module_context_valid(NULL));
    goat_module_services_v1 invalid = services;
    goat_module_context_v1 invalid_context = {&invalid, &state};
    invalid.struct_size = GOAT_MODULE_SERVICES_SIZE - 1;
    assert(!goat_module_context_valid(&invalid_context));
    invalid = services;
    invalid.reserved = 1;
    assert(!goat_module_context_valid(&invalid_context));
    invalid = services;
    invalid.to_lower = NULL;
    assert(!goat_module_context_valid(&invalid_context));
    goat_module_value_v1 result, argument = {GOAT_MODULE_INTEGER, 0, 42};
    uint32_t native_status;
    state.output = argument;
    assert(goat_module_abi_invoke(adapter,
                                  GOAT_MODULE_INTEGER,
                                  &context,
                                  1,
                                  &argument,
                                  &result,
                                  NULL,
                                  NULL,
                                  &native_status)
           == GOAT_ABI_OK);
    assert(state.calls == 1 && result.payload == 42 && native_status == 0);
    for (uint32_t status = 1; status <= 6; status++) {
        state.adapter_status = status;
        unsigned before = state.calls;
        assert(goat_module_abi_invoke(adapter,
                                      GOAT_MODULE_INTEGER,
                                      &context,
                                      1,
                                      &argument,
                                      &result,
                                      NULL,
                                      NULL,
                                      &native_status)
               == GOAT_ABI_CALL_FAILED);
        assert(state.calls == before + 1 && native_status == status);
        assert(!result.tag && !result.reserved && !result.payload);
    }
    state.adapter_status = UINT32_MAX;
    assert(goat_module_abi_invoke(adapter,
                                  GOAT_MODULE_INTEGER,
                                  &context,
                                  0,
                                  NULL,
                                  &result,
                                  NULL,
                                  NULL,
                                  &native_status)
           == GOAT_ABI_CALL_FAILED);
    state.adapter_status = 0;
    state.output.reserved = 1;
    assert(goat_module_abi_invoke(adapter,
                                  GOAT_MODULE_INTEGER,
                                  &context,
                                  0,
                                  NULL,
                                  &result,
                                  NULL,
                                  NULL,
                                  &native_status)
           == GOAT_ABI_VALUE);
    state.output = (goat_module_value_v1){GOAT_MODULE_BOOLEAN, 0, 2};
    assert(goat_module_abi_invoke(adapter,
                                  GOAT_MODULE_BOOLEAN,
                                  &context,
                                  0,
                                  NULL,
                                  &result,
                                  NULL,
                                  NULL,
                                  &native_status)
           == GOAT_ABI_VALUE);
    state.output = (goat_module_value_v1){GOAT_MODULE_REAL, 0, 0};
    assert(goat_module_abi_invoke(adapter,
                                  GOAT_MODULE_INTEGER,
                                  &context,
                                  0,
                                  NULL,
                                  &result,
                                  NULL,
                                  NULL,
                                  &native_status)
           == GOAT_ABI_VALUE);
    unsigned calls = state.calls;
    argument.reserved = 1;
    assert(goat_module_abi_invoke(adapter,
                                  GOAT_MODULE_INTEGER,
                                  &context,
                                  1,
                                  &argument,
                                  &result,
                                  NULL,
                                  NULL,
                                  &native_status)
           == GOAT_ABI_VALUE);
    assert(state.calls == calls && native_status == UINT32_MAX);
    assert(goat_module_abi_invoke(adapter,
                                  GOAT_MODULE_INTEGER,
                                  &invalid_context,
                                  0,
                                  NULL,
                                  &result,
                                  NULL,
                                  NULL,
                                  &native_status)
           == GOAT_ABI_CONTEXT);
    assert(goat_module_abi_invoke(NULL,
                                  GOAT_MODULE_INTEGER,
                                  &context,
                                  0,
                                  NULL,
                                  &result,
                                  NULL,
                                  NULL,
                                  &native_status)
           == GOAT_ABI_ARGUMENT);
    assert(
        goat_module_abi_invoke(adapter, 99, &context, 0, NULL, &result, NULL, NULL, &native_status)
        == GOAT_ABI_ARGUMENT);
    assert(goat_module_abi_invoke(adapter,
                                  GOAT_MODULE_INTEGER,
                                  &context,
                                  1,
                                  NULL,
                                  &result,
                                  NULL,
                                  NULL,
                                  &native_status)
           == GOAT_ABI_ARGUMENT);
    assert(goat_module_abi_invoke(adapter,
                                  GOAT_MODULE_INTEGER,
                                  &context,
                                  0,
                                  NULL,
                                  NULL,
                                  NULL,
                                  NULL,
                                  &native_status)
           == GOAT_ABI_ARGUMENT);
#if SIZE_MAX == UINT32_MAX
    argument.reserved = 0;
    assert(goat_module_abi_invoke(adapter,
                                  GOAT_MODULE_INTEGER,
                                  &context,
                                  UINT32_MAX,
                                  &argument,
                                  &result,
                                  NULL,
                                  NULL,
                                  &native_status)
           == GOAT_ABI_ARGUMENT);
#endif
    assert(state.calls == calls);
    assert(!state.bulk_releases);
    puts("Module ABI layouts, values, services and one-shot invocation passed");
}

static void load_fixture(const char *path, size_t descriptor_size) {
#ifdef _WIN32
    HMODULE module = LoadLibraryA(path);
    assert(module);
    FARPROC function = GetProcAddress(module, "goat_fn_1_v1");
    FARPROC descriptor_address = GetProcAddress(module, "goat_module_descriptor_v1");
    const void *descriptor;
    assert(sizeof(descriptor) == sizeof(descriptor_address));
    memcpy(&descriptor, &descriptor_address, sizeof(descriptor));
#else
    void *module = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!module)
        fprintf(stderr, "%s\n", dlerror());
    assert(module);
    void *function = dlsym(module, "goat_fn_1_v1");
    const void *descriptor = dlsym(module, "goat_module_descriptor_v1");
#endif
    assert(function && descriptor);
    /* This is our own freshly compiled fixture. Its bounded array size is supplied
     * by the test producer, never inferred from potentially hostile memory. */
    yyjson_doc *doc = NULL;
    assert(goat_module_descriptor_read(descriptor, descriptor_size, &doc) == GOAT_ABI_OK);
    goat_module_adapter_v1 invoke;
    assert(sizeof(invoke) == sizeof(function));
    memcpy(&invoke, &function, sizeof(invoke));
    fixture_state state = {0};
    goat_module_context_v1 context = {&services, &state};
    goat_module_value_v1 arguments[] = {{GOAT_MODULE_INTEGER, 0, 19}, {GOAT_MODULE_INTEGER, 0, 23}};
    goat_module_value_v1 result;
    uint32_t status;
    yyjson_val *spec =
        yyjson_arr_get_first(yyjson_obj_get(yyjson_doc_get_root(doc), "specializations"));
    assert(goat_module_guard_matches(spec, 2, arguments));
    assert(goat_module_abi_invoke(invoke,
                                  GOAT_MODULE_INTEGER,
                                  &context,
                                  2,
                                  arguments,
                                  &result,
                                  NULL,
                                  NULL,
                                  &status)
           == GOAT_ABI_OK);
    assert(result.payload == 42 && status == 0);
    assert(state.allocations == 1 && state.releases == 1 && !state.bulk_releases);
    yyjson_doc_free(doc);
#ifdef _WIN32
    assert(FreeLibrary(module));
#else
    assert(dlclose(module) == 0);
#endif
    printf("Real module-v1 adapter returned 42 (%zu-bit process)\n", sizeof(void *) * 8);
}

int main(int argc, char **argv) {
    if (argc == 1) {
        api_checks();
        return 0;
    }
    if (!strcmp(argv[1], "load")) {
        assert(argc == 4);
        load_fixture(argv[2], (size_t)strtoull(argv[3], NULL, 10));
        return 0;
    }
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    const size_t capacity = 1024 * 1024;
    char *input = malloc(capacity);
    assert(input);
    size_t size = FREAD(input, 1, capacity, stdin);
    assert(size < capacity && !FERROR(stdin));
    yyjson_doc *doc = NULL;
    if (!strcmp(argv[1], "overlap")) {
        /* The Python producer supplies valid guard records; this mode isolates the
         * witness algorithm for independent differential testing. */
        doc = yyjson_read(input, size, YYJSON_READ_NUMBER_AS_RAW);
        assert(doc);
        yyjson_val *pairs = yyjson_doc_get_root(doc);
        for (size_t i = 0; i < yyjson_arr_size(pairs); i++) {
            yyjson_val *pair = yyjson_arr_get(pairs, i);
            printf("%d\n",
                   goat_module_guards_overlap(yyjson_arr_get(pair, 0), yyjson_arr_get(pair, 1)));
        }
        yyjson_doc_free(doc);
        free(input);
        return 0;
    }
    goat_module_abi_error status;
    if (!strcmp(argv[1], "metadata")) {
        status = goat_module_json_read(input, size, GOAT_MODULE_METADATA, &doc)
                     ? GOAT_ABI_DESCRIPTOR
                     : GOAT_ABI_OK;
    } else if (!strcmp(argv[1], "descriptor")) {
        status = goat_module_descriptor_read(input, size, &doc);
    } else {
        goat_module_json_error error = goat_module_json_read(input, size, GOAT_MODULE_NATIVE, &doc);
        status = error ? GOAT_ABI_DESCRIPTOR : goat_module_abi_validate(yyjson_doc_get_root(doc));
    }
    if (status || strcmp(argv[1], "match")) {
        printf("%d\n", status);
    } else {
        assert(argc == 5);
        uint32_t count = (uint32_t)strtoul(argv[2], NULL, 10);
        assert(count <= 2);
        goat_module_value_v1 arguments[2] = {{0}, {GOAT_MODULE_INTEGER, 0, 7}};
        arguments[0].tag = (uint32_t)strtoul(argv[3], NULL, 10);
        arguments[0].payload = strtoull(argv[4], NULL, 16);
        uint32_t scalars[] = {'A', 0, 0x1f600};
        goat_module_string_v1 string = {.data = scalars, .length = 3};
        if (arguments[0].tag == GOAT_MODULE_STRING)
            arguments[0].payload = (uint64_t)(uintptr_t)&string;
        yyjson_val *spec =
            yyjson_arr_get_first(yyjson_obj_get(yyjson_doc_get_root(doc), "specializations"));
        printf("%d\n", goat_module_guard_matches(spec, count, arguments));
    }
    yyjson_doc_free(doc);
    free(input);
    return 0;
}
