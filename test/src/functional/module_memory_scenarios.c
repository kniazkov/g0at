/** @file module_memory_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent allocation-failure and resource-limit scenarios (#370).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/format_binary.h"
#include "test/support/format_zip.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"
#include "yyjson.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#define REQUIRE(x) scenario_require(r, (x), #x)

typedef struct {
    const char *host, *kind;
} configuration_t;

typedef struct {
    uint64_t status, allocations, checks;
    bool triggered;
} report_t;

typedef struct {
    size_t size, expanded;
} fixture_t;

static bool contains(const void *data, size_t size, const char *text) {
    size_t n = strlen(text);
    for (size_t i = 0; n <= size && i <= size - n; i++)
        if (!memcmp((const char *)data + i, text, n))
            return true;
    return false;
}

static bool release_doc(void *p) {
    yyjson_doc_free(p);
    return true;
}

static scenario_command_result_t
invoke(scenario_run_t *r, configuration_t *c, const char *const *args) {
    const char *arguments[8] = {c->host};
    size_t count = 0;
    while (args[count]) {
        REQUIRE(count < 6);
        arguments[count + 1] = args[count];
        count++;
    }
    scenario_command_result_t result = language_command(r, c->host, arguments, NULL);
    REQUIRE(!contains(result.err, result.err_size, "ERROR: AddressSanitizer"));
    REQUIRE(!contains(result.err, result.err_size, "runtime error:"));
    return result;
}

static report_t report(scenario_run_t *r, scenario_command_result_t result, bool executed) {
    REQUIRE(result.exit_code == 0);
    const char *data = result.out;
    size_t size = result.out_size;
    if (executed) {
        REQUIRE(size >= 9 && !memcmp(data, "EXECUTED", 8));
        data += 8;
        size -= 8;
        if (size && *data == '\r') {
            data++;
            size--;
        }
        REQUIRE(size && *data == '\n');
        data++;
        size--;
    }
    REQUIRE(!contains(data, size, "EXECUTED"));
    yyjson_doc *doc = yyjson_read(data, size, 0);
    REQUIRE(doc);
    scenario_defer(r, release_doc, doc);
    yyjson_val *root = yyjson_doc_get_root(doc);
    const char *keys[] = {"status", "allocations", "checks"};
    uint64_t values[3];
    for (size_t i = 0; i < 3; i++) {
        yyjson_val *v = yyjson_obj_get(root, keys[i]);
        REQUIRE(yyjson_is_uint(v));
        values[i] = yyjson_get_uint(v);
    }
    yyjson_val *v = yyjson_obj_get(root, "triggered");
    REQUIRE(yyjson_is_bool(v));
    return (report_t){values[0], values[1], values[2], yyjson_get_bool(v)};
}

static report_t normal(scenario_run_t *r,
                       configuration_t *c,
                       unsigned status,
                       const char *archive_limit,
                       const char *expanded_limit) {
    const char *args[] = {"root.gbin", "0", archive_limit, expanded_limit, NULL};
    report_t info = report(r, invoke(r, c, args), status == 0);
    REQUIRE(info.status == status && !info.triggered);
    return info;
}

static fixture_t copy_fixture(scenario_run_t *r, const char *name, const char *destination) {
    const scenario_context_t *context = scenario_run_context(r);
    const char *directory = language_join(r, scenario_data_directory(context), "module-memory");
    size_t size;
    const unsigned char *data =
        (const unsigned char *)language_read(r, language_join(r, directory, name), &size);
    size_t manifest_size;
    const char *manifest =
        language_read(r, language_join(r, directory, "fixtures.json"), &manifest_size);
    yyjson_doc *doc = yyjson_read(manifest, manifest_size, 0);
    REQUIRE(doc);
    scenario_defer(r, release_doc, doc);
    char key[32];
    const char *dot = strchr(name, '.');
    REQUIRE(dot && (size_t)(dot - name) < sizeof(key));
    memcpy(key, name, (size_t)(dot - name));
    key[dot - name] = 0;
    yyjson_val *entry = yyjson_obj_get(yyjson_obj_get(yyjson_doc_get_root(doc), "fixtures"), key);
    const char *expected = yyjson_get_str(yyjson_obj_get(entry, "sha256"));
    char hash[65];
    format_binary_sha256(data, size, hash);
    REQUIRE(expected && !strcmp(hash, expected));
    language_write(r, language_join(r, scenario_work_directory(context), destination), data, size);
    if (!strcmp(name, "huge.gbin"))
        return (fixture_t){size, 0};
    format_zip_info_t info;
    char error[256];
    REQUIRE(format_zip_inspect(data, size, SIZE_MAX, &info, error, sizeof(error)));
    size_t count = info.count, expanded = 0;
    for (size_t i = 0; i < count; i++) {
        REQUIRE(format_zip_inspect(data, size, i, &info, error, sizeof(error)));
        REQUIRE(info.size <= SIZE_MAX - expanded);
        expanded += info.size;
    }
    return (fixture_t){size, expanded};
}

static uint64_t boundaries(scenario_run_t *r, configuration_t *c) {
    const char *args[] = {"boundaries", NULL};
    scenario_command_result_t result = invoke(r, c, args);
    REQUIRE(result.exit_code == 0);
    bool is32 = (result.out_size == 3 && !memcmp(result.out, "32\n", 3))
                || (result.out_size == 4 && !memcmp(result.out, "32\r\n", 4));
    bool is64 = (result.out_size == 3 && !memcmp(result.out, "64\n", 3))
                || (result.out_size == 4 && !memcmp(result.out, "64\r\n", 4));
    REQUIRE(is32 || is64);
    return is32 ? UINT32_MAX : UINT64_MAX;
}

static void body(scenario_run_t *r, void *user) {
    configuration_t *c = user;
    const char *kind = c->kind;
    if (!strcmp(kind, "boundaries")) {
        boundaries(r, c);
        return;
    }
    if (!strcmp(kind, "add-overflow") || !strcmp(kind, "multiply-overflow")) {
        const char *args[] = {kind, NULL};
        scenario_command_result_t result = invoke(r, c, args);
        REQUIRE(result.exit_code == 1);
        REQUIRE(contains(result.err, result.err_size, "Allocation size overflow"));
        return;
    }
    const char *work = scenario_work_directory(scenario_run_context(r));
    language_directory(r, language_join(r, work, "modules"));
    language_directory(r, language_join(r, work, "modules/dep"));
    language_directory(r, language_join(r, work, "fallback"));
    language_directory(r, language_join(r, work, "fallback/dep"));
    fixture_t root = copy_fixture(r, "root.gbin", "root.gbin");
    fixture_t dep = copy_fixture(r, "dep.gbin", "modules/dep/1.gbin");
    copy_fixture(r, "dep.gbin", "fallback/dep/1.gbin");
    if (!strcmp(kind, "baseline") || !strcmp(kind, "persistent") || !strcmp(kind, "once")
        || !strcmp(kind, "after-last")) {
        report_t baseline = normal(r, c, 0, NULL, NULL);
        REQUIRE(baseline.checks == 2 && baseline.allocations > 0
                && baseline.allocations < UINT64_MAX);
        if (!strcmp(kind, "baseline"))
            return;
        if (!strcmp(kind, "after-last")) {
            char index[32];
            snprintf(index, sizeof(index), "%" PRIu64, baseline.allocations + 1);
            const char *args[] = {"root.gbin", index, NULL};
            report_t info = report(r, invoke(r, c, args), true);
            REQUIRE(!info.triggered && info.status == 0 && info.checks == 2);
            return;
        }
        size_t returned = 0, terminated = 0;
        for (uint64_t i = 1; i <= baseline.allocations; i++) {
            char index[32];
            snprintf(index, sizeof(index), "%" PRIu64, i);
            const char *args[] = {"root.gbin", index, !strcmp(kind, "once") ? "once" : NULL, NULL};
            scenario_command_result_t result = invoke(r, c, args);
            REQUIRE(!contains(result.out, result.out_size, "EXECUTED"));
            if (result.exit_code) {
                REQUIRE(result.exit_code == 1);
                REQUIRE(contains(result.err, result.err_size, "Out of memory"));
                terminated++;
            } else {
                report_t info = report(r, result, false);
                REQUIRE(info.status == 3 && info.triggered);
                returned++;
            }
        }
        REQUIRE(returned && terminated);
        printf("%s: %" PRIu64 " allocations, %zu controlled returns, %zu fatal failures\n",
               kind,
               baseline.allocations,
               returned,
               terminated);
        return;
    }
    if (!strncmp(kind, "count", 5)) {
        char name[32];
        snprintf(name, sizeof(name), "%s.gbin", kind);
        copy_fixture(r, name, "root.gbin");
        normal(r, c, 2, NULL, NULL);
        return;
    }
    uint64_t maximum = boundaries(r, c), archive = maximum, expanded = maximum;
    unsigned status = 2;
    if (!strcmp(kind, "limit-exact")) {
        archive = root.size > dep.size ? root.size : dep.size;
        expanded = root.expanded > dep.expanded ? root.expanded : dep.expanded;
        status = 0;
    } else if (!strcmp(kind, "limit-archive"))
        archive = root.size - 1;
    else if (!strcmp(kind, "limit-root-expanded"))
        expanded = root.expanded - 1;
    else if (!strcmp(kind, "limit-dependency-expanded")) {
        REQUIRE(dep.expanded > root.expanded);
        expanded = dep.expanded - 1;
    } else {
        REQUIRE(!strcmp(kind, "declared-expanded"));
        /* Deliberately impossible lengths; never allocate their declared sizes. */
        copy_fixture(r, "huge.gbin", "root.gbin");
        expanded = UINT32_MAX;
    }
    char a[32], e[32];
    snprintf(a, sizeof(a), "%" PRIu64, archive);
    snprintf(e, sizeof(e), "%" PRIu64, expanded);
    report_t info = normal(r, c, status, a, e);
    if (!strcmp(kind, "limit-dependency-expanded"))
        REQUIRE(info.checks == 2);
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
    if (argc == 5 || (argc == 6 && !strcmp(argv[5], "--keep-work"))) {
        fs_error_t error = {0};
        char *host = absolute_fs_path(argv[2], &error);
        clear_fs_error(&error);
        if (host) {
            const char *kinds[] = {"baseline",
                                   "persistent",
                                   "once",
                                   "after-last",
                                   "boundaries",
                                   "add-overflow",
                                   "multiply-overflow",
                                   "limit-exact",
                                   "limit-archive",
                                   "limit-root-expanded",
                                   "limit-dependency-expanded",
                                   "count20",
                                   "count24",
                                   "count28",
                                   "count32",
                                   "count36",
                                   "declared-expanded"};

            enum { COUNT = sizeof(kinds) / sizeof(kinds[0]) };

            configuration_t configs[COUNT];
            scenario_definition_t definitions[COUNT];
            char ids[COUNT][96];
            for (size_t i = 0; i < COUNT; i++) {
                snprintf(ids[i], sizeof(ids[i]), "fault.module_memory.%s", kinds[i]);
                configs[i] = (configuration_t){host, kinds[i]};
                definitions[i] = (scenario_definition_t){ids[i], 300000, body, configs + i, NULL};
            }
            scenario_options_t options = {.build_directory = argv[3], .data_directory = argv[4]};
            const char *selection[] = {argv[1], argc == 6 ? "--keep-work" : NULL};
            result =
                scenario_registry_main(definitions, COUNT, argc == 6 ? 2 : 1, selection, &options);
        }
        FREE(host);
    } else
        fputs("usage: module_memory_scenarios ID HOST BUILD DATA [--keep-work]\n", stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
