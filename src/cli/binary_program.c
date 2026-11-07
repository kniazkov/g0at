/** @file binary_program.c
 * @brief Validate compiled output paths before starting compilation.
 */
#include "binary_program.h"

#include "c_output.h"
#include "lib/files/file_system.h"
#include "lib/files/io.h"

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
    bool ok = !paths_refer_to_same_file(options->input_file, options->analysis_output_file)
              && !paths_refer_to_same_file(options->input_file, options->graph_output_file)
              && (!options->compile_only || !conflicts(options, binary));
    free_path(binary);
    if (!ok)
        FPRINTF(stderr, "Compiled output conflicts with input or a report.\n");
    return ok;
}
