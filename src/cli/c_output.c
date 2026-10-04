/** @file c_output.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Default C filename, UTF-8 output and backend diagnostics.
 */
#include "c_output.h"

#include "codegen/c_module_output.h"
#include "lib/allocate.h"
#include "lib/io.h"
#include "resources/messages.h"

#include <stdio.h>
#include <string.h>

path_t *c_output_path(const path_t *input) {
    if (!input || !input->file_name || !input->file_name[0])
        return create_path("generated.c");
    if (input->extension && !strcasecmp(input->extension, "c"))
        return NULL;
    size_t length = strlen(input->normal_path);
    const char *dot = strrchr(input->file_name, '.');
    if (dot && dot != input->file_name)
        length -= strlen(dot);
    char *name = ALLOC(length + 3);
    memcpy(name, input->normal_path, length);
    memcpy(name + length, ".c", 3);
    path_t *result = create_path(name);
    FREE(name);
    return result;
}

bool output_c_module(const options_t *options, arena_t *arena, const node_t *root) {
    c_module_t *module = create_c_module(arena, root);
    c_module_output_t output = generate_c_module(arena, module);
    for (const c_module_failure_t *failure = output.failures; failure; failure = failure->next) {
        const wchar_t *reason =
            failure->dependency                           ? L"dependency unavailable"
            : failure->status == C_GENERATION_UNSUPPORTED ? L"unsupported lowering"
            : failure->status == C_GENERATION_NOT_PROVEN  ? L"missing proof or dependency"
                                                          : L"invalid generation request";
        fprintf_utf8(stderr, get_messages()->c_omitted, failure->entry->name.data, reason);
        if (failure->dependency)
            fprintf_utf8(stderr, L" (%s)", failure->dependency->name.data);
        else if (failure->failed_node)
            fprintf_utf8(stderr, L" (%s)", failure->failed_node->vtbl->type_name);
        fprintf(stderr, "\n");
    }
    bool success = true;
    if (options->save_c) {
        path_t *path = c_output_path(options->input_file);
        success = path && write_utf8_file(path->full_path, output.source.data);
        if (!success) {
            fprintf_utf8(stderr,
                         get_messages()->cannot_write_c_file,
                         path ? path->normal_path : options->input_file->normal_path);
            fprintf(stderr, "\n");
        }
        free_path(path);
    }
    if (success && options->print_c) {
        print_utf8(output.source.data);
        success = fflush(stdout) == 0 && !ferror(stdout);
    }
    FREE_STRING(output.source);
    return success;
}
