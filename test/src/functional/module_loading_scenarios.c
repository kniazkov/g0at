/** @file module_loading_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent real lazy-loading and cleaner lifecycle scenarios (#369).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "module_loading_cases.h"
#include "module_loading_lifecycle.h"
#include "test/support/registry.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *runner, *builder, *compiler, *include, *schema;
    size_t index;
    bool lifecycle;
} config_t;

static void run_case(scenario_run_t *r, void *arg) {
    config_t *c = arg;
    loading_fixture_t *f =
        loading_fixture_create(r, c->builder, c->compiler, c->include, c->schema);
    if (c->lifecycle)
        loading_lifecycle_case_run(r, f, c->runner, c->index);
    else
        loading_normal_case_run(r, f, c->runner, c->index);
}

int main(int argc, char **argv) {
#ifdef _WIN32
    fs_error_t ae = {0};
    if (!fs_arguments(&argc, &argv, &ae)) {
        clear_fs_error(&ae);
        return 1;
    }
#endif
    int result = 1;
    if (argc == 9 || argc == 10) {
        fs_error_t e = {0};
        char *runner = absolute_fs_path(argv[2], &e);
        clear_fs_error(&e);
        char *builder = absolute_fs_path(argv[3], &e);
        clear_fs_error(&e);
        size_t normal = loading_normal_case_count(),
               count = normal + loading_lifecycle_case_count();
        scenario_definition_t *defs = calloc(count, sizeof(*defs));
        config_t *config = calloc(count, sizeof(*config));
        char(*ids)[128] = calloc(count, sizeof(*ids));
        if (runner && builder && defs && config && ids) {
            for (size_t i = 0; i < count; i++) {
                bool life = i >= normal;
                size_t ix = life ? i - normal : i;
                snprintf(ids[i],
                         sizeof(ids[i]),
                         "integration.module_loading.%s",
                         life ? loading_lifecycle_case_name(ix) : loading_normal_case_name(ix));
                config[i] = (config_t){runner, builder, argv[4], argv[7], argv[8], ix, life};
                defs[i] = (scenario_definition_t){ids[i], 300000, run_case, config + i, NULL};
            }
            scenario_options_t options = {.build_directory = argv[5], .data_directory = argv[6]};
            const char *args[] = {argv[1], argc == 10 ? argv[9] : NULL};
            result = scenario_registry_main(defs, count, argc == 10 ? 2 : 1, args, &options);
        }
        FREE(runner);
        FREE(builder);
        free(defs);
        free(config);
        free(ids);
    } else
        fputs("usage: module_loading_scenarios ID RUNNER BUILDER CC BUILD DATA INCLUDE SCHEMA "
              "[--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
