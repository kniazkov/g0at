/** @file source_include_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief End-to-end source include compilation, packaging and memory-only execution.
 */
#include "common/module_compilation.h"
#include "lib/files/file_system.h"
#include "lib/files/io.h"
#include "lib/memory/allocate.h"
#include "model/module_files.h"
#include "model/thread.h"
#include "vm/module_writer.h"
#include "vm/vm.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Reject oversized counts without requiring a multi-billion-entry allocation. */
static void check_compiler_boundaries(void) {
    module_compilation_t *out = NULL;
    module_compilation_error_t error = {0};
    assert(!compile_module_sources(NULL, &out, &error) && !out && error.message);
    clear_module_compilation_error(&error);
    module_source_t file = {0};
    module_sources_t source = {.files = &file, .count = SIZE_MAX};
    assert(!compile_module_sources(&source, &out, &error) && !out && error.message);
    clear_module_compilation_error(&error);
    source.count = 1;
    assert(!compile_module_sources(&source, &out, &error) && !out && error.message);
    clear_module_compilation_error(&error);
    file.source = "";
    file.name = "main.goat";
    file.includes.count = 1;
    assert(!compile_module_sources(&source, &out, &error) && !out && error.message);
    clear_module_compilation_error(&error);
    assert(!get_allocated_memory_size());
}

static int execute(validated_module_t *module) {
    process_t *process = create_process();
    const bytecode_t *code = validated_module_bytecode(module);
    unsigned char *original = malloc(code->buffer_size);
    assert(original);
    memcpy(original, code->buffer, code->buffer_size);
    module_files_t *files = NULL;
    assert(create_module_files_from_snapshot(
        process,
        goat_module_container_metadata(validated_module_container(module)),
        code,
        &files));
    int result = run_module_file(process, module_files_main(files));
    if (result && process->main_thread->exception.value) {
        function_access_t observer;
        init_function_access(&observer, module_files_identity(files));
        string_value_t message =
            object_to_string_with_access(process->main_thread->exception.value, &observer);
        fprintf_utf8(stderr, U"Uncaught: %s\n", message.data);
        FREE_STRING(message);
        clear_function_access(&observer, true);
    }
    assert(!memcmp(original, code->buffer, code->buffer_size));
    free(original);
    destroy_process(process);
    return result;
}

int main(int argc, char **argv) {
    check_compiler_boundaries();
    if (argc < 3 || argc > 4)
        return 2;
    int result = 1;
    validated_module_t *module = NULL;
    if (!strcmp(argv[1], "binary")) {
        module_validation_result_t checked = validate_module_file(argv[2], NULL, &module);
        if (checked.status)
            FPRINTF(stderr, "Invalid module: %d\n", checked.status);
        else
            result = execute(module);
    } else {
        module_sources_t *sources = NULL;
        module_sources_error_t source_error = {0};
        module_compilation_t *compilation = NULL;
        module_compilation_error_t compile_error = {0};
        module_writer_error_t write_error = {0};
        void *bytes = NULL;
        size_t size = 0;
        size_t length = strlen(argv[2]);
        bool descriptor = length >= 5 && !strcmp(argv[2] + length - 5, ".gmod");
        if (module_sources_read(argv[2], descriptor, NULL, &sources, &source_error)) {
            FPRINTF(stderr,
                    "%s:%zu:%zu: %s\n",
                    source_error.path ? source_error.path : argv[2],
                    source_error.row,
                    source_error.column,
                    source_error.message ? source_error.message : "source discovery failed");
        } else if (!compile_module_sources(sources, &compilation, &compile_error)) {
            FPRINTF(stderr,
                    "file %zu:%zu:%zu: %s\n",
                    compile_error.file,
                    compile_error.row,
                    compile_error.column,
                    compile_error.message ? compile_error.message : "compilation failed");
        } else {
            module_writer_input_t input = {
                .metadata = compilation->metadata,
                .bytecode = {compilation->code->buffer, compilation->code->buffer_size}};
            if (!strcmp(argv[1], "compile") && argc == 4) {
                result = module_write_file(argv[3], &input, NULL, &write_error) ? 1 : 0;
            } else if (!strcmp(argv[1], "run") && argc == 3) {
                if (!module_write_memory(&input, NULL, &bytes, &size, &write_error)) {
                    module_validation_result_t checked =
                        validate_module(bytes, size, argv[2], NULL, &module);
                    if (!checked.status)
                        result = execute(module);
                    else
                        FPRINTF(stderr, "Invalid compiled module: %d\n", checked.status);
                }
            }
            if (write_error.status)
                FPRINTF(stderr, "Module packaging failed: %d\n", write_error.status);
        }
        free_module_writer_bytes(bytes);
        clear_module_writer_error(&write_error);
        free_module_compilation(compilation);
        clear_module_compilation_error(&compile_error);
        free_module_sources(sources);
        clear_module_sources_error(&source_error);
    }
    free_validated_module(module);
    assert(!get_allocated_memory_size());
    return result;
}
