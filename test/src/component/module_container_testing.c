#include "lib/files/file_system.h"
#include "module_container.h"

/* Keep API calls and validation assertions active in Release builds. */
#ifdef NDEBUG
#    undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#endif

static void hex(zip_view_t bytes) {
    for (size_t i = 0; i < bytes.size; i++)
        printf("%02x", bytes.data[i]);
}

static void api_checks(void) {
    goat_module_container *c = (void *)(uintptr_t)1;
    goat_container_error error;
    assert(goat_module_container_read(NULL, 1, SIZE_MAX, &c, &error) == GOAT_CONTAINER_INVALID);
    assert(!c && error.status == GOAT_CONTAINER_INVALID && error.entry_index == SIZE_MAX);
    assert(goat_module_container_read(NULL, 0, SIZE_MAX, &c, NULL) == GOAT_CONTAINER_ZIP && !c);
    assert(goat_module_container_read(NULL, 0, SIZE_MAX, NULL, NULL) == GOAT_CONTAINER_INVALID);
    assert(goat_module_container_read("", UINT32_MAX, SIZE_MAX, &c, &error) == GOAT_CONTAINER_LIMIT
           && !c);
    goat_module_container_free(NULL);
    assert(!goat_module_container_metadata(NULL));
    assert(!goat_module_container_bytecode(NULL).data);
    assert(!goat_module_container_native_count(NULL));
    goat_container_native item;
    assert(!goat_module_container_native_at(NULL, 0, &item) && !item.bytes.data);
    puts("Container API checks passed");
}

int main(int argc, char **argv) {
    if (argc == 1) {
        api_checks();
        return 0;
    }
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    size_t capacity = 32 * 1024 * 1024;
    void *input = malloc(capacity);
    assert(input);
    size_t size = FREAD(input, 1, capacity, stdin);
    assert(size < capacity && !FERROR(stdin));
    size_t limit = argc > 2 ? (size_t)strtoull(argv[2], NULL, 10) : SIZE_MAX;
    goat_module_container *c = NULL;
    goat_container_error error;
    goat_container_status status = goat_module_container_read(input, size, limit, &c, &error);
    memset(input, 0xaa, size);
    free(input);
    if (status) {
        assert(!c && error.status == status);
        FPRINTF(stderr,
                "%s; entry=%zu; zip=%u; json=%u\n",
                goat_container_status_string(status),
                error.entry_index,
                error.zip_error,
                error.json_error);
        return status;
    }
    assert(c && error.status == GOAT_CONTAINER_OK);
    char *json = NULL;
    size_t length = 0;
    assert(goat_module_json_write(goat_module_container_metadata(c),
                                  GOAT_MODULE_METADATA,
                                  false,
                                  &json,
                                  &length)
           == GOAT_MODULE_JSON_OK);
    printf("{\"metadata\":");
    FWRITE(json, 1, length, stdout);
    goat_module_json_free(json);
    printf(",\"bytecode\":\"");
    hex(goat_module_container_bytecode(c));
    printf("\",\"native\":[");
    size_t count = goat_module_container_native_count(c);
    for (size_t i = 0; i < count; i++) {
        goat_container_native item;
        assert(goat_module_container_native_at(c, i, &item));
        printf("%s{\"name\":\"", i ? "," : "");
        FWRITE(item.name, 1, item.name_length, stdout);
        printf("\",\"data\":\"");
        hex(item.bytes);
        printf("\"}");
    }
    goat_container_native invalid;
    assert(!goat_module_container_native_at(c, count, &invalid) && !invalid.name
           && !invalid.bytes.data);
    printf("]}\n");
    goat_module_container_free(c);
    return 0;
}
