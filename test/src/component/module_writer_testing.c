/** @file module_writer_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Deterministic writer and atomic-publication component harness.
 */
/* The component host performs setup/I/O inside assertions; keep those checks
 * active when linked to Release production libraries for #364 parity. */
#ifdef NDEBUG
#    undef NDEBUG
#endif

#include "lib/files/binary_file.h"
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "lib/wire/wire.h"
#include "module/module_hash.h"
#include "vm/module_writer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#endif

/* Independent little-endian producer, with no descriptors or function records.
 * These vectors exercise semantic validation through both public entry points. */
static void operator_image_check(const instruction_t *instructions, size_t count, bool valid) {
    uint8_t image[1024] = {0};
    size_t file_offset = 64 + count * 8, location_offset = file_offset + 16;
    size_t size = location_offset + 24;
    assert(size <= sizeof(image));
    memcpy(image, "GOATBC01", 8);
    const uint32_t header[] = {1,
                               64,
                               (uint32_t)size,
                               (uint32_t)count,
                               0,
                               1,
                               0,
                               1,
                               64,
                               (uint32_t)file_offset,
                               (uint32_t)file_offset,
                               (uint32_t)location_offset,
                               (uint32_t)location_offset,
                               (uint32_t)size};
    for (size_t i = 0; i < sizeof(header) / sizeof(*header); ++i)
        write_le32(image + 8 + i * 4, header[i]);
    for (size_t i = 0; i < count; ++i) {
        image[64 + i * 8] = instructions[i].opcode;
        image[66 + i * 8] = (uint8_t)instructions[i].arg0;
        image[67 + i * 8] = (uint8_t)(instructions[i].arg0 >> 8);
        write_le32(image + 68 + i * 8, instructions[i].arg1);
    }
    write_le32(image + file_offset + 4, (uint32_t)count);
    write_le32(image + location_offset + 4, (uint32_t)count);
    uint8_t original[1024];
    memcpy(original, image, size);
    bytecode_t *decoded = decode_bytecode_image(image, size);
    assert((decoded != NULL) == valid);
    if (decoded) {
        assert(!memcmp(decoded->buffer, image, size));
        free_bytecode(decoded);
    }
    const char *pattern = "{\"formatVersion\":1,\"bytecodeVersion\":1,\"name\":\"operators\","
                          "\"version\":\"0\",\"main\":0,\"files\":[{\"name\":\"operators.goat\","
                          "\"entry\":0,\"external\":false}],\"functions\":[],\"mainHash\":\"%s\"}";
    char json[512], hash[65];
    snprintf(json,
             sizeof(json),
             pattern,
             "0000000000000000000000000000000000000000000000000000000000000000");
    yyjson_doc *metadata = yyjson_read(json, strlen(json), 0);
    assert(metadata && !goat_module_main_hash(metadata, image, size, hash));
    yyjson_doc_free(metadata);
    snprintf(json, sizeof(json), pattern, hash);
    metadata = yyjson_read(json, strlen(json), 0);
    assert(metadata);
    module_writer_input_t input = {.metadata = metadata, .bytecode = {image, size}};
    module_writer_error_t error = {0};
    void *first = NULL, *second = NULL;
    size_t first_size = 0, second_size = 0;
    module_writer_status_t status = module_write_memory(&input, NULL, &first, &first_size, &error);
    assert(status == (valid ? MODULE_WRITER_OK : MODULE_WRITER_VALIDATION));
    if (valid) {
        assert(!module_write_memory(&input, NULL, &second, &second_size, &error));
        assert(first_size == second_size && !memcmp(first, second, first_size));
    } else {
        assert(!first && !first_size);
    }
    assert(!memcmp(original, image, size));
    free_module_writer_bytes(first);
    free_module_writer_bytes(second);
    clear_module_writer_error(&error);
    yyjson_doc_free(metadata);
}

