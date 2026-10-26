/** @file module_native_values_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Same-bitness test-only loading of freshly built module-v1 adapters.
 */
#include "goat/module_abi.h"
#include "lib/allocate.h"
#include "lib/binary_file.h"
#include "lib/file_system.h"
#include "module/module_abi.h"
#include "module/module_json.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <windows.h>
#else
#    include <dlfcn.h>
#endif

typedef struct block_t {
    struct block_t *next;
    void *pointer;
} block_t;

static block_t *blocks;
static unsigned space_calls;

static void *GOAT_MODULE_CALL allocate(void *state, uint64_t size) {
    (void)state;
    assert(size <= SIZE_MAX);
    block_t *block = malloc(sizeof(*block));
    assert(block);
    block->pointer = calloc(1, size ? (size_t)size : 1);
    assert(block->pointer);
    block->next = blocks;
    blocks = block;
    return block->pointer;
}

static void GOAT_MODULE_CALL release(void *state, void *pointer) {
    (void)state;
    if (!pointer)
        return;
    block_t **cursor = &blocks;
    while (*cursor && (*cursor)->pointer != pointer)
        cursor = &(*cursor)->next;
    assert(*cursor);
    block_t *block = *cursor;
    *cursor = block->next;
    free(block->pointer);
    free(block);
}

static void GOAT_MODULE_CALL release_all(void *state) {
    (void)state;
    /* Only this test owner, not generated code, may bulk-release the context. */
    assert(0);
}

static uint32_t GOAT_MODULE_CALL space(uint32_t value) {
    ++space_calls;
    return value == 32 || value == 0x2003;
}

static uint32_t GOAT_MODULE_CALL upper(uint32_t value) {
    return value == 'a' ? 'A' : value;
}

static uint32_t GOAT_MODULE_CALL lower(uint32_t value) {
    return value == 'A' ? 'a' : value;
}

static goat_module_services_v1 services =
    {sizeof(goat_module_services_v1), 0, allocate, release, release_all, space, upper, lower};
static goat_module_context_v1 context = {&services, NULL};
static unsigned calls;
#ifdef _WIN32
static HMODULE library;
#else
static void *library;
#endif
static yyjson_val *metadata;

static uint64_t number(yyjson_val *value) {
    return yyjson_is_raw(value) ? strtoull(yyjson_get_raw(value), NULL, 10)
                                : yyjson_get_uint(value);
}

static goat_module_adapter_v1 adapter(const char *name) {
    yyjson_val *external = yyjson_obj_get(yyjson_obj_get(metadata, "externals"), name);
    assert(external);
    uint64_t id = number(yyjson_obj_get(external, "functionId"));
    yyjson_val *variant = yyjson_arr_get_first(yyjson_obj_get(metadata, "native"));
    yyjson_val *specializations = yyjson_obj_get(variant, "specializations");
    size_t index, count;
    yyjson_val *specification;
    const char *symbol = NULL;
    yyjson_arr_foreach(specializations, index, count, specification) {
        if (number(yyjson_obj_get(specification, "functionId")) == id) {
            assert(!symbol);
            symbol = yyjson_get_str(yyjson_obj_get(specification, "symbol"));
        }
    }
    assert(symbol);
#ifdef _WIN32
    FARPROC pointer = GetProcAddress(library, symbol);
#else
    void *pointer = dlsym(library, symbol);
#endif
    assert(pointer);
    goat_module_adapter_v1 result;
    assert(sizeof(result) == sizeof(pointer));
    memcpy(&result, &pointer, sizeof(result));
    return result;
}

static goat_module_value_v1 invoke(const char *name, goat_module_value_v1 argument) {
    goat_module_value_v1 result = {0};
    assert(adapter(name)(1, &context, 1, &argument, &result) == GOAT_MODULE_SUCCESS);
    assert(!result.reserved);
    ++calls;
    return result;
}

static void clear(void) {
    while (blocks)
        release(NULL, blocks->pointer);
}

static void string_case(const char *name,
                        const uint32_t *input,
                        size_t length,
                        const uint32_t *expected,
                        size_t expected_length) {
    goat_module_string_v1 view = {0};
    view.data = input;
    view.length = length;
    goat_module_value_v1 result = invoke(name, (goat_module_value_v1){3, 0, (uintptr_t)&view});
    assert(result.tag == 3 && result.payload && result.payload <= UINTPTR_MAX);
    goat_module_string_v1 *actual = (void *)(uintptr_t)result.payload;
    assert(actual->length == expected_length);
    assert(!expected_length || !memcmp(actual->data, expected, expected_length * sizeof(uint32_t)));
#if UINTPTR_MAX == UINT32_MAX
    assert(!actual->reserved);
#endif
    clear();
}

