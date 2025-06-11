/**
 * @file options.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of command-line options parsing functions.
 *
 * This file contains the implementation of the functions responsible for parsing the
 * command-line options provided by the user. It processes the input arguments, validates
 * them, and stores the parsed options in a structured format.
 */

#include <stdio.h>
#include <string.h>
#include <memory.h>

#include "options.h"
#include "lib/allocate.h"
#include "lib/io.h"
#include "resources/messages.h"

/**
 * @brief Checks if the graph output filename has a valid extension and format.
 * This function validates that the provided filename:
 * - Ends with either .png or .svg extension
 * - Is not an empty string
 * @param filename The filename to validate
 * @return true if filename is valid, false otherwise
 */
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
    return opt;
}

options_t *parse_options(int argc, char **argv) {
    if (argc < 2) {
        fprintf_utf8(stderr, get_messages()->no_input_file);
        return NULL;
    }

    options_t *opt = create_options();
    bool input_file_found = false;

    for (int index = 1; index < argc; index++) {
        char *arg = argv[index];
        if (arg[0] == '-') {
            if (strcmp(arg, "--print-bytecode") == 0) {
                opt->print_bytecode = true;
                continue;
            }

            if (strcmp(arg, "--print-source-code") == 0) {
                opt->print_source_code = true;
                continue;
            }

            if (strcmp(arg, "--print-graph") == 0) {
                if (index + 1 >= argc || argv[index + 1][0] == '-') {
                    fprintf_utf8(stderr, get_messages()->missing_specification, arg);
                    goto error;
                }
                const char *filename = argv[++index];
                if (!check_graph_file(filename)) {
                    fprintf_utf8(stderr, get_messages()->bad_graph_file);
                    goto error;
                }
                opt->graph_output_file = filename;
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

        if (!input_file_found) {
            opt->input_file = arg;
            input_file_found = true;
        } else {
            append_to_vector(opt->script_args, arg);
        }
    }

    if (!opt->input_file) {
        fprintf_utf8(stderr, get_messages()->no_input_file);
        goto error;
    }

    return opt;

error:
    destroy_options(opt);
    return NULL;
}

void destroy_options(options_t *opt) {
    destroy_vector(opt->script_args);
    FREE(opt);
}
