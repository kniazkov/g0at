/** @file test_native_options.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Modes, defaults, repeated options and reports.
 */
#include "test_native_options.h"

#include "cli/options.h"
#include "test_macro.h"

#include <stdio.h>
#include <string.h>

bool test_native_options(void) {
    options_t *options = create_options();
    ASSERT(options->native_execution == NATIVE_OFF && !options->print_native
           && !options->native_output_file);
    ASSERT(options->native_probe_timeout == 10);
    destroy_options(options);
    char *modes[] = {"off", "auto", "required"};
    for (size_t i = 0; i < 3; i++) {
        char *args[] = {"goat",
                        "--native",
                        modes[i],
                        "--print-native",
                        "--save-native",
                        "first.txt",
                        "--save-native",
                        "second.txt",
                        "test.goat"};
        options = parse_options(9, args);
        ASSERT(options && options->native_execution == (native_execution_mode_t)i
               && options->print_native);
        ASSERT(!strcmp(options->native_output_file->file_name, "second.txt"));
        destroy_options(options);
    }
    char *args[] =
        {"goat", "--native", "required", "--native", "off", "--optimize", "none", "test.goat"};
    options = parse_options(8, args);
    ASSERT(options && options->native_execution == NATIVE_OFF
           && options->optimization_level == OPTIMIZATION_NONE);
    destroy_options(options);
    char *timeout_args[] = {"goat", "--native-probe-timeout", "4294967295", "test.goat"};
    options = parse_options(4, timeout_args);
    ASSERT(options && options->native_probe_timeout == UINT32_MAX);
    destroy_options(options);
    char *build_args[] = {"goat",
                          "build",
                          "app.gmod",
                          "--module-dir",
                          "first",
                          "--module-dir",
                          "output",
                          "--native-target",
                          "host",
                          "--native",
                          "off"};
    options = parse_options(11, build_args);
    ASSERT(options && options->build_module && options->compile_only && options->native_explicit
           && options->native_execution == NATIVE_OFF);
    ASSERT(options->module_directory && !strcmp(options->module_directory->normal_path, "output"));
    ASSERT(!strcmp(options->native_target, "host"));
    destroy_options(options);
    char *run_args[] = {"goat", "run", "app.gmod", "--", "--native", "required", "build"};
    options = parse_options(7, run_args);
    ASSERT(options && options->run_command && !options->run_binary && !options->native_explicit
           && options->script_args->size == 3);
    ASSERT(!strcmp(options->script_args->data[0], "--native"));
    destroy_options(options);
    char *literal_args[] = {"goat", "--", "build", "--module-dir"};
    options = parse_options(4, literal_args);
    ASSERT(options && !options->build_module && !strcmp(options->input_file->file_name, "build")
           && options->script_args->size == 1);
    destroy_options(options);
    char *list_args[] =
        {"goat", "--list-native-targets", "--cc", "compiler", "--native-probe-timeout", "2"};
    options = parse_options(6, list_args);
    ASSERT(options && options->list_native_targets && !options->input_file
           && !strcmp(options->native_compiler, "compiler") && options->native_probe_timeout == 2);
    destroy_options(options);
    char *invalid[][5] = {
        {"goat", "build", "main.goat"},
        {"goat", "build", "run", "app.gmod"},
        {"goat", "run", "--compile", "app.gmod"},
        {"goat", "--list-native-targets", "app.gmod"},
        {"goat", "--list-native-targets", "--native", "off"},
        {"goat", "--native-target", "host", "main.goat"},
        {"goat", "--module-dir"},
        {"goat", "--native-target", ""},
        {"goat", "--interactive", "--module-dir", "modules"},
    };
    const int lengths[] = {3, 4, 4, 3, 4, 4, 2, 3, 4};
    for (size_t i = 0; i < sizeof(lengths) / sizeof(*lengths); i++) {
        options = parse_options(lengths[i], invalid[i]);
        ASSERT(!options);
    }
    return true;
}
