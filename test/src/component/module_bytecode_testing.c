/** @file module_bytecode_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Binary stdin harness for independent module bytecode fixtures.
 */
#ifdef NDEBUG
#    undef NDEBUG
#endif
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "lib/wire/wire.h"
#include "vm/module_bytecode.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#endif

int main(int argc, char **argv) {
    (void)argv;
    bytecode_t *code = (void *)(uintptr_t)1;
    uint32_t entry = 99;
    module_bytecode_error_t error;
    if (argc == 1) {
        assert(read_module_bytecode(NULL, 0, NULL, 0, &code, &entry, &error)
               == MODULE_BYTECODE_ARGUMENT);
        assert(!code && !entry && error.index == SIZE_MAX);
        assert(read_module_bytecode("", 0, "", 0, NULL, &entry, NULL) == MODULE_BYTECODE_ARGUMENT);
        assert(read_module_bytecode("", 0, "", 0, &code, NULL, NULL) == MODULE_BYTECODE_ARGUMENT);
        puts("Module bytecode API checks passed");
        return 0;
    }
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    size_t capacity = 32 * 1024 * 1024;
    unsigned char *input = malloc(capacity);
    assert(input);
    size_t size = FREAD(input, 1, capacity, stdin);
    assert(size >= 4 && size < capacity && !FERROR(stdin));
    size_t json_size = read_le32(input);
    assert(json_size <= size - 4);
    module_bytecode_status_t status = read_module_bytecode((const char *)input + 4,
                                                           json_size,
                                                           input + 4 + json_size,
                                                           size - 4 - json_size,
                                                           &code,
                                                           &entry,
                                                           &error);
    assert(error.status == status);
    if (!status) {
        assert(code && entry < code->instructions_count);
        assert(!memcmp(code->buffer, input + 4 + json_size, code->buffer_size));
        memset(input, 0xaa, size);
        assert(validate_bytecode_image(code));
        printf("%u\n", entry);
        free_bytecode(code);
    } else {
        assert(!code && !entry);
        FPRINTF(stderr,
                "%s; index=%zu; json=%u\n",
                module_bytecode_status_string(status),
                error.index,
                error.json_error);
    }
    free(input);
    assert(!get_allocated_memory_size());
    return (int)status;
}
