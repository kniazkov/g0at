/** @file module_canonical_hash_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Exact Python corpus replay against production and independent C oracles.
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/format_binary.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *exe, *schema, *group;
} configuration_t;

/* One deferred holder lets each iteration release its oracle allocations early,
 * while failure at any assertion still releases the current iteration. */
typedef struct {
    yyjson_mut_doc *doc;
    char *output;
} oracle_resources_t;

static bool release_oracle(void *pointer) {
    oracle_resources_t *resources = pointer;
    yyjson_mut_doc_free(resources->doc);
    free(resources->output);
    free(resources);
    return true;
}

static bool release_doc(void *pointer) {
    yyjson_doc_free(pointer);
    return true;
}

static bool release_mut_doc(void *pointer) {
    yyjson_mut_doc_free(pointer);
    return true;
}

static unsigned hex_digit(scenario_run_t *run, unsigned char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    scenario_require(run, false, "invalid corpus hexadecimal digit");
    return 0;
}

static unsigned char *decode(scenario_run_t *run, yyjson_val *value, size_t *size) {
    scenario_require(run,
                     yyjson_is_str(value) && !(yyjson_get_len(value) % 2),
                     "missing or malformed corpus hexadecimal bytes");
    *size = yyjson_get_len(value) / 2;
    unsigned char *bytes = language_owned(run, ALLOC(*size + 1));
    const char *hex = yyjson_get_str(value);
    for (size_t i = 0; i < *size; i++)
        bytes[i] =
            (unsigned char)((hex_digit(run, hex[i * 2]) << 4) | hex_digit(run, hex[i * 2 + 1]));
    bytes[*size] = 0;
    return bytes;
}

static bool known_mode(const char *mode, bool hash) {
    return hash ? !strcmp(mode, "hash") || !strcmp(mode, "verify-hash")
                : !strcmp(mode, "canonical") || !strcmp(mode, "canonical-module")
                      || !strcmp(mode, "canonical-gmod") || !strcmp(mode, "main-metadata")
                      || !strcmp(mode, "module");
}

static void check_oracle(scenario_run_t *run,
                         oracle_resources_t *resources,
                         yyjson_mut_val *schema,
                         const char *mode,
                         const unsigned char *input,
                         size_t input_size,
                         int status,
                         scenario_command_result_t result) {
    /* The strict JSON oracle intentionally accepts arbitrary-size integer tokens;
     * production's int64 rejection cases are checked against frozen exit codes.
     * Every accepted value (including verify-hash mismatches) uses the independent
     * oracle, not the production normalizer/hash implementation. */
    if (status == 1)
        return;
    bool hash = !strcmp(mode, "hash") || !strcmp(mode, "verify-hash");
    const unsigned char *separator = hash ? memchr(input, 0, input_size) : NULL;
    scenario_require(run, !hash || separator, "missing hash input separator");
    size_t metadata_size = hash ? (size_t)(separator - input) : input_size;
    char error[256] = {0};
    resources->doc = format_json_parse((const char *)input, metadata_size, error, sizeof(error));
    scenario_require(run, resources->doc != NULL, error[0] ? error : "oracle parse failure");
    yyjson_mut_val *root = yyjson_mut_doc_get_root(resources->doc);
    size_t output_size = 0;
    if (hash) {
        char digest[65];
        scenario_require(run,
                         format_binary_main_hash(root,
                                                 schema,
                                                 separator + 1,
                                                 input_size - metadata_size - 1,
                                                 digest,
                                                 error,
                                                 sizeof(error)),
                         error[0] ? error : "independent hash failure");
        if (!strcmp(mode, "hash"))
            scenario_require_bytes(run,
                                   "independent SHA-256",
                                   digest,
                                   64,
                                   result.out,
                                   result.out_size,
                                   false);
        else {
            yyjson_mut_val *stored = yyjson_mut_obj_get(root, "mainHash");
            bool matches = yyjson_mut_equals_str(stored, digest);
            scenario_require(run,
                             status == (matches ? 0 : 3),
                             "independent hash verification disagrees with frozen status");
        }
    } else {
        if (!strcmp(mode, "main-metadata"))
            resources->output =
                format_metadata_main_bytes(root, schema, &output_size, error, sizeof(error));
        else {
            if (strcmp(mode, "canonical"))
                scenario_require(
                    run,
                    format_metadata_normalize(resources->doc,
                                              root,
                                              !strcmp(mode, "canonical-gmod") ? "gmod" : "module",
                                              schema,
                                              error,
                                              sizeof(error)),
                    error[0] ? error : "independent normalization failure");
            resources->output = format_json_canonical(root, &output_size);
        }
        scenario_require(run,
                         resources->output != NULL,
                         error[0] ? error : "independent canonicalization failure");
        scenario_require_bytes(run,
                               "independent canonical bytes",
                               resources->output,
                               output_size,
                               result.out,
                               result.out_size,
                               false);
    }
    yyjson_mut_doc_free(resources->doc);
    resources->doc = NULL;
    free(resources->output);
    resources->output = NULL;
}

