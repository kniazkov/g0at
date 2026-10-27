/** @file unicode_generator_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Unicode generation, read-only checks and malformed-input rejection.
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/registry.h"

#include <stdlib.h>
#include <string.h>

static const char *outputs[] = {"lib/strings/unicode_data.h",
                                "third_party/unicode/17.0.0/SHA256SUMS"};
static const char *inputs[] = {"DerivedCoreProperties.txt",
                               "PropList.txt",
                               "SpecialCasing.txt",
                               "UnicodeData.txt"};
#define OUTPUT_COUNT (sizeof(outputs) / sizeof(*outputs))
#define INPUT_COUNT (sizeof(inputs) / sizeof(*inputs))

typedef struct {
    const char *generator;
    const char *root;
    const char *mode;
} settings_t;

typedef struct {
    unsigned char *bytes;
    size_t size;
    fs_info_t info;
} snapshot_t;

static bool release(void *memory) {
    FREE(memory);
    return true;
}

static void *owned(scenario_run_t *run, void *memory) {
    scenario_require(run, memory != NULL, "allocate test resource");
    scenario_defer(run, release, memory);
    return memory;
}

static char *path(scenario_run_t *run, const char *root, const char *relative) {
    fs_error_t error = {0};
    char *result = join_fs_path(root, relative, &error);
    clear_fs_error(&error);
    return owned(run, result);
}

static snapshot_t read_file(scenario_run_t *run, const char *name) {
    snapshot_t result = {0};
    fs_error_t error = {0};
    bool ok =
        read_fs_regular_file(name, 4 * 1024 * 1024, (void **)&result.bytes, &result.size, &error);
    clear_fs_error(&error);
    scenario_require(run, ok, name);
    owned(run, result.bytes);
    ok = inspect_fs_path(name, false, &result.info, &error);
    clear_fs_error(&error);
    scenario_require(run, ok, "inspect snapshot identity");
    return result;
}

static void write_file(scenario_run_t *run, const char *name, const void *bytes, size_t size) {
    FILE *file = FOPEN(name, "wb");
    scenario_require(run, file != NULL, name);
    bool ok = fwrite(bytes, 1, size, file) == size;
    if (fclose(file))
        ok = false;
    scenario_require(run, ok, "write isolated input");
}

static void directory(scenario_run_t *run, const char *root, const char *relative) {
    char *name = path(run, root, relative);
    fs_error_t error = {0};
    bool ok = create_fs_directory(name, &error);
    clear_fs_error(&error);
    scenario_require(run, ok, "create isolated output directory");
}

static void command(scenario_run_t *run,
                    const settings_t *settings,
                    const char *root,
                    bool check,
                    int expected) {
    const char *args[] = {settings->generator,
                          "--root",
                          root,
                          check ? "--check" : "--update",
                          NULL};
    child_options_t options = {0};
    options.executable = settings->generator;
    options.arguments = args;
    options.empty_stdin = true;
    scenario_command(run, &options, expected);
}

static void unchanged(scenario_run_t *run, const char *name, const snapshot_t *before) {
    snapshot_t after = read_file(run, name);
    scenario_require_bytes(run, name, before->bytes, before->size, after.bytes, after.size, false);
    scenario_require(run,
                     before->info.device == after.info.device
                         && before->info.identity == after.info.identity,
                     "--check must not replace a file");
}

static void compare_outputs(scenario_run_t *run, const settings_t *settings, const char *root) {
    for (size_t i = 0; i < OUTPUT_COUNT; ++i) {
        snapshot_t expected = read_file(run, path(run, settings->root, outputs[i]));
        snapshot_t actual = read_file(run, path(run, root, outputs[i]));
        /* Windows Git checkouts may contain CRLF; generated artifacts are always LF. */
        scenario_require_bytes(run,
                               outputs[i],
                               expected.bytes,
                               expected.size,
                               actual.bytes,
                               actual.size,
                               true);
    }
}

static void remove_file(scenario_run_t *run, const char *name) {
    fs_error_t error = {0};
    bool ok = remove_fs_entry(name, &error);
    clear_fs_error(&error);
    scenario_require(run, ok, "remove isolated file");
}

static void absent(scenario_run_t *run, const char *name) {
    fs_info_t info;
    fs_error_t error = {0};
    bool exists = inspect_fs_path(name, false, &info, &error);
    clear_fs_error(&error);
    scenario_require(run, !exists, "command must not create missing file");
}

