/** @file inspect_report_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Python parity fixtures and negative tests of shared report checks.
 */
#include "lib/files/file_system.h"
#include "test/support/inspect_report.h"
#include "test/support/registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *cases, *schema, *directory;
    bool mutant;
} options;

static yyjson_mut_doc *load(const char *path) {
    FILE *f = FOPEN(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END)) {
        fclose(f);
        return NULL;
    }
    long length = ftell(f);
    if (length < 0 || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        return NULL;
    }
    char *text = malloc((size_t)length + 1);
    if (!text) {
        fclose(f);
        return NULL;
    }
    bool ok = fread(text, 1, (size_t)length, f) == (size_t)length;
    if (fclose(f))
        ok = false;
    yyjson_mut_doc *doc = ok ? inspect_report_parse(text, (size_t)length, NULL, 0) : NULL;
    free(text);
    return doc;
}

static bool release(void *doc) {
    yyjson_mut_doc_free(doc);
    return true;
}

static void parity(scenario_run_t *run, void *user) {
    options *o = user;
    yyjson_mut_doc *cases = load(o->cases);
    scenario_require(run, cases != NULL, "load frozen Python cases");
    scenario_defer(run, release, cases);
    yyjson_mut_doc *published = load(o->schema);
    scenario_require(run, published != NULL, "load published schema");
    scenario_defer(run, release, published);
    size_t i, n, good = 0, bad = 0;
    yyjson_mut_val *row;
    yyjson_mut_arr_foreach(yyjson_mut_doc_get_root(cases), i, n, row) {
        scenario_checkpoint(run);
        yyjson_mut_val *schema = yyjson_mut_obj_get(row, "schema");
        if (yyjson_mut_is_null(schema))
            schema = yyjson_mut_doc_get_root(published);
        bool expected = yyjson_mut_get_bool(yyjson_mut_obj_get(row, "valid"));
        char error[256];
        bool actual = inspect_report_schema(yyjson_mut_obj_get(row, "value"),
                                            schema,
                                            schema,
                                            o->directory,
                                            error,
                                            sizeof(error));
        if (o->mutant)
            actual = true;
        const char *name = yyjson_mut_get_str(yyjson_mut_obj_get(row, "name"));
        if (actual != expected)
            fprintf(stderr, "%s: expected %d actual %d (%s)\n", name, expected, actual, error);
        scenario_require(run, actual == expected, name);
        if (expected)
            good++;
        else
            bad++;
    }
    scenario_require(run, good >= 20 && bad >= 50, "positive and negative coverage");
    printf("%zu valid and %zu invalid Python/C report cases passed\n", good, bad);
}

static void parsing(scenario_run_t *run, void *user) {
    (void)user;
    const char *invalid[] = {"{\"a\":1,\"a\":2}",
                             "{\"x\":[{\"a\":1,\"\\u0061\":2}]}",
                             "{\"a\\u0000b\":1,\"a\\u0000b\":2}",
                             "{\"x\":NaN}",
                             "{\"x\":1,}",
                             "{} trailing",
                             "\"\xff\""};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
        char error[128];
        yyjson_mut_doc *doc =
            inspect_report_parse(invalid[i], strlen(invalid[i]), error, sizeof(error));
        bool rejected = doc == NULL;
        yyjson_mut_doc_free(doc);
        scenario_require(run, rejected && error[0], "reject malformed/duplicate JSON");
    }
    const char *valid[] = {"{\"a\":1,\"ab\":2}",
                           "{\"x\":1.5}",
                           "{\"x\":123456789012345678901234567890}",
                           "{\"a\":{\"x\":1},\"b\":{\"x\":2}}"};
    for (size_t i = 0; i < sizeof(valid) / sizeof(*valid); i++) {
        yyjson_mut_doc *doc = inspect_report_parse(valid[i], strlen(valid[i]), NULL, 0);
        bool accepted = doc != NULL;
        yyjson_mut_doc_free(doc);
        scenario_require(run, accepted, "accept strict structural JSON");
    }
}

int main(int argc, char **argv) {
    if (argc != 7 && argc != 8) {
        fprintf(stderr,
                "Usage: %s parity|parsing build data cases schema directory [accept-invalid]\n",
                argv[0]);
        return 1;
    }
    if (argc == 8 && strcmp(argv[7], "accept-invalid"))
        return 1;
    options o = {argv[4], argv[5], argv[6], argc == 8};
    scenario_definition_t definitions[] = {{"parity", 30000, parity, &o, NULL},
                                           {"parsing", 30000, parsing, &o, NULL}};
    scenario_options_t base = {NULL, argv[2], argv[3], false, stderr};
    const char *selection[] = {argv[1]};
    return scenario_registry_main(definitions, 2, 1, selection, &base);
}
