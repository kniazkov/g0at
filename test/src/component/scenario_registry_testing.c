/** @file scenario_registry_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independently supervised registry validation and selection contracts.
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/registry.h"

#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "Failed line %d: %s\n", __LINE__, #condition);                         \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

typedef struct {
    unsigned calls;
    unsigned completed;
    bool fail;
    char *work;
    const char *expected_name;
} state_t;

static void body(scenario_run_t *run, void *user) {
    state_t *state = user;
    state->calls++;
    scenario_context_t *context = scenario_run_context(run);
    const char *work = scenario_work_directory(context);
    state->work = malloc(strlen(work) + 1);
    scenario_require(run, state->work != NULL, "copy work path");
    strcpy(state->work, work);
    scenario_require(run, !strcmp(scenario_name(context), state->expected_name), "selected ID");
    scenario_require(run, !state->fail, "intentional registry test failure");
    state->completed++;
}

static bool contains(FILE *file, const char *text) {
    char buffer[8192];
    if (fflush(file) || fseek(file, 0, SEEK_SET))
        return false;
    size_t size = fread(buffer, 1, sizeof(buffer) - 1, file);
    buffer[size] = 0;
    bool found = strstr(buffer, text) != NULL;
    return !fseek(file, 0, SEEK_END) && found;
}

static bool remove_work(const char *path) {
    fs_error_t error = {0};
    bool ok = remove_fs_tree(path, &error);
    clear_fs_error(&error);
    return ok;
}

static bool exists(const char *path) {
    fs_error_t error = {0};
    fs_info_t info;
    bool found = inspect_fs_path(path, false, &info, &error);
    clear_fs_error(&error);
    return found;
}

static int validation(FILE *out) {
    scenario_definition_t good = {"alpha.case-1_2", 1000, body, NULL, NULL};
    CHECK(scenario_registry_validate(&good, 1, out));
    CHECK(!scenario_registry_validate(NULL, 1, out));
    CHECK(!scenario_registry_validate(&good, 0, out));
    const char *invalid[] = {NULL, "", "2alpha", "../alpha", "a/b", "a b", "a\n", "a\xC3\xA9"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
        scenario_definition_t entry = good;
        entry.id = invalid[i];
        CHECK(!scenario_registry_validate(&entry, 1, out));
    }
    scenario_definition_t duplicate[] = {good, good};
    CHECK(!scenario_registry_validate(duplicate, 2, out));
    CHECK(contains(out, "Duplicate scenario ID: alpha.case-1_2"));
    scenario_definition_t bad = good;
    bad.timeout_ms = 0;
    CHECK(!scenario_registry_validate(&bad, 1, out));
    bad = good;
    bad.body = NULL;
    CHECK(!scenario_registry_validate(&bad, 1, out));
    bad.skip_reason = "";
    CHECK(!scenario_registry_validate(&bad, 1, out));
    bad.skip_reason = "platform capability unavailable";
    CHECK(scenario_registry_validate(&bad, 1, out));
    return 0;
}

static int test(const char *mode, scenario_options_t options) {
    FILE *out = tmpfile();
    CHECK(out);
    options.diagnostics = out;
    state_t alpha = {0, 0, false, NULL, "alpha"};
    state_t beta = {0, 0, false, NULL, "beta"};
    scenario_definition_t definitions[] = {
        {"alpha", 5000, body, &alpha, NULL},
        {"beta", 5000, body, &beta, NULL},
        {"optional", 5000, NULL, NULL, "optional fixture capability unavailable"},
    };
    const char *args[] = {"beta", NULL};
    if (!strcmp(mode, "validation")) {
        CHECK(!validation(out));
    } else if (!strcmp(mode, "selection") || !strcmp(mode, "keep-work")) {
        bool keep = !strcmp(mode, "keep-work");
        args[1] = "--keep-work";
        CHECK(!scenario_registry_main(definitions, 3, keep ? 2 : 1, args, &options));
        CHECK(!alpha.calls && beta.calls == 1 && beta.completed == 1);
        CHECK(beta.work && exists(beta.work) == keep);
        if (keep)
            CHECK(remove_work(beta.work));
    } else if (!strcmp(mode, "unknown")) {
        args[0] = "missing";
        CHECK(scenario_registry_main(definitions, 3, 1, args, &options) == 1);
        CHECK(contains(out, "Unknown scenario ID: missing"));
        args[0] = "alpha";
        args[1] = "--typo";
        CHECK(scenario_registry_main(definitions, 3, 2, args, &options) == 1);
        CHECK(scenario_registry_main(definitions, 3, 0, NULL, &options) == 1);
        CHECK(scenario_registry_main(definitions, 3, 3, args, &options) == 1);
        CHECK(scenario_registry_main(definitions, 3, 1, args, NULL) == 1);
        scenario_definition_t duplicate[] = {definitions[0], definitions[0]};
        CHECK(scenario_registry_main(duplicate, 2, 1, args, &options) == 1);
        CHECK(!alpha.calls && !beta.calls);
    } else if (!strcmp(mode, "list")) {
        args[0] = "--list";
        CHECK(!scenario_registry_main(definitions, 3, 1, args, &options));
        CHECK(contains(out, "alpha\nbeta\noptional\n"));
        CHECK(scenario_registry_main(definitions, 3, 2, args, &options) == 1);
        CHECK(!alpha.calls && !beta.calls);
    } else if (!strcmp(mode, "skip")) {
        args[0] = "optional";
        CHECK(scenario_registry_main(definitions, 3, 1, args, &options) == 77);
        CHECK(contains(out, "SKIP optional: optional fixture capability unavailable"));
        CHECK(!alpha.calls && !beta.calls);
    } else if (!strcmp(mode, "continuation")) {
        alpha.fail = true;
        args[0] = "alpha";
        CHECK(scenario_registry_main(definitions, 3, 1, args, &options) == 1);
        CHECK(alpha.calls == 1 && !alpha.completed && alpha.work && exists(alpha.work));
        args[0] = "beta";
        CHECK(!scenario_registry_main(definitions, 3, 1, args, &options));
        CHECK(beta.calls == 1 && beta.completed == 1);
        CHECK(remove_work(alpha.work));
    } else {
        CHECK(false);
    }
    free(alpha.work);
    free(beta.work);
    CHECK(!fclose(out));
    return 0;
}

static void fixture_body(scenario_run_t *run, void *user) {
    scenario_require(run, user == NULL, "intentional fixture failure");
}

static int run(int argc, char **argv) {
    if (argc == 5 && !strcmp(argv[1], "--worker")) {
        scenario_options_t options = {"registry-worker", argv[3], argv[4], false, stderr};
        return test(argv[2], options);
    }
    int fail = 1;
    scenario_definition_t fixtures[] = {
        {"fixture-pass", 1000, fixture_body, NULL, NULL},
        {"fixture-fail", 1000, fixture_body, &fail, NULL},
        {"explicit-skip",
         1000,
         NULL,
         NULL,
         "intentional skip fixture: optional capability unavailable"},
    };
    if (argc == 2 && !strcmp(argv[1], "--list")) {
        scenario_options_t listing = {0};
        listing.diagnostics = stdout;
        return scenario_registry_main(fixtures, 3, 1, (const char *const *)(argv + 1), &listing);
    }
    if (argc != 4)
        return 2;
    scenario_options_t options = {argv[1], argv[2], argv[3], false, stderr};
    if (!strcmp(argv[1], "fixture-pass") || !strcmp(argv[1], "fixture-fail")
        || !strcmp(argv[1], "explicit-skip"))
        return scenario_registry_main(fixtures, 3, 1, (const char *const *)(argv + 1), &options);
    const char *modes[] =
        {"validation", "selection", "unknown", "list", "skip", "continuation", "keep-work"};
    bool known = false;
    for (size_t i = 0; i < sizeof(modes) / sizeof(*modes); ++i)
        known = known || !strcmp(argv[1], modes[i]);
    if (!known)
        return scenario_registry_main(fixtures, 3, 1, (const char *const *)(argv + 1), &options);
    const char *args[] = {argv[0], "--worker", argv[1], argv[2], argv[3], NULL};
    child_options_t host = {0};
    host.arguments = args;
    return scenario_supervise(&options, 10000, &host) ? 0 : 1;
}

int main(int argc, char **argv) {
    fs_error_t error = {0};
    if (!fs_arguments(&argc, &argv, &error)) {
        clear_fs_error(&error);
        return 2;
    }
    /* The supervised child starts in its owned workspace, not the caller's cwd.
     * Resolve input directories before launch; NULL child.executable relaunches this
     * executable independently of argv[0] and its original spelling.
     */
    char *build = NULL;
    char *data = NULL;
    char *original_build = argc == 4 ? argv[2] : NULL;
    char *original_data = argc == 4 ? argv[3] : NULL;
    int result;
    if (argc == 4) {
        build = canonical_fs_path(original_build, &error);
        data = build ? canonical_fs_path(original_data, &error) : NULL;
        if (build && data) {
            argv[2] = build;
            argv[3] = data;
            result = run(argc, argv);
            argv[2] = original_build;
            argv[3] = original_data;
        } else {
            fprintf(stderr, "Cannot resolve scenario build/data directories\n");
            result = 2;
        }
    } else {
        result = run(argc, argv);
    }
    FREE(build);
    FREE(data);
    free_fs_arguments(argc, argv);
    clear_fs_error(&error);
    return result;
}