static void descriptor(void) {
#ifdef _WIN32
    FARPROC address = GetProcAddress(library, "goat_module_descriptor_v1");
    const unsigned char *bytes;
    memcpy(&bytes, &address, sizeof(bytes));
#else
    const unsigned char *bytes = dlsym(library, "goat_module_descriptor_v1");
#endif
    assert(bytes && !memcmp(bytes, "GOATND01", 8));
    uint64_t length = 0;
    for (unsigned i = 0; i < 8; ++i)
        length |= (uint64_t)bytes[8 + i] << (8 * i);
    assert(length < 16 * 1024 * 1024);
    yyjson_doc *doc = NULL;
    assert(goat_module_descriptor_read(bytes, (size_t)length + 17, &doc) == GOAT_ABI_OK);
    yyjson_val *root = yyjson_doc_get_root(doc);
    assert(!strcmp(yyjson_get_str(yyjson_obj_get(root, "module")),
                   yyjson_get_str(yyjson_obj_get(metadata, "name"))));
    const char *fields[] = {"version", "mainHash", "target", "specializations", "goatAbiVersion"};
    yyjson_val *variant = yyjson_arr_get_first(yyjson_obj_get(metadata, "native"));
    for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); ++i) {
        char *a = NULL, *b = NULL;
        size_t na, nb;
        assert(!goat_json_canonical(yyjson_obj_get(root, fields[i]), &a, &na));
        assert(
            !goat_json_canonical(yyjson_obj_get(i < 2 ? metadata : variant, fields[i]), &b, &nb));
        assert(na == nb && !memcmp(a, b, na));
        goat_module_json_free(a);
        goat_module_json_free(b);
    }
    yyjson_doc_free(doc);
    ++calls;
}

int main(int argc, char **argv) {
    fs_error_t error = {0};
    assert(fs_arguments(&argc, &argv, &error));
    assert(argc == 3);
    size_t length;
    void *bytes = read_binary_file(argv[2], 16 * 1024 * 1024, &length);
    yyjson_doc *doc = NULL;
    assert(bytes && !goat_module_json_read(bytes, length, GOAT_MODULE_METADATA, &doc));
    FREE(bytes);
    metadata = yyjson_doc_get_root(doc);
#ifdef _WIN32
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argv[1], -1, NULL, 0);
    assert(count);
    wchar_t *wide = malloc((size_t)count * sizeof(*wide));
    assert(wide && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argv[1], -1, wide, count));
    library = LoadLibraryW(wide);
    free(wide);
#else
    library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library)
        fprintf(stderr, "%s\n", dlerror());
#endif
    assert(library);
#ifdef _WIN32
    assert(!GetProcAddress(library, "goat_native_query_v2"));
#else
    assert(!dlsym(library, "goat_native_query_v2"));
#endif
    descriptor();
    const uint64_t reals[] = {0,
                              UINT64_C(0x8000000000000000),
                              UINT64_C(0x3ff8000000000000),
                              UINT64_C(0x7ff0000000000000),
                              UINT64_C(0xfff0000000000000),
                              UINT64_C(0x7ff8000000000042)};
    for (size_t i = 0; i < sizeof(reals) / sizeof(*reals); ++i) {
        goat_module_value_v1 result = invoke("real", (goat_module_value_v1){2, 0, reals[i]});
        assert(result.tag == 2 && result.payload == reals[i]);
        clear();
    }
    const int64_t integers[] = {0, 9, -12};
    for (size_t i = 0; i < sizeof(integers) / sizeof(*integers); ++i) {
        goat_module_value_v1 result =
            invoke("square", (goat_module_value_v1){1, 0, (uint64_t)integers[i]});
        assert(result.tag == 1 && result.payload == (uint64_t)(integers[i] * integers[i]));
        clear();
    }
    for (unsigned i = 0; i < 2; ++i) {
        goat_module_value_v1 result = invoke("boolean", (goat_module_value_v1){4, 0, i});
        assert(result.tag == 4 && result.payload == i);
        clear();
    }
    const uint32_t chars[] = {0, 'a', 0x1f600, 0x10ffff};
    for (size_t i = 0; i < sizeof(chars) / sizeof(*chars); ++i) {
        goat_module_value_v1 result = invoke("character", (goat_module_value_v1){5, 0, chars[i]});
        assert(result.tag == 5 && result.payload == chars[i]);
        clear();
    }
    const uint32_t text[] = {'a', 0, 0x1f600, 0x430};
    string_case("text", text, 4, text, 4);
    string_case("text", NULL, 0, NULL, 0);
    const uint32_t upper_text[] = {'A', 0, 0x1f600, 0x410};
    string_case("upper", text, 4, upper_text, 4);
    const uint32_t padded[] = {0x2003, 'a', 32};
    const uint32_t trimmed[] = {'a'};
    string_case("trim", padded, 3, trimmed, 1);
    assert(space_calls >= 3);
    goat_module_value_v1 argument = {1, 0, 7}, result = {0};
    assert(adapter("square")(99, &context, 1, &argument, &result) == GOAT_MODULE_ABI_MISMATCH);
    assert(adapter("square")(1, &context, 0, NULL, &result) == GOAT_MODULE_TYPE_MISMATCH);
    argument = (goat_module_value_v1){4, 0, 2};
    assert(adapter("boolean")(1, &context, 1, &argument, &result) == GOAT_MODULE_BAD_REQUEST);
    argument = (goat_module_value_v1){5, 0, 0xd800};
    assert(adapter("character")(1, &context, 1, &argument, &result) == GOAT_MODULE_BAD_REQUEST);
    clear();
    calls += 4;
#ifdef _WIN32
    assert(FreeLibrary(library));
#else
    assert(!dlclose(library));
#endif
    yyjson_doc_free(doc);
    free_fs_arguments(argc, argv);
    printf("%u generated module native value checks passed (%zu-bit)\n", calls, sizeof(void *) * 8);
    return 0;
}