static void corpus_body(scenario_run_t *run, void *user) {
    configuration_t *configuration = user;
    bool hash = !strcmp(configuration->group, "hash");
    size_t size;
    const char *source = language_read(run, configuration->schema, &size);
    char error[256] = {0};
    yyjson_mut_doc *schema = format_json_parse(source, size, error, sizeof(error));
    scenario_require(run, schema != NULL, "invalid independent oracle schema source");
    scenario_defer(run, release_mut_doc, schema);
    source = language_read(run,
                           language_join(run,
                                         scenario_data_directory(scenario_run_context(run)),
                                         "module-canonical-hash/cases.json"),
                           &size);
    yyjson_doc *corpus = yyjson_read(source, size, 0);
    scenario_require(run, corpus != NULL, "invalid canonical/hash corpus JSON");
    scenario_defer(run, release_doc, corpus);
    yyjson_val *cases = yyjson_doc_get_root(corpus);
    scenario_require(run,
                     yyjson_is_arr(cases) && yyjson_arr_size(cases) == 935,
                     "missing or truncated canonical/hash corpus");
    oracle_resources_t *resources = calloc(1, sizeof(*resources));
    scenario_require(run, resources != NULL, "cannot allocate oracle resources");
    scenario_defer(run, release_oracle, resources);
    size_t index, count, selected = 0, accepted = 0, rejected = 0, mismatched = 0;
    yyjson_val *item;
    yyjson_arr_foreach(cases, index, count, item) {
        yyjson_val *group = yyjson_obj_get(item, "scenario");
        scenario_require(run,
                         yyjson_equals_str(group, "canonical") || yyjson_equals_str(group, "hash"),
                         "unknown corpus scenario");
        if (!yyjson_equals_str(group, configuration->group))
            continue;
        scenario_checkpoint(run);
        yyjson_val *mode_value = yyjson_obj_get(item, "mode");
        const char *mode = yyjson_get_str(mode_value);
        scenario_require(run,
                         mode && strlen(mode) == yyjson_get_len(mode_value)
                             && known_mode(mode, hash),
                         "unknown corpus mode");
        yyjson_val *exit_code = yyjson_obj_get(item, "exitCode");
        scenario_require(run, yyjson_is_uint(exit_code), "invalid corpus exit code");
        uint64_t code = yyjson_get_uint(exit_code);
        scenario_require(run,
                         code == 0 || code == 1 || (hash && code == 3),
                         "unexpected corpus exit code");
        int status = (int)code;
        size_t input_size, expected_size;
        unsigned char *input = decode(run, yyjson_obj_get(item, "inputHex"), &input_size);
        unsigned char *expected = decode(run, yyjson_obj_get(item, "expectedHex"), &expected_size);
        char argument[32];
        snprintf(argument, sizeof(argument), "--%s", mode);
        const char *arguments[] = {configuration->exe, argument, NULL};
        child_options_t options = {.executable = configuration->exe,
                                   .arguments = arguments,
                                   .stdin_data = input,
                                   .stdin_size = input_size};
        printf("Canonical/hash case %zu (%s, expected %d)\n", index + 1, mode, status);
        scenario_command_result_t result = scenario_command_capture(run, &options);
        scenario_require(run, result.exit_code == status, "production acceptance mismatch");
        if (status == 0)
            scenario_require_bytes(run,
                                   "frozen Python bytes",
                                   expected,
                                   expected_size,
                                   result.out,
                                   result.out_size,
                                   false);
        check_oracle(run,
                     resources,
                     yyjson_mut_doc_get_root(schema),
                     mode,
                     input,
                     input_size,
                     status,
                     result);
        selected++;
        if (status == 0)
            accepted++;
        else if (status == 1)
            rejected++;
        else
            mismatched++;
    }
    scenario_require(run,
                     hash
                         ? selected == 513 && accepted == 333 && rejected == 11 && mismatched == 169
                         : selected == 422 && accepted == 328 && rejected == 94 && mismatched == 0,
                     "canonical/hash corpus coverage count mismatch");
    printf("[ ok ] %s checks passed: %zu (%zu accepted, %zu rejected, %zu hash mismatches)\n",
           configuration->group,
           selected,
           accepted,
           rejected,
           mismatched);
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
        puts("native.module_canonical\nnative.module_hash");
        result = 0;
    } else if (argc == 6 || argc == 7) {
        fs_error_t error = {0};
        char *exe = absolute_fs_path(argv[2], &error);
        clear_fs_error(&error);
        char *schema = absolute_fs_path(argv[5], &error);
        clear_fs_error(&error);
        if (exe && schema) {
            configuration_t configurations[] = {{exe, schema, "canonical"}, {exe, schema, "hash"}};
            scenario_definition_t definitions[] = {
                {"native.module_canonical", 180000, corpus_body, &configurations[0], NULL},
                {"native.module_hash", 180000, corpus_body, &configurations[1], NULL}};
            scenario_options_t options = {.build_directory = argv[3], .data_directory = argv[4]};
            const char *selection[] = {argv[1], argc == 7 ? argv[6] : NULL};
            result = scenario_registry_main(definitions, 2, argc == 7 ? 2 : 1, selection, &options);
        }
        FREE(schema);
        FREE(exe);
    } else
        fputs("usage: module_canonical_hash_scenarios ID HOST BUILD DATA SCHEMA [--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
