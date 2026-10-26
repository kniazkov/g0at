/** @file module_validation_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Common consumer fixtures for the nonexecuting validation API.
 */
#include "lib/allocate.h"
#include "lib/file_system.h"
#include "vm/module_validation.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#endif

static void quoted(const void *data, size_t size) {
    const unsigned char *text = data;
    putchar('"');
    for (size_t i = 0; i < size; i++) {
        unsigned char c = text[i];
        if (c == '"' || c == '\\') {
            putchar('\\');
            putchar(c);
        } else if (c < 32)
            printf("\\u%04x", c);
        else
            putchar(c);
    }
    putchar('"');
}

static void report(const module_validation_diagnostic_t *d, void *context) {
    size_t *count = context;
    (*count)++;
    printf("{\"check\":%d,\"state\":%d,\"prerequisite\":%d,\"detail\":%d,"
           "\"containerPrerequisite\":%d,\"container\":%d,\"zip\":%d,\"json\":%d,\"archive\":",
           d->check,
           d->state,
           d->prerequisite,
           d->detail,
           d->container_prerequisite,
           d->container.status,
           d->container.zip_error,
           d->container.json_error);
    quoted(d->archive, strlen(d->archive));
    printf(",\"entry\":");
    quoted(d->entry.data, d->entry.size);
    printf(",\"field\":");
    quoted(d->field, d->field_length);
    puts("}");
}

typedef struct {
    const char *archive;
    char *json;
    size_t size, count;
} metadata_capture_t;

static void metadata(const char *archive, const yyjson_doc *doc, void *context) {
    metadata_capture_t *capture = context;
    assert(!strcmp(archive, capture->archive));
    assert(!capture->count++);
    assert(
        !goat_module_json_write(doc, GOAT_MODULE_METADATA, false, &capture->json, &capture->size));
}

static void api_checks(void) {
    validated_module_t *module = (void *)(uintptr_t)1;
    assert(validate_module(NULL, 1, NULL, NULL, &module).status == MODULE_VALIDATION_ARGUMENT);
    assert(!module);
    assert(validate_module("", 0, NULL, NULL, NULL).status == MODULE_VALIDATION_ARGUMENT);
    module = (void *)(uintptr_t)1;
    assert(validate_module_file(NULL, NULL, &module).status == MODULE_VALIDATION_ARGUMENT);
    assert(!module);
    assert(!validated_module_container(NULL));
    assert(!validated_module_bytecode(NULL));
    assert(!validated_module_archive(NULL));
    assert(!validated_module_entry(NULL));
    free_validated_module(NULL);
    assert(!get_allocated_memory_size());
}

int main(int argc, char **argv) {
    if (argc == 1) {
        api_checks();
        puts("Validation API checks passed");
        return 0;
    }
#ifdef _WIN32
    fs_error_t arguments_error = {0};
    assert(fs_arguments(&argc, &argv, &arguments_error));
    clear_fs_error(&arguments_error);
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    const size_t capacity = 32 * 1024 * 1024;
    unsigned char *input = malloc(capacity);
    assert(input);
    size_t size = FREAD(input, 1, capacity, stdin);
    assert(size < capacity && !FERROR(stdin));
    bool file_mode = !strcmp(argv[1], "file");
    size_t events = 0;
    const char *label = argc > 2 ? argv[2] : "memory fixture.gbin";
    metadata_capture_t captured = {.archive = label};
    module_validation_options_t options = {.max_archive = SIZE_MAX,
                                           .max_expanded = SIZE_MAX,
                                           .report = report,
                                           .context = &events,
                                           .metadata = metadata,
                                           .metadata_context = &captured};
    if (argc > 3)
        options.max_archive = (size_t)strtoull(argv[3], NULL, 10);
    if (argc > 4)
        options.max_expanded = (size_t)strtoull(argv[4], NULL, 10);
    validated_module_t *module = (void *)(uintptr_t)1;
    module_validation_result_t result =
        file_mode ? validate_module_file(label, &options, &module)
                  : validate_module(input, size, label, &options, &module);
    if (result.status) {
        assert(!module && result.failures);
    } else {
        assert(module && !result.failures && !result.skipped);
        const bytecode_t *code = validated_module_bytecode(module);
        const goat_module_container *container = validated_module_container(module);
        assert(code && container && code->instructions_count);
        assert(validated_module_entry(module) < code->instructions_count);
        assert(!strcmp(validated_module_archive(module), label));
        size_t length = code->buffer_size;
        void *saved = malloc(length);
        assert(saved);
        memcpy(saved, code->buffer, length);
        zip_view_t image = goat_module_container_bytecode(container);
        assert(image.size == length && !memcmp(image.data, saved, length));
        /* The entire caller buffer can disappear immediately after validation. */
        memset(input, 0xa5, size);
        free(input);
        input = NULL;
        if (file_mode) {
            fs_error_t error = {0};
            FILE *replacement = NULL;
            char *temporary = create_fs_temporary(label, false, &replacement, &error);
            assert(temporary && replacement);
            assert(write_fs_file(replacement, "replacement", 11, temporary, &error));
            assert(close_fs_file(replacement, temporary, &error));
            assert(rename_fs_path(temporary, label, &error));
            FREE(temporary);
            clear_fs_error(&error);
        }
        assert(!memcmp(code->buffer, saved, length));
        image = goat_module_container_bytecode(container);
        assert(!memcmp(image.data, saved, length));
        assert(yyjson_is_obj(yyjson_doc_get_root(goat_module_container_metadata(container))));
        for (size_t i = 0; i < goat_module_container_native_count(container); i++) {
            goat_container_native native;
            assert(goat_module_container_native_at(container, i, &native));
            assert(native.name && native.name_length && native.bytes.size);
        }
        free(saved);
    }
    free_validated_module(module);
    free(input);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    assert(!get_allocated_memory_size());
    printf("{\"status\":%d,\"failures\":%zu,\"skipped\":%zu,\"events\":%zu,\"metadata\":",
           result.status,
           result.failures,
           result.skipped,
           events);
    /* The copied metadata remains usable after the invalid/valid snapshot and
     * input storage have both gone away; it is not an executable snapshot. */
    if (captured.json)
        fwrite(captured.json, 1, captured.size, stdout);
    else
        fputs("null", stdout);
    puts("}");
    goat_module_json_free(captured.json);
    return 0;
}
