/** @file native_runner_policy_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent regression controls for the actual native runner policies (#383).
 */
#include "lib/files/file_system.h"
#include "test/support/native_runner_policy.h"
#include "test/support/registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define REQUIRE(x) scenario_require(r, (x), #x)

static yyjson_doc *document(scenario_run_t *r, const char *s) {
    yyjson_doc *d = yyjson_read(s, strlen(s), 0);
    REQUIRE(d);
    return d;
}

static bool report(scenario_run_t *r, const char *text, const char *case_json, bool native) {
    yyjson_doc *d = document(r, case_json);
    char error[256];
    bool result =
        nr_check_report(text, strlen(text), yyjson_doc_get_root(d), native, error, sizeof(error));
    yyjson_doc_free(d);
    return result;
}

typedef struct {
    const char *names[4];
    size_t visited;
    bool fail_first;
} visits_t;

static bool visit(yyjson_val *c, void *user) {
    visits_t *v = user;
    v->names[v->visited++] = yyjson_get_str(yyjson_obj_get(c, "name"));
    return !(v->fail_first && v->visited == 1);
}

static bool exists(const char *relative, bool check, void *user) {
    (void)check;
    return strcmp(relative, user ? user : "missing") != 0;
}

static bool manifest(scenario_run_t *r, const char *json, const char *absent) {
    yyjson_doc *d = document(r, json);
    char error[128];
    bool ok =
        nr_manifest_validate(yyjson_doc_get_root(d), exists, (void *)absent, error, sizeof(error));
    yyjson_doc_free(d);
    return ok;
}

