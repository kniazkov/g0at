/**
 * @file options.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of command-line options parsing functions.
 */

#include "options.h"

#include "binary_program.h"
#include "lib/allocate.h"
#include "lib/io.h"
#include "resources/messages.h"

#include <memory.h>
#include <stdio.h>
#include <string.h>

/** @brief Accepts nonempty PNG or SVG output names. */
static bool check_graph_file(const char *filename) {
    if (filename == NULL || *filename == '\0') {
        return false;
    }

    const char *dot = strrchr(filename, '.');
    if (dot == NULL) {
        return false;
    }

    const char *ext = dot + 1;
    if (strcasecmp(ext, "png") == 0 || strcasecmp(ext, "svg") == 0) {
        return true;
    }

    return false;
}

options_t *create_options() {
    options_t *opt = (options_t *)CALLOC(sizeof(options_t));
    opt->script_args = create_vector();
    opt->optimization_level = OPTIMIZATION_ALL;
    return opt;
}

options_t *parse_options(int argc, char **argv) {
    if (argc < 2) {
        fprintf_utf8(stderr, get_messages()->no_input_file);
        return NULL;
    }

    options_t *opt = create_options();

    bool native_explicit = false, optimization_explicit = false;
    for (int index = 1; index < argc; index++) {
        char *arg = argv[index];

        if (strcmp(arg, "/?") == 0) {
            goto help;
        }

        if (arg[0] == '-') {
            if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
                goto help;
            }

            if (strcmp(arg, "-w") == 0 || strcmp(arg, "--enable-warnings") == 0) {
                opt->enable_warnings = true;
                continue;
            }

            if (!strcmp(arg, "--compile")) {
                opt->compile_only = true;
                continue;
            }
            if (!strcmp(arg, "--run")) {
                opt->run_binary = true;
                continue;
            }
            if (strcmp(arg, "--optimize") == 0) {
                optimization_explicit = true;
                if (index + 1 >= argc || !argv[index + 1][0] || argv[index + 1][0] == '-') {
                    fprintf_utf8(stderr, get_messages()->missing_specification, arg);
                    goto error;
                }
                const char *level = argv[++index];
                if (strcmp(level, "none") == 0)
                    opt->optimization_level = OPTIMIZATION_NONE;
                else if (strcmp(level, "all") == 0)
                    opt->optimization_level = OPTIMIZATION_ALL;
                else {
                    fprintf_utf8(stderr, get_messages()->bad_optimization_level, level);
                    goto error;
                }
                continue;
            }

            if (strcmp(arg, "--native") == 0) {
                native_explicit = true;
                if (index + 1 >= argc || !argv[index + 1][0] || argv[index + 1][0] == '-') {
                    fprintf_utf8(stderr, get_messages()->missing_specification, arg);
                    goto error;
                }
                const char *mode = argv[++index];
                if (!strcmp(mode, "off"))
                    opt->native_execution = NATIVE_OFF;
                else if (!strcmp(mode, "auto"))
                    opt->native_execution = NATIVE_AUTO;
                else if (!strcmp(mode, "required"))
                    opt->native_execution = NATIVE_REQUIRED;
                else {
                    fprintf_utf8(stderr, get_messages()->bad_native_mode, mode);
                    goto error;
                }
                continue;
            }
            if (strcmp(arg, "--print-native") == 0) {
                opt->print_native = true;
                continue;
            }
            if (strcmp(arg, "--save-native") == 0) {
                if (index + 1 >= argc || !argv[index + 1][0] || argv[index + 1][0] == '-') {
                    fprintf_utf8(stderr, get_messages()->missing_specification, arg);
                    goto error;
                }
                free_path(opt->native_output_file);
                opt->native_output_file = create_path(argv[++index]);
                continue;
            }

            if (strcmp(arg, "--save-library") == 0) {
                opt->save_library = true;
                continue;
            }

            if (strcmp(arg, "--print-c") == 0 || strcmp(arg, "--save-c") == 0) {
                if (strcmp(arg, "--print-c") == 0)
                    opt->print_c = true;
                else
                    opt->save_c = true;
                continue;
            }

            if (strcmp(arg, "--print-bytecode") == 0) {
                opt->print_bytecode = true;
                continue;
            }

            if (strcmp(arg, "--print-source-code") == 0) {
                opt->print_source_code = true;
                continue;
            }

            if (strcmp(arg, "--print-analysis") == 0) {
                opt->print_analysis = true;
                continue;
            }

            if (strcmp(arg, "--save-analysis") == 0) {
                if (index + 1 >= argc || !argv[index + 1][0] || argv[index + 1][0] == '-') {
                    fprintf_utf8(stderr, get_messages()->missing_specification, arg);
                    goto error;
                }
                free_path(opt->analysis_output_file);
                opt->analysis_output_file = create_path(argv[++index]);
                continue;
            }

            if (strcmp(arg, "--save-graph") == 0) {
                if (index + 1 >= argc || argv[index + 1][0] == '-') {
                    fprintf_utf8(stderr, get_messages()->missing_specification, arg);
                    goto error;
                }
                opt->graph_output_file = create_path(argv[++index]);
                if (strcasecmp(opt->graph_output_file->extension, "png") != 0
                    && strcasecmp(opt->graph_output_file->extension, "svg") != 0) {
                    fprintf_utf8(stderr, get_messages()->bad_graph_file);
                    goto error;
                }
                continue;
            }

            if (strcmp(arg, "-l") == 0 || strcmp(arg, "--lang") == 0
                || strcmp(arg, "--language") == 0) {
                if (index + 1 >= argc || argv[index + 1][0] == '-') {
                    fprintf_utf8(stderr, get_messages()->missing_specification, arg);
                    goto error;
                }
                opt->language = argv[++index];
                continue;
            }

            fprintf_utf8(stderr, get_messages()->unknown_option, arg);
            goto error;
        }

        if (!opt->input_file) {
            opt->input_file = create_path(arg);
        } else {
            append_to_vector(opt->script_args, arg);
        }
    }

    if (!opt->input_file) {
        fprintf_utf8(stderr, get_messages()->no_input_file);
        goto error;
    }

    if ((opt->compile_only && opt->run_binary)
        || ((opt->compile_only || opt->run_binary)
            && (opt->print_c || opt->save_c || opt->save_library))
        || (opt->run_binary
            && (optimization_explicit || opt->print_analysis || opt->analysis_output_file
                || opt->graph_output_file || opt->print_source_code || opt->enable_warnings))) {
        fprintf(stderr, "Incompatible --compile/--run options.\n");
        goto error;
    }
    if (opt->run_binary && !native_explicit)
        opt->native_execution = NATIVE_AUTO;
    if ((opt->print_c || opt->save_c || opt->save_library)
        && (opt->optimization_level == OPTIMIZATION_NONE || opt->print_analysis
            || opt->print_source_code || opt->print_bytecode)) {
        fprintf_utf8(stderr, get_messages()->bad_c_options);
        goto error;
    }
    if ((opt->native_execution != NATIVE_OFF && opt->optimization_level == OPTIMIZATION_NONE)
        || ((opt->print_c || opt->save_c || opt->save_library)
            && (opt->native_execution != NATIVE_OFF || opt->print_native
                || opt->native_output_file))) {
        fprintf_utf8(stderr, get_messages()->bad_native_options);
        goto error;
    }
    if (paths_refer_to_same_file(opt->input_file, opt->native_output_file)) {
        fprintf_utf8(stderr, get_messages()->native_report_conflict);
        goto error;
    }
    if (!binary_program_options_valid(opt))
        goto error;
    return opt;

error:
    destroy_options(opt);
    return NULL;

help:
    fprintf_utf8(stdout, get_messages()->help);
    destroy_options(opt);
    return NULL;
}

void destroy_options(options_t *opt) {
    free_path(opt->input_file);
    free_path(opt->graph_output_file);
    free_path(opt->analysis_output_file);
    free_path(opt->native_output_file);
    destroy_vector(opt->script_args);
    FREE(opt);
}
