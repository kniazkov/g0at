/**
 * @file options.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Definitions and function prototypes for command-line options parsing.
 *
 * This file provides the structures and functions necessary to parse command-line options
 * provided by the user. It defines the structure for holding parsed options and provides
 * functionality for reading, validating, and storing those options.
 */

#pragma once

#include <stdbool.h>

/**
 * @struct options_t
 * @brief Structure for storing parsed command-line options.
 * 
 * This structure is used to hold the options provided by the user through
 * the command line.
 */
typedef struct {
    /**
     * @brief Path to the input file.
     */
    const char *input_file;
} options_t;

/**
 * @brief Parses command-line options and populates the provided options structure.
 * 
 * This function processes the command-line arguments passed to the program, handling options 
 * and storing the parsed results  in the provided `options_t` structure. If an invalid or unknown
 * option is encountered, or if the required input file is missing, an error message is printed
 * to `stderr`.
 * 
 * Currently, only one option (`input_file`) is supported. Additional options may be 
 * added in the future, so this function serves as a placeholder for future expansion.
 * 
 * @param argc The number of command-line arguments.
 * @param argv The command-line arguments array.
 * @param opt A pointer to the `options_t` structure where the parsed options will be stored.
 * 
 * @return `true` if the parsing was successful, `false` if an error occurred (e.g., unknown option,
 *         missing input file).
 */
bool parse_options(int argc, char** argv, options_t *opt);