static void body(scenario_run_t *r, void *user) {
    const char *kind = user;
    if (!strncmp(kind, "output-", 7)) {
        static const unsigned char expected[] = {'a', '\n', 'b', 0, '\n'};
        static const unsigned char good[] = {'a', '\r', '\n', 'b', 0, '\r', '\n'};
        static const unsigned char no_nul[] = {'a', '\n', 'b', '\n'}, no_nl[] = {'a', '\n', 'b', 0},
                                   space[] = {'a', '\n', 'b', 0, ' ', '\n'},
                                   extra[] = {'a', '\n', 'b', 0, '\n', '\n'};
        const void *actual = good;
        size_t n = sizeof(good);
        bool want = !strcmp(kind, "output-good");
        if (!strcmp(kind, "output-missing-nul")) {
            actual = no_nul;
            n = sizeof(no_nul);
        }
        if (!strcmp(kind, "output-missing-newline")) {
            actual = no_nl;
            n = sizeof(no_nl);
        }
        if (!strcmp(kind, "output-space")) {
            actual = space;
            n = sizeof(space);
        }
        if (!strcmp(kind, "output-extra-line")) {
            actual = extra;
            n = sizeof(extra);
        }
        REQUIRE(nr_output_equal(actual, n, expected, sizeof(expected)) == want);
        return;
    }
    if (!strcmp(kind, "report-unicode")) {
        char huge[4500];
        const char *prefix = "mode=required\npreparation=ready\nbound=";
        size_t prefix_size = strlen(prefix);
        memcpy(huge, prefix, prefix_size);
        memset(huge + prefix_size, '1', 4301);
        strcpy(huge + prefix_size + 4301, "\nattempts=3\nsucceeded=3\nretries=0\n");
        REQUIRE(!report(r, huge, "{\"calls\":3}", true));

        REQUIRE(report(r, "mode=required\x85", "{\"calls\":3}", true) == false);
        REQUIRE(report(r,
                       "mode=required\xc2\x85preparation=ready\xe2\x80\xa8"
                       "bound= +\xd9\xa2_\xd9\xa0 \xe2\x80\xa9"
                       "attempts=3\r\nsucceeded=3\vretries=0\f",
                       "{\"calls\":3}",
                       true));
        REQUIRE(!report(
            r,
            "mode=required\npreparation=ready\nbound=2__0\nattempts=3\nsucceeded=3\nretries=0\n",
            "{\"calls\":3}",
            true));
        REQUIRE(!report(
            r,
            "mode=required\npreparation=ready\nbound=-2\nattempts=3\nsucceeded=3\nretries=0\n",
            "{\"calls\":3}",
            true));
        REQUIRE(!report(
            r,
            "mode=required\npreparation=ready\nbound=2\nattempts=03\nsucceeded=3\nretries=0\n",
            "{\"calls\":3}",
            true));
        return;
    }
    if (!strcmp(kind, "report-duplicates")) {
        REQUIRE(report(r,
                       "mode=auto\nmode=required\npreparation=ready\nbound=2\nattempts="
                       "3\nsucceeded=3\nretries=0\nextra=a=b\n",
                       "{\"calls\":3,\"bound\":2}",
                       true));
        REQUIRE(!report(r,
                        "mode=required\nmode=auto\npreparation=ready\nbound=2\nattempts="
                        "3\nsucceeded=3\nretries=0\n",
                        "{\"calls\":3,\"bound\":2}",
                        true));
        REQUIRE(!report(r, "mode=off\nattempts=0\nsucceeded=0\nretries=0\n\n", "{}", false));
        return;
    }
    if (!strcmp(kind, "manifest-validation")) {
        REQUIRE(manifest(r, "[{\"name\":\"Bad\",\"name\":\"good\",\"calls\":1}]", NULL));
        REQUIRE(!manifest(r, "[{\"name\":\"good\",\"name\":\"Bad\",\"calls\":1}]", NULL));
        REQUIRE(manifest(r, "[{\"name\":\"good\",\"calls\":false,\"calls\":3}]", NULL));
        REQUIRE(!manifest(r, "[{\"name\":\"good\",\"calls\":3,\"calls\":false}]", NULL));

        REQUIRE(manifest(
            r,
            "[{\"name\":\"first\",\"calls\":3},{\"name\":\"other\",\"check\":\"runner.py\"}]",
            NULL));
        REQUIRE(!manifest(r, "[]", NULL));
        REQUIRE(!manifest(r, "{}", NULL));
        REQUIRE(!manifest(r, "[{\"name\":\"bad\",\"calls\":true}]", NULL));
        REQUIRE(!manifest(r, "[{\"name\":\"bad\",\"calls\":0}]", NULL));
        REQUIRE(!manifest(r, "[{\"name\":\"bad\",\"calls\":1,\"extra\":0}]", NULL));
        REQUIRE(
            !manifest(r, "[{\"name\":\"bad\",\"calls\":1},{\"name\":\"bad\",\"calls\":1}]", NULL));
        REQUIRE(!manifest(r, "[{\"name\":\"Bad\",\"calls\":1}]", NULL));
        REQUIRE(!manifest(r, "[{\"name\":\"bad\",\"check\":\"missing.py\"}]", "missing.py"));
        REQUIRE(!manifest(r, "[{\"name\":\"bad\",\"calls\":1}]", "bad/program.goat"));
        return;
    }
    if (!strcmp(kind, "continuation") || !strcmp(kind, "selection-order")
        || !strcmp(kind, "selection-unknown")) {
        yyjson_doc *d = document(r, "[{\"name\":\"bad\"},{\"name\":\"good\"}]");
        yyjson_val *cases = yyjson_doc_get_root(d);
        visits_t v = {.fail_first = !strcmp(kind, "continuation")};
        size_t total = 99, failed = 99;
        char error[128];
        const char *only[] = {"good", "bad", "good"};
        size_t count = !strcmp(kind, "selection-order") ? 3 : 0;
        if (!strcmp(kind, "selection-unknown")) {
            only[0] = "missing";
            count = 1;
        }
        bool ok =
            nr_run_selected(cases, only, count, visit, &v, &total, &failed, error, sizeof(error));
        if (!strcmp(kind, "selection-unknown")) {
            REQUIRE(!ok && total == 0 && failed == 0 && v.visited == 0);
        } else {
            REQUIRE(v.visited == 2 && !strcmp(v.names[0], "bad") && !strcmp(v.names[1], "good")
                    && total == 2);
            REQUIRE(failed == (v.fail_first ? 1 : 0) && ok == !v.fail_first);
        }
        yyjson_doc_free(d);
        return;
    }
    const char *keys[] = {"mode", "preparation", "bound", "attempts", "succeeded", "retries"};
    const char *values[] = {"required", "ready", "2", "3", "3", "0"};
    bool native = strncmp(kind, "report-vm-", 10) != 0,
         want = !strcmp(kind, "report-good") || !strcmp(kind, "report-vm-good");
    if (!native) {
        values[0] = "off";
        values[3] = values[4] = !strcmp(kind, "report-vm-good") ? "0" : "1";
    }
    if (!strcmp(kind, "report-mode"))
        values[0] = "auto";
    if (!strcmp(kind, "report-preparation"))
        values[1] = "empty";
    if (!strcmp(kind, "report-bound-zero"))
        values[2] = "0";
    if (!strcmp(kind, "report-bound-count"))
        values[2] = "1";
    if (!strcmp(kind, "report-zero-calls"))
        values[3] = values[4] = "0";
    if (!strcmp(kind, "report-attempts"))
        values[3] = "4";
    if (!strcmp(kind, "report-succeeded"))
        values[4] = "2";
    if (!strcmp(kind, "report-retries"))
        values[5] = "1";
    char text[256];
    size_t used = 0;
    for (size_t i = 0; i < 6; i++) {
        if (!native && (i == 1 || i == 2))
            continue;
        if (!strncmp(kind, "report-missing-", 15) && !strcmp(kind + 15, keys[i]))
            continue;
        int n = snprintf(text + used, sizeof(text) - used, "%s=%s\n", keys[i], values[i]);
        REQUIRE(n >= 0 && (size_t)n < sizeof(text) - used);
        used += (size_t)n;
    }
    REQUIRE(report(r, text, "{\"calls\":3,\"bound\":2}", native) == want);
}

