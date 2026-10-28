/** @file unicode_interactive_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Unicode, character and redirected interactive scenario entry points.
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/registry.h"

#include <string.h>
void characters_cases(scenario_run_t *, const char *);
void unicode_cases(scenario_run_t *, const char *);
void interactive_cases(scenario_run_t *, const char *);

static void characters_body(scenario_run_t *run, void *exe) {
    characters_cases(run, exe);
}

static void unicode_body(scenario_run_t *run, void *exe) {
    unicode_cases(run, exe);
}

static void interactive_body(scenario_run_t *run, void *exe) {
    interactive_cases(run, exe);
}

int main(int argc, char **argv) {
#ifdef _WIN32
    fs_error_t arguments_error = {0};
    if (!fs_arguments(&argc, &argv, &arguments_error)) {
        clear_fs_error(&arguments_error);
        return 1;
    }
#endif
    int result = 1;
    if (argc == 2 && !strcmp(argv[1], "--list")) {
        puts("native.characters\nnative.unicode\nnative.interactive");
        result = 0;
    } else if (argc == 5 || argc == 6) {
        fs_error_t error = {0};
        char *exe = absolute_fs_path(argv[2], &error);
        clear_fs_error(&error);
        if (exe) {
            scenario_definition_t definitions[] = {
                {"native.characters", 180000, characters_body, exe, NULL},
                {"native.unicode", 180000, unicode_body, exe, NULL},
                {"native.interactive", 180000, interactive_body, exe, NULL}};
            scenario_options_t options = {.build_directory = argv[3], .data_directory = argv[4]};
            const char *selection[] = {argv[1], argc == 6 ? argv[5] : NULL};
            result = scenario_registry_main(definitions, 3, argc == 6 ? 2 : 1, selection, &options);
            FREE(exe);
        }
    } else
        fputs("usage: unicode_interactive_testing "
              "native.characters|native.unicode|native.interactive GOAT BUILD DATA [--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
