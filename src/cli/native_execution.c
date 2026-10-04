/** @file native_execution.c
 * @copyright 2026 Ivan Kniazkov
 * @brief CLI preparation failure policy and stable execution counters.
 */
#include "native_execution.h"

#include "codegen/native_pipeline.h"
#include "lib/allocate.h"
#include "lib/io.h"
#include "lib/string_ext.h"
#include "model/process.h"
#include "model/thread.h"
#include "resources/messages.h"

#include <stdio.h>
#include <stdlib.h>

static const char *status_name(native_prepare_status_t status) {
    switch (status) {
        case NATIVE_PREPARE_READY:
            return "ready";
        case NATIVE_PREPARE_EMPTY:
            return "empty";
        case NATIVE_PREPARE_IO_ERROR:
            return "io-error";
        case NATIVE_PREPARE_COMPILE_ERROR:
            return "compile-error";
        case NATIVE_PREPARE_LOAD_ERROR:
            return "load-error";
        case NATIVE_PREPARE_BIND_ERROR:
            return "bind-error";
    }
    return "unknown";
}

bool prepare_native_program(const options_t *options,
                            const node_t *root,
                            bytecode_t *code,
                            native_execution_report_t *report) {
    *report = (native_execution_report_t){.preparation = "disabled"};
    if (options->native_execution == NATIVE_OFF)
        return true;
    const char *compiler = getenv("CC");
    native_prepare_result_t prepared =
        prepare_native_execution(root, code, compiler && *compiler ? compiler : NULL);
    report->preparation = status_name(prepared.status);
    report->bound_functions = prepared.bound_functions;
    report->omitted_specializations = prepared.omitted_specializations;
    bool ready = prepared.status == NATIVE_PREPARE_READY;
    bool failed = !ready && prepared.status != NATIVE_PREPARE_EMPTY;
    if (failed || (!ready && options->native_execution == NATIVE_REQUIRED)) {
        fprintf_utf8(stderr,
                     get_messages()->native_prepare_failed,
                     report->preparation,
                     prepared.diagnostic ? prepared.diagnostic
                                         : "No native functions could be bound");
        fputc('\n', stderr);
    }
    destroy_native_prepare_result(&prepared);
    return ready || options->native_execution == NATIVE_AUTO;
}

bool output_native_report(const options_t *options,
                          const native_execution_report_t *report,
                          const process_t *process) {
    if (!options->print_native && !options->native_output_file)
        return true;
    size_t attempts = 0, successes = 0, retries = 0;
    const thread_t *first = process ? process->main_thread : NULL;
    if (first) {
        const thread_t *thread = first;
        do {
            attempts += thread->native_attempts;
            successes += thread->native_successes;
            retries += thread->native_retries;
            thread = thread->next;
        } while (thread != first);
    }
    const char *mode = options->native_execution == NATIVE_OFF    ? "off"
                       : options->native_execution == NATIVE_AUTO ? "auto"
                                                                  : "required";
    string_value_t text = format_string(L"mode=%a\npreparation=%a\nbound=%zu\nomitted=%zu\n"
                                        L"attempts=%zu\nsucceeded=%zu\nretries=%zu\n",
                                        mode,
                                        report->preparation,
                                        report->bound_functions,
                                        report->omitted_specializations,
                                        attempts,
                                        successes,
                                        retries);
    if (options->print_native) {
        fputc('\n', stdout);
        print_utf8(text.data);
    }
    bool written = true;
    if (options->native_output_file) {
        if (paths_refer_to_same_file(options->input_file, options->native_output_file)) {
            fprintf_utf8(stderr, get_messages()->native_report_conflict);
            written = false;
        } else if (!write_utf8_file(options->native_output_file->full_path, text.data)) {
            fprintf_utf8(stderr,
                         get_messages()->cannot_write_native_report,
                         options->native_output_file->normal_path);
            written = false;
        }
        if (!written)
            fputc('\n', stderr);
    }
    FREE_STRING(text);
    return written;
}
