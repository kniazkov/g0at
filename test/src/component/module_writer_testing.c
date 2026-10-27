/** @file module_writer_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Deterministic writer and atomic-publication component harness.
 */
#include "lib/files/binary_file.h"
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "vm/module_writer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#endif

static void api_checks(void) {
    void *data = (void *)(uintptr_t)1;
    size_t size = 123;
    module_writer_error_t error = {0};
    assert(module_write_memory(NULL, NULL, &data, &size, &error) == MODULE_WRITER_ARGUMENT);
    assert(!data && !size);
    clear_module_writer_error(&error);
    assert(module_write_file(NULL, NULL, NULL, &error) == MODULE_WRITER_ARGUMENT);
    clear_module_writer_error(&error);
    free_module_writer_bytes(NULL);
    yyjson_doc *doc = yyjson_read("{}", 2, 0);
    assert(doc);
    module_writer_native_t part = {"native/x.so", 11, {(const uint8_t *)"x", 1}};
    module_writer_input_t input = {.metadata = doc,
                                   .native = &part,
                                   .native_count = UINT16_MAX - 2};
    assert(module_write_memory(&input, NULL, &data, &size, &error) == MODULE_WRITER_LIMIT);
    assert(!data && !size);
    input.native_count = 0;
    input.bytecode = (zip_view_t){(const uint8_t *)"x", UINT32_MAX};
    assert(module_write_memory(&input, NULL, &data, &size, &error) == MODULE_WRITER_LIMIT);
    input.bytecode = (zip_view_t){NULL, 1};
    assert(module_write_memory(&input, NULL, &data, &size, &error) == MODULE_WRITER_ARGUMENT);
    input.bytecode = (zip_view_t){0};
    assert(module_write_memory(&input, NULL, NULL, &size, &error) == MODULE_WRITER_ARGUMENT);
    assert(module_write_memory(&input, NULL, &data, NULL, &error) == MODULE_WRITER_ARGUMENT);
    input.native_count = 1;
    input.native = NULL;
    assert(module_write_memory(&input, NULL, &data, &size, &error) == MODULE_WRITER_ARGUMENT);
    clear_module_writer_error(&error);
    yyjson_doc_free(doc);
    assert(!get_allocated_memory_size());
}

int main(int argc, char **argv) {
    if (argc == 1) {
        api_checks();
        puts("Writer API checks passed");
        return 0;
    }
#ifdef _WIN32
    fs_error_t arguments_error = {0};
    assert(fs_arguments(&argc, &argv, &arguments_error));
    clear_fs_error(&arguments_error);
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    const size_t capacity = 32 * 1024 * 1024;
    unsigned char *raw = malloc(capacity);
    assert(raw);
    size_t length = FREAD(raw, 1, capacity, stdin);
    assert(length < capacity && !FERROR(stdin));
    uint64_t original = binary_checksum(raw, length);
    zip_archive_t *archive = NULL;
    assert(zip_archive_open(raw, length, &archive) == ZIP_SUCCESS);
    zip_layout_t layout;
    assert(zip_archive_layout(archive, &layout) == ZIP_SUCCESS);
    void **contents = calloc(layout.count, sizeof(*contents));
    char **names = calloc(layout.count, sizeof(*names));
    module_writer_native_t *native = calloc(layout.count, sizeof(*native));
    assert(contents && names && native);
    module_writer_input_t input = {0};
    yyjson_doc *metadata = NULL;
    for (size_t i = 0; i < layout.count; i++) {
        zip_entry_t entry;
        assert(zip_archive_entry(archive, i, &entry) == ZIP_SUCCESS);
        contents[i] = malloc(entry.size ? entry.size : 1);
        names[i] = malloc(entry.name.size + 1);
        assert(contents[i] && names[i]);
        memcpy(names[i], entry.name.data, entry.name.size);
        names[i][entry.name.size] = 0;
        size_t written;
        assert(zip_archive_read(archive, i, contents[i], entry.size, &written) == ZIP_SUCCESS);
        if (!strcmp(names[i], "module.json")) {
            metadata = yyjson_read(contents[i], written, YYJSON_READ_NUMBER_AS_RAW);
            assert(metadata);
            input.metadata = metadata;
        } else if (!strcmp(names[i], "bytecode.bin")) {
            input.bytecode = (zip_view_t){contents[i], written};
        } else {
            native[input.native_count++] =
                (module_writer_native_t){names[i], entry.name.size, {contents[i], written}};
        }
    }
    input.native = native;
    char *saved_metadata = yyjson_write(metadata, 0, NULL);
    assert(saved_metadata);
    module_writer_options_t options = {SIZE_MAX, SIZE_MAX};
    if (argc > 3)
        options.max_archive = (size_t)strtoull(argv[3], NULL, 10);
    if (argc > 4)
        options.max_expanded = (size_t)strtoull(argv[4], NULL, 10);
    module_writer_error_t error = {0};
    void *output = (void *)(uintptr_t)1;
    size_t size = 99;
    bool file_mode = !strcmp(argv[1], "file");
    module_writer_status_t status =
        file_mode ? module_write_file(argv[2], &input, &options, &error)
                  : module_write_memory(&input, &options, &output, &size, &error);
    if (!file_mode) {
        if (status)
            assert(!output && !size);
        else {
            validated_module_t *validated = NULL;
            assert(validate_module(output, size, "writer output", NULL, &validated).status
                   == MODULE_VALIDATION_OK);
            free_validated_module(validated);
            assert(FWRITE(output, 1, size, stdout) == size);
        }
        free_module_writer_bytes(output);
    }
    assert(binary_checksum(raw, length) == original);
    char *after_metadata = yyjson_write(metadata, 0, NULL);
    assert(after_metadata && !strcmp(saved_metadata, after_metadata));
    free(saved_metadata);
    free(after_metadata);
    for (size_t i = 0; i < layout.count; i++) {
        zip_entry_t entry;
        assert(zip_archive_entry(archive, i, &entry) == ZIP_SUCCESS);
        void *expected = malloc(entry.size ? entry.size : 1);
        assert(expected);
        size_t written;
        assert(zip_archive_read(archive, i, expected, entry.size, &written) == ZIP_SUCCESS);
        assert(!memcmp(expected, contents[i], written));
        assert(strlen(names[i]) == entry.name.size
               && !memcmp(names[i], entry.name.data, entry.name.size));
        free(expected);
    }
    if (status)
        FPRINTF(stderr,
                "writer status %d, JSON %d, ZIP %d, validation %d, FS %d\n",
                status,
                error.json,
                error.zip,
                error.validation.status,
                error.filesystem.status);
    clear_module_writer_error(&error);
    yyjson_doc_free(metadata);
    for (size_t i = 0; i < layout.count; i++) {
        free(contents[i]);
        free(names[i]);
    }
    free(contents);
    free(names);
    free(native);
    zip_archive_close(archive);
    free(raw);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    assert(!get_allocated_memory_size());
    return status;
}