static const char *kinds[] = {"output-good",
                              "output-missing-nul",
                              "output-missing-newline",
                              "output-space",
                              "output-extra-line",
                              "report-good",
                              "report-mode",
                              "report-preparation",
                              "report-bound-zero",
                              "report-bound-count",
                              "report-zero-calls",
                              "report-attempts",
                              "report-succeeded",
                              "report-retries",
                              "report-missing-mode",
                              "report-missing-preparation",
                              "report-missing-bound",
                              "report-missing-attempts",
                              "report-missing-succeeded",
                              "report-missing-retries",
                              "report-vm-good",
                              "report-vm-native",
                              "continuation",
                              "report-unicode",
                              "report-duplicates",
                              "manifest-validation",
                              "selection-order",
                              "selection-unknown"};

int main(int argc, char **argv) {
#ifdef _WIN32
    if (!fs_arguments(&argc, &argv, NULL))
        return 1;
#endif
    int result = 1;
    if (argc == 4 || (argc == 5 && scenario_keep_work_argument(argv[4]))) {
        enum { COUNT = sizeof(kinds) / sizeof(kinds[0]) };

        scenario_definition_t defs[COUNT];
        char ids[COUNT][100];
        for (size_t i = 0; i < COUNT; i++) {
            snprintf(ids[i], sizeof(ids[i]), "helpers.native_runner.%s", kinds[i]);
            defs[i] = (scenario_definition_t){ids[i], 10000, body, (void *)kinds[i], NULL};
        }
        scenario_options_t options = {.build_directory = argv[2], .data_directory = argv[3]};
        result = 0;
        if (!strcmp(argv[1], "all")) {
            for (size_t i = 0; i < COUNT; i++) {
                const char *select[] = {ids[i], argc == 5 ? argv[4] : NULL};
                if (scenario_registry_main(defs, COUNT, argc == 5 ? 2 : 1, select, &options))
                    result = 1;
            }
        } else {
            const char *select[] = {argv[1], argc == 5 ? argv[4] : NULL};
            result = scenario_registry_main(defs, COUNT, argc == 5 ? 2 : 1, select, &options);
        }
    } else
        fputs("usage: native_runner_policy_testing ID|all BUILD DATA [--keep-work]\n", stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
