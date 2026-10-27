/** @file module_consistency_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent archive/descriptor/graph fixture harness, without OS loading.
 */
#include "lib/allocate.h"
#include "lib/file_system.h"
#include "lib/wire.h"
#include "vm/module_consistency.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#endif

int main(int argc, char **argv) {
    if (argc == 1) {
        bytecode_t *code = (void *)(uintptr_t)1;
        uint32_t entry = 99;
        module_consistency_error_t error;
        assert(read_module_consistency(NULL, &code, &entry, &error) == MODULE_CONSISTENCY_ARGUMENT);
        assert(!code && !entry && error.status == MODULE_CONSISTENCY_ARGUMENT);
        assert(match_module_descriptor(NULL, 0, "", 0) == MODULE_CONSISTENCY_ARGUMENT);
        assert(check_module_references(NULL, 0, 0) == MODULE_CONSISTENCY_ARGUMENT);
        assert(goat_module_native_validate(NULL, NULL) == GOAT_NATIVE_ARGUMENT);
        puts("Consistency API checks passed");
        return 0;
    }
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    const size_t capacity = 32 * 1024 * 1024;
    unsigned char *input = malloc(capacity);
    assert(input);
    size_t size = FREAD(input, 1, capacity, stdin), offset = 4;
    assert(size >= 4 && size < capacity && !FERROR(stdin));
    uint32_t count = read_le32(input);
    assert(count && count < 1024);
    goat_module_container **nodes = calloc(count, sizeof(*nodes));
    assert(nodes);
    int status = 0;
    for (uint32_t i = 0; i < count; i++) {
        assert(offset <= size && size - offset >= 4);
        size_t length = read_le32(input + offset);
        offset += 4;
        assert(length <= size - offset);
        goat_container_status container =
            goat_module_container_read(input + offset, length, SIZE_MAX, nodes + i, NULL);
        offset += length;
        if (container) {
            status = 100 + container;
            goto finish;
        }
        bytecode_t *code = NULL;
        uint32_t entry = 0;
        module_consistency_error_t error;
        status = read_module_consistency(nodes[i], &code, &entry, &error);
        assert(status == error.status);
        if (status) {
            assert(!code && !entry);
            goto finish;
        }
        zip_view_t original = goat_module_container_bytecode(nodes[i]);
        assert(code && entry < code->instructions_count && original.size == code->buffer_size);
        assert(!memcmp(original.data, code->buffer, original.size));
        free_bytecode(code);
    }
    if (!strcmp(argv[1], "descriptor")) {
        assert(argc == 3);
        status = match_module_descriptor(nodes[0],
                                         (size_t)strtoull(argv[2], NULL, 10),
                                         input + offset,
                                         size - offset);
    } else if (!strcmp(argv[1], "graph")) {
        assert(argc == 3);
        status = check_module_references((const goat_module_container *const *)nodes,
                                         count,
                                         (size_t)strtoull(argv[2], NULL, 10));
    } else {
        /* off/auto/required deliberately share the exact same structural path. */
        assert(!strcmp(argv[1], "off") || !strcmp(argv[1], "auto") || !strcmp(argv[1], "required"));
    }
finish:
    for (uint32_t i = 0; i < count; i++)
        goat_module_container_free(nodes[i]);
    free(nodes);
    free(input);
    assert(!get_allocated_memory_size());
    printf("%d\n", status);
    return 0;
}
