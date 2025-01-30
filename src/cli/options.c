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
#include <memory.h>

#include "options.h"
#include "lib/io.h"
#include "resources/messages.h"

bool parse_options(int argc, char** argv, options_t *opt) {
    memset(opt, 0, sizeof(options_t));
    int index = 0;
    while(index < argc) {
        char *arg = argv[index];
        if (arg[0] == '-') {
            fprintf_utf8(stderr, get_messages()->unknown_option, arg);
            return false;
        }
        else {
            if (opt->input_file != NULL) {
                fprintf_utf8(stderr, get_messages()->unknown_option, arg);
                return false;
            }
            opt->input_file = arg;
        }
        index++;
    }
    if (!opt->input_file) {
        fprintf_utf8(stderr, get_messages()->no_input_file);
        return false;
    }
    return true;
}
