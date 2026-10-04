/** @file binary_program.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Artifact paths, native fallback policy and VM invocation.
 */
#include "binary_program.h"

#include "c_output.h"
#include "lib/allocate.h"
#include "lib/binary_file.h"
#include "lib/io.h"
#include "model/thread.h"
#include "resources/messages.h"
#include "vm/binary.h"
#include "vm/vm.h"

#include <stdio.h>

#ifdef _WIN32
#    define LIBRARY_EXTENSION "dll"
#else
#    define LIBRARY_EXTENSION "so"
#endif

static bool conflicts(const options_t *options, const path_t *path) {
    return !path || paths_refer_to_same_file(path, options->input_file)
           || paths_refer_to_same_file(path, options->native_output_file)
           || paths_refer_to_same_file(path, options->analysis_output_file)
           || paths_refer_to_same_file(path, options->graph_output_file);
}

bool binary_program_options_valid(const options_t *options) {
    if (!options->compile_only && !options->run_binary)
        return true;
    path_t *binary = options->compile_only ? output_path(options->input_file, "gbin") : NULL;
    path_t *library = output_path(options->input_file, LIBRARY_EXTENSION);
    bool ok = !paths_refer_to_same_file(options->input_file, options->analysis_output_file)
              && !paths_refer_to_same_file(options->input_file, options->graph_output_file)
              && (!options->compile_only || !conflicts(options, binary))
              && (!library || !conflicts(options, library))
              && (options->native_execution == NATIVE_OFF || library);
    free_path(binary);
    free_path(library);
    if (!ok)
        fprintf(stderr, "Compiled output conflicts with input or a report.\n");
    return ok;
}

bool compile_binary_program(const options_t *options, const node_t *root, bytecode_t *code) {
    path_t *binary = output_path(options->input_file, "gbin");
    path_t *library = output_path(options->input_file, LIBRARY_EXTENSION);
    bool ok = !conflicts(options, binary)
              && (options->native_execution == NATIVE_OFF || !conflicts(options, library));
    native_execution_report_t report = {.preparation = "disabled"};
    if (!ok) {
        fprintf(stderr, "Compiled output conflicts with input or a report.\n");
        goto done;
    }
    if (!prepare_native_program_artifact(options,
                                         root,
                                         code,
                                         &report,
                                         library ? library->normal_path : NULL)) {
        ok = false;
        goto done;
    }
    uint64_t checksum = 0;
    if (report.bound_functions) {
        size_t size;
        void *bytes = read_binary_file(library->normal_path, GOAT_BINARY_LIMIT, &size);
        ok = bytes != NULL;
        if (ok)
            checksum = binary_checksum(bytes, size);
        FREE(bytes);
    }
    if (ok)
        ok = save_binary_program(binary->normal_path, code, checksum);
    if (!ok)
        fprintf(stderr, "Could not save compiled program.\n");
done:
    if (!output_native_report(options, &report, NULL))
        ok = false;
    free_path(binary);
    free_path(library);
    return ok;
}

int execute_program(const options_t *options,
                    bytecode_t *code,
                    const native_execution_report_t *report) {
    if (options->print_bytecode) {
        string_value_t text = bytecode_to_text(code);
        print_utf8(text.data);
        FREE_STRING(text);
    }
    process_t *process = create_process();
    int status = run(process, code);
    thread_t *thread = process->main_thread;
    do {
        if (thread->native_status)
            fprintf(stderr, "Native backend failed (status %u).\n", thread->native_status);
        if (thread->exception.value) {
            string_value_t text = convert_object_to_string(thread->exception.value);
            fprintf_utf8(stderr, get_messages()->uncaught_exception, text.data);
            fprintf(stderr, "\n");
            FREE_STRING(text);
        }
        thread = thread->next;
    } while (thread != process->main_thread);
    if (!output_native_report(options, report, process))
        status = -1;
    destroy_process(process);
    return status;
}

int run_binary_program(const options_t *options) {
    binary_program_t program = load_binary_program(options->input_file->full_path);
    if (!program.code) {
        fprintf(stderr, "Invalid, incompatible or unreadable Goat binary.\n");
        return -1;
    }
    native_execution_report_t report = {.preparation = "disabled"};
    bool ready = true;
    if (options->native_execution != NATIVE_OFF) {
        path_t *library = output_path(options->input_file, LIBRARY_EXTENSION);
        if (program.binding_count && conflicts(options, library)) {
            fprintf(stderr, "Native library conflicts with input or a report.\n");
            free_path(library);
            destroy_binary_program(&program);
            return -1;
        }
        bool bound =
            program.binding_count && library && bind_binary_library(&program, library->full_path);
        report.preparation = bound ? "ready" : program.binding_count ? "load-error" : "empty";
        report.bound_functions = bound ? program.binding_count : 0;
        ready = bound || options->native_execution == NATIVE_AUTO;
        if ((!bound && program.binding_count) || !ready)
            fprintf(stderr, "Native artifact unavailable, incompatible or mismatched.\n");
        free_path(library);
    }
    int status;
    if (ready)
        status = execute_program(options, program.code, &report);
    else {
        output_native_report(options, &report, NULL);
        status = -1;
    }
    destroy_binary_program(&program);
    return status;
}