static void test(scenario_run_t *run, void *user) {
    const settings_t *settings = user;
    const char *work = scenario_work_directory(scenario_run_context(run));
    char *source_names[INPUT_COUNT];
    snapshot_t source_before[INPUT_COUNT];
    char *ucd = path(run, settings->root, "third_party/unicode/17.0.0");
    for (size_t i = 0; i < INPUT_COUNT; ++i) {
        source_names[i] = path(run, ucd, inputs[i]);
        source_before[i] = read_file(run, source_names[i]);
    }
    if (!strcmp(settings->mode, "check")) {
        snapshot_t before[OUTPUT_COUNT];
        char *names[OUTPUT_COUNT];
        for (size_t i = 0; i < OUTPUT_COUNT; ++i) {
            names[i] = path(run, settings->root, outputs[i]);
            before[i] = read_file(run, names[i]);
        }
        command(run, settings, settings->root, true, 0);
        for (size_t i = 0; i < OUTPUT_COUNT; ++i)
            unchanged(run, names[i], &before[i]);
    } else {
        directory(run, work, "lib");
        directory(run, work, "lib/strings");
        directory(run, work, "third_party");
        directory(run, work, "third_party/unicode");
        directory(run, work, "third_party/unicode/17.0.0");
        char *work_ucd = path(run, work, "third_party/unicode/17.0.0");
        char *input_names[INPUT_COUNT];
        for (size_t i = 0; i < INPUT_COUNT; ++i) {
            input_names[i] = path(run, work_ucd, inputs[i]);
            write_file(run, input_names[i], source_before[i].bytes, source_before[i].size);
        }
        command(run, settings, work, false, 0);
        compare_outputs(run, settings, work);
        char *names[OUTPUT_COUNT];
        for (size_t i = 0; i < OUTPUT_COUNT; ++i)
            names[i] = path(run, work, outputs[i]);
        if (!strcmp(settings->mode, "roundtrip")) {
            snapshot_t before[OUTPUT_COUNT];
            for (size_t i = 0; i < OUTPUT_COUNT; ++i)
                before[i] = read_file(run, names[i]);
            command(run, settings, work, true, 0);
            command(run, settings, work, true, 0);
            for (size_t i = 0; i < OUTPUT_COUNT; ++i)
                unchanged(run, names[i], &before[i]);
            command(run, settings, work, false, 0);
            compare_outputs(run, settings, work);
        } else if (!strcmp(settings->mode, "stale") || !strcmp(settings->mode, "missing")) {
            bool missing = !strcmp(settings->mode, "missing");
            for (size_t damaged = 0; damaged < OUTPUT_COUNT; ++damaged) {
                snapshot_t before[OUTPUT_COUNT];
                if (missing)
                    remove_file(run, names[damaged]);
                else {
                    static const char corrupt[] = "stale generated output\n";
                    write_file(run, names[damaged], corrupt, sizeof(corrupt) - 1);
                }
                for (size_t i = 0; i < OUTPUT_COUNT; ++i)
                    if (!missing || i != damaged)
                        before[i] = read_file(run, names[i]);
                command(run, settings, work, true, 1);
                for (size_t i = 0; i < OUTPUT_COUNT; ++i)
                    if (!missing || i != damaged)
                        unchanged(run, names[i], &before[i]);
                if (missing)
                    absent(run, names[damaged]);
                command(run, settings, work, false, 0);
                compare_outputs(run, settings, work);
            }
        } else {
            snapshot_t before[OUTPUT_COUNT];
            for (size_t i = 0; i < OUTPUT_COUNT; ++i)
                before[i] = read_file(run, names[i]);
            if (!strcmp(settings->mode, "missing-input")) {
                /* Every required input must fail closed, in both modes. */
                for (size_t removed = 0; removed < INPUT_COUNT; ++removed) {
                    remove_file(run, input_names[removed]);
                    command(run, settings, work, true, 1);
                    command(run, settings, work, false, 1);
                    absent(run, input_names[removed]);
                    for (size_t i = 0; i < OUTPUT_COUNT; ++i)
                        unchanged(run, names[i], &before[i]);
                    write_file(run,
                               input_names[removed],
                               source_before[removed].bytes,
                               source_before[removed].size);
                }
            } else {
                size_t damaged = 0;
                const char *invalid = NULL;
                if (!strcmp(settings->mode, "reject-unicode")) {
                    damaged = 3;
                    invalid = "0041;INVALID;Lu\n";
                } else if (!strcmp(settings->mode, "reject-special")) {
                    damaged = 2;
                    invalid = "0041; 110000; 0041; 0041; # invalid mapping\n";
                } else if (!strcmp(settings->mode, "reject-property")) {
                    damaged = 0;
                    invalid = "0042..0041 ; Cased # reversed range\n";
                }
                scenario_require(run, invalid != NULL, "recognized negative scenario");
                write_file(run, input_names[damaged], invalid, strlen(invalid));
                snapshot_t bad_input = read_file(run, input_names[damaged]);
                command(run, settings, work, true, 1);
                command(run, settings, work, false, 1);
                for (size_t i = 0; i < OUTPUT_COUNT; ++i)
                    unchanged(run, names[i], &before[i]);
                unchanged(run, input_names[damaged], &bad_input);
            }
        }
    }
    for (size_t i = 0; i < INPUT_COUNT; ++i)
        unchanged(run, source_names[i], &source_before[i]);
}

int main(int argc, char **argv) {
    if (argc != 5 && argc != 6) {
        fprintf(stderr,
                "usage: %s scenario generator source-root build-dir [--keep-work]\n",
                argv[0]);
        return 1;
    }
    settings_t settings = {argv[2], argv[3], argv[1]};
    const char *ids[] = {"check",
                         "roundtrip",
                         "stale",
                         "missing",
                         "missing-input",
                         "reject-unicode",
                         "reject-special",
                         "reject-property"};
    scenario_definition_t definitions[sizeof(ids) / sizeof(*ids)];
    for (size_t i = 0; i < sizeof(ids) / sizeof(*ids); ++i)
        definitions[i] = (scenario_definition_t){ids[i], 30000, test, &settings, NULL};
    fs_error_t error = {0};
    char *data = join_fs_path(settings.root, "test/data", &error);
    clear_fs_error(&error);
    if (!data)
        return 1;
    scenario_options_t options = {NULL, argv[4], data, false, stderr};
    const char *selection[] = {argv[1], argc == 6 ? argv[5] : NULL};
    int result = scenario_registry_main(definitions,
                                        sizeof(ids) / sizeof(*ids),
                                        argc == 6 ? 2 : 1,
                                        selection,
                                        &options);
    FREE(data);
    return result;
}