static void operator_checks(void) {
    /* Both zero and multiple index operands are valid for user handlers. */
    instruction_t indices[] = {{.opcode = NIL},
                               {.opcode = NIL},
                               {.opcode = NIL},
                               {.opcode = CHECKINDEX, .arg1 = 2},
                               {.opcode = DUPN, .arg1 = 3},
                               {.opcode = GETINDEX, .arg1 = 2},
                               {.opcode = ROTATE, .arg1 = 3},
                               {.opcode = SETINDEX, .arg1 = 2},
                               {.opcode = POP},
                               {.opcode = END}};
    operator_image_check(indices, sizeof(indices) / sizeof(*indices), true);
    instruction_t empty[] = {{.opcode = NIL},
                             {.opcode = GETINDEX},
                             {.opcode = NIL},
                             {.opcode = CHECKINDEX},
                             {.opcode = SETINDEX},
                             {.opcode = POP},
                             {.opcode = END}};
    operator_image_check(empty, sizeof(empty) / sizeof(*empty), true);
    instruction_t logical[] = {{.opcode = NIL},
                               {.opcode = LAND, .arg1 = 7},
                               {.opcode = NIL},
                               {.opcode = LOR, .arg1 = 6},
                               {.opcode = NIL},
                               {.opcode = BOOL, .arg0 = 1},
                               {.opcode = BOOL, .arg0 = 1},
                               {.opcode = POP},
                               {.opcode = END}};
    operator_image_check(logical, sizeof(logical) / sizeof(*logical), true);
    logical[5].arg0 = 0; /* Missing nested completion. */
    operator_image_check(logical, sizeof(logical) / sizeof(*logical), false);
    logical[5].arg0 = 1;
    logical[3].arg1 = 7; /* Shared completion loses one selection. */
    operator_image_check(logical, sizeof(logical) / sizeof(*logical), false);
    logical[3].arg1 = 6;
    logical[4] = (instruction_t){.opcode = JUMP, .arg1 = 7}; /* Escape selection. */
    operator_image_check(logical, sizeof(logical) / sizeof(*logical), false);
    instruction_t orphan[] = {{.opcode = NIL},
                              {.opcode = NIL},
                              {.opcode = NIL},
                              {.opcode = BOOL, .arg0 = 1},
                              {.opcode = END}};
    operator_image_check(orphan, sizeof(orphan) / sizeof(*orphan), false);
    const opcode_t counts[] = {GETINDEX, SETINDEX, CHECKINDEX, DUPN, ROTATE, ARRAY};
    for (size_t i = 0; i < sizeof(counts) / sizeof(*counts); ++i) {
        instruction_t malformed[] = {{.opcode = NIL},
                                     {.opcode = counts[i], .arg1 = UINT32_MAX},
                                     {.opcode = END}};
        operator_image_check(malformed, 3, false);
        malformed[1].arg1 = 2;
        operator_image_check(malformed, 3, false);
    }
    for (unsigned method = 0; method < 2; ++method) {
        instruction_t call[] = {{.opcode = NIL},
                                {.opcode = method ? CALLM : CALL, .arg0 = 2},
                                {.opcode = END}};
        operator_image_check(call, 3, false);
        call[1].arg0 = UINT16_MAX;
        operator_image_check(call, 3, false);
    }
    instruction_t join[] = {{.opcode = NIL},
                            {.opcode = JIF, .arg1 = 4},
                            {.opcode = NIL},
                            {.opcode = JUMP, .arg1 = 4},
                            {.opcode = END}};
    operator_image_check(join, 5, false); /* Inconsistent incoming stack heights. */
    instruction_t branch_count[] = {{.opcode = JUMP, .arg1 = 2},
                                    {.opcode = END},
                                    {.opcode = CHECKINDEX},
                                    {.opcode = END}};
    operator_image_check(branch_count, 4, false); /* A target is not an unknown stack. */
    instruction_t lost_selection[] = {{.opcode = NIL},
                                      {.opcode = LAND, .arg1 = 8},
                                      {.opcode = POP},
                                      {.opcode = POP},
                                      {.opcode = NIL},
                                      {.opcode = NIL},
                                      {.opcode = NIL},
                                      {.opcode = BOOL, .arg0 = 1},
                                      {.opcode = END}};
    operator_image_check(lost_selection, 9, false);
    instruction_t loop_count[] = {{.opcode = LOOP_ENTER},
                                  {.opcode = LOOP_JUMP, .arg1 = 2},
                                  {.opcode = GETINDEX, .arg1 = UINT32_MAX},
                                  {.opcode = END}};
    operator_image_check(loop_count, 4, false);
    loop_count[2].arg1 = 0;
    operator_image_check(loop_count, 4, false);
    loop_count[2] = (instruction_t){.opcode = RESTORE};
    operator_image_check(loop_count, 4, true);
    instruction_t finally_count[] = {{.opcode = FINALLY_ENTER, .arg1 = 4},
                                     {.opcode = FINALLY_LEAVE, .arg1 = 2},
                                     {.opcode = CHECKINDEX, .arg1 = UINT32_MAX},
                                     {.opcode = END},
                                     {.opcode = FINALLY_END},
                                     {.opcode = END}};
    operator_image_check(finally_count, 6, false);
    finally_count[2].arg1 = 0;
    operator_image_check(finally_count, 6, false);
    finally_count[2] = (instruction_t){.opcode = NIL};
    operator_image_check(finally_count, 6, true);
    instruction_t lost_try_prefix[] = {{.opcode = NIL},
                                       {.opcode = TRY, .arg1 = 5},
                                       {.opcode = POP},
                                       {.opcode = NIL},
                                       {.opcode = THROW},
                                       {.opcode = POP},
                                       {.opcode = POP},
                                       {.opcode = END}};
    operator_image_check(lost_try_prefix, 8, false);
    instruction_t lost_loop_prefix[] = {{.opcode = NIL},
                                        {.opcode = LOOP_ENTER},
                                        {.opcode = POP},
                                        {.opcode = LOOP_JUMP, .arg1 = 4},
                                        {.opcode = POP},
                                        {.opcode = END}};
    operator_image_check(lost_loop_prefix, 6, false);
    instruction_t lost_cleanup_prefix[] = {{.opcode = NIL},
                                           {.opcode = FINALLY_ENTER, .arg1 = 4},
                                           {.opcode = FINALLY_LEAVE, .arg1 = 6},
                                           {.opcode = END},
                                           {.opcode = POP},
                                           {.opcode = FINALLY_END},
                                           {.opcode = END}};
    operator_image_check(lost_cleanup_prefix, 7, false);
    instruction_t ancestor_exit[] = {{.opcode = NIL},
                                     {.opcode = LAND, .arg1 = 17},
                                     {.opcode = LOOP_ENTER},
                                     {.opcode = NIL},
                                     {.opcode = LAND, .arg1 = 11},
                                     {.opcode = LOOP_JUMP, .arg1 = 12},
                                     {.opcode = NIL},
                                     {.opcode = NOP},
                                     {.opcode = NOP},
                                     {.opcode = NOP},
                                     {.opcode = BOOL, .arg0 = 1},
                                     {.opcode = POP},
                                     {.opcode = RESTORE},
                                     {.opcode = NIL},
                                     {.opcode = NOP},
                                     {.opcode = NOP},
                                     {.opcode = BOOL, .arg0 = 1},
                                     {.opcode = POP},
                                     {.opcode = END}};
    operator_image_check(ancestor_exit, 19, true);
    assert(!get_allocated_memory_size());
}

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
        operator_checks();
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
