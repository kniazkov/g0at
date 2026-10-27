/** @file module_schema_generator_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Native schema generation, no-write checks and unsupported-input rejection.
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/registry.h"

#include <stdlib.h>
#include <string.h>

static const char *outputs[] = {"docs/module-format-v1/gmod.schema.json",
                                "docs/module-format-v1/module.schema.json",
                                "docs/module-format-v1/target.schema.json",
                                "docs/module-format-v1/native.schema.json",
                                "src/module/module_json_schema.h"};
#define OUTPUT_COUNT (sizeof(outputs) / sizeof(*outputs))

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
    bool ok = read_fs_regular_file(name, 1024 * 1024, (void **)&result.bytes, &result.size, &error);
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
                    const char *source,
                    bool check,
                    int expected) {
    const char *args[] =
        {settings->generator, "--root", root, "--source", source, check ? "--check" : NULL, NULL};
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

static char *replace(scenario_run_t *run,
                     const snapshot_t *source,
                     const char *needle,
                     const char *replacement) {
    const char *start = (const char *)source->bytes;
    const char *found = strstr(start, needle);
    scenario_require(run, found != NULL, "mutation must match the reviewed source");
    size_t prefix = (size_t)(found - start);
    size_t size = source->size - strlen(needle) + strlen(replacement);
    char *result = owned(run, ALLOC(size + 1));
    memcpy(result, start, prefix);
    memcpy(result + prefix, replacement, strlen(replacement));
    memcpy(result + prefix + strlen(replacement),
           found + strlen(needle),
           source->size - prefix - strlen(needle));
    result[size] = 0;
    return result;
}

static void test(scenario_run_t *run, void *user) {
    const settings_t *settings = user;
    const char *work = scenario_work_directory(scenario_run_context(run));
    char *source = path(run, settings->root, "docs/module-format-v1/schema-source.json");
    snapshot_t input = read_file(run, source);
    if (!strcmp(settings->mode, "check")) {
        snapshot_t before[OUTPUT_COUNT];
        char *names[OUTPUT_COUNT];
        for (size_t i = 0; i < OUTPUT_COUNT; ++i) {
            names[i] = path(run, settings->root, outputs[i]);
            before[i] = read_file(run, names[i]);
        }
        command(run, settings, settings->root, source, true, 0);
        for (size_t i = 0; i < OUTPUT_COUNT; ++i)
            unchanged(run, names[i], &before[i]);
        unchanged(run, source, &input);
        return;
    }
    directory(run, work, "docs");
    directory(run, work, "docs/module-format-v1");
    directory(run, work, "src");
    directory(run, work, "src/module");
    command(run, settings, work, source, false, 0);
    compare_outputs(run, settings, work);
    if (!strcmp(settings->mode, "roundtrip")) {
        command(run, settings, work, source, true, 0);
        command(run, settings, work, source, false, 0);
        compare_outputs(run, settings, work);
    } else if (!strcmp(settings->mode, "stale") || !strcmp(settings->mode, "missing")) {
        bool missing = !strcmp(settings->mode, "missing");
        for (size_t damaged = 0; damaged < OUTPUT_COUNT; ++damaged) {
            char *names[OUTPUT_COUNT];
            snapshot_t before[OUTPUT_COUNT];
            for (size_t i = 0; i < OUTPUT_COUNT; ++i)
                names[i] = path(run, work, outputs[i]);
            fs_error_t error = {0};
            if (missing) {
                bool ok = remove_fs_entry(names[damaged], &error);
                clear_fs_error(&error);
                scenario_require(run, ok, "remove isolated generated file");
            } else {
                static const char corrupt[] = "stale generated file\n";
                write_file(run, names[damaged], corrupt, sizeof(corrupt) - 1);
            }
            for (size_t i = 0; i < OUTPUT_COUNT; ++i)
                if (!missing || i != damaged)
                    before[i] = read_file(run, names[i]);
            command(run, settings, work, source, true, 1);
            for (size_t i = 0; i < OUTPUT_COUNT; ++i)
                if (!missing || i != damaged)
                    unchanged(run, names[i], &before[i]);
            if (missing) {
                fs_info_t info;
                bool exists = inspect_fs_path(names[damaged], false, &info, &error);
                clear_fs_error(&error);
                scenario_require(run, !exists, "--check must not create missing files");
            }
            command(run, settings, work, source, false, 0);
            compare_outputs(run, settings, work);
        }
    } else {
        const char *needle = "\"type\": \"object\"";
        const char *replacement = NULL;
        if (!strcmp(settings->mode, "reject-keyword"))
            replacement = "\"type\": \"object\", \"unsupported\": true";
        else if (!strcmp(settings->mode, "reject-pattern"))
            replacement = "\"type\": \"object\", \"pattern\": \"unsupported\"";
        else if (!strcmp(settings->mode, "reject-type"))
            replacement = "\"type\": \"unknown\"";
        else if (!strcmp(settings->mode, "reject-ref")) {
            needle = "#/$defs/facts";
            replacement = "#/$defs/not-defined";
        } else if (!strcmp(settings->mode, "reject-float")) {
            needle = "\"minLength\": 1";
            replacement = "\"minLength\": 1.0";
        } else if (!strcmp(settings->mode, "reject-duplicate"))
            replacement = "\"type\": \"object\", \"type\": \"object\"";
        else {
            needle = "\"DEFS\": {";
            if (!strcmp(settings->mode, "reject-array-items"))
                replacement = "\"DEFS\": {\"negative\": {\"type\": \"array\"},";
            else if (!strcmp(settings->mode, "reject-ref-siblings"))
                replacement =
                    "\"DEFS\": {\"negative\": {\"$ref\": \"#/$defs/facts\", \"type\": \"string\"},";
            else if (!strcmp(settings->mode, "reject-zero-items"))
                replacement = "\"DEFS\": {\"negative\": {\"type\": \"array\", \"items\": "
                              "{\"type\": \"string\"}, \"maxItems\": 0},";
            else if (!strcmp(settings->mode, "reject-zero-length"))
                replacement = "\"DEFS\": {\"negative\": {\"type\": \"string\", \"maxLength\": 0},";
            else if (!strcmp(settings->mode, "reject-unique-nonstrings"))
                replacement = "\"DEFS\": {\"negative\": {\"type\": \"array\", \"items\": "
                              "{\"type\": \"integer\"}, \"uniqueItems\": true},";
            else if (!strcmp(settings->mode, "reject-ref-cycle"))
                replacement = "\"DEFS\": {\"negative\": {\"$ref\": \"#/$defs/negative2\"}, "
                              "\"negative2\": {\"$ref\": \"#/$defs/negative\"},";
        }
        scenario_require(run, replacement != NULL, "recognized negative scenario");
        char *mutated = replace(run, &input, needle, replacement);
        char *bad_source = path(run, work, "invalid-source.json");
        write_file(run, bad_source, mutated, strlen(mutated));
        snapshot_t bad_before = read_file(run, bad_source);
        snapshot_t before[OUTPUT_COUNT];
        char *names[OUTPUT_COUNT];
        for (size_t i = 0; i < OUTPUT_COUNT; ++i) {
            names[i] = path(run, work, outputs[i]);
            before[i] = read_file(run, names[i]);
        }
        /* Both commands validate before touching any generated file. */
        command(run, settings, work, bad_source, true, 1);
        command(run, settings, work, bad_source, false, 1);
        for (size_t i = 0; i < OUTPUT_COUNT; ++i)
            unchanged(run, names[i], &before[i]);
        unchanged(run, bad_source, &bad_before);
    }
    unchanged(run, source, &input);
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
                         "reject-keyword",
                         "reject-pattern",
                         "reject-type",
                         "reject-ref",
                         "reject-float",
                         "reject-duplicate",
                         "reject-array-items",
                         "reject-ref-siblings",
                         "reject-zero-items",
                         "reject-zero-length",
                         "reject-unique-nonstrings",
                         "reject-ref-cycle"};
    scenario_definition_t definitions[sizeof(ids) / sizeof(*ids)];
    for (size_t i = 0; i < sizeof(ids) / sizeof(*ids); ++i)
        definitions[i] = (scenario_definition_t){ids[i], 30000, test, &settings, NULL};
    /* Existing immutable test/data is deliberately disjoint from the build tree. */
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
