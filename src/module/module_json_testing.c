#include "module_json.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void check(const char *json, goat_module_json_kind kind,
                  goat_module_json_error expected) {
    yyjson_doc *doc = NULL;
    goat_module_json_error actual = goat_module_json_read(json, strlen(json), kind, &doc);
    if (actual != expected) {
        fprintf(stderr, "Unexpected result: %s (expected %s) for %s\n",
            goat_module_json_error_string(actual),
            goat_module_json_error_string(expected), json);
    }
    assert(actual == expected);
    assert((doc != NULL) == (expected == GOAT_MODULE_JSON_OK));
    yyjson_doc_free(doc);
}

int main(void) {
    const char *good = "{\"formatVersion\":1,\"name\":\"geometry\","
                       "\"version\":\"1.2.0\",\"main\":\"src/main.goat\"}";
    check(good, GOAT_MODULE_GMOD, GOAT_MODULE_JSON_OK);
    check("{\"formatVersion\":1,\"name\":\"g\",\"version\":\"1\","
          "\"main\":\"a\",\"dependencies\":{\"math\":\"2.0.0\"},"
          "\"native\":{\"mode\":\"auto\",\"target\":\"host\"}}",
          GOAT_MODULE_GMOD, GOAT_MODULE_JSON_OK);
    check("{\"formatVersion\":1}", GOAT_MODULE_METADATA, GOAT_MODULE_JSON_OK);
    check("{\"formatVersion\":1.0,\"name\":\"g\",\"version\":\"1\","
          "\"main\":\"a\"}", GOAT_MODULE_GMOD, GOAT_MODULE_JSON_INVALID_FIELD);
    check("{\"formatVersion\":1e0}", GOAT_MODULE_METADATA, GOAT_MODULE_JSON_INVALID_FIELD);
    check("{\"formatVersion\":18446744073709551616}", GOAT_MODULE_METADATA,
          GOAT_MODULE_JSON_INVALID_FIELD);
    check("{\"formatVersion\":2}", GOAT_MODULE_METADATA,
          GOAT_MODULE_JSON_UNSUPPORTED_VERSION);
    check("{\"formatVersion\":1,\"formatVersion\":1}", GOAT_MODULE_METADATA,
          GOAT_MODULE_JSON_DUPLICATE_KEY);
    check("{\"formatVersion\":1,\"\\u006eame\":\"a\",\"name\":\"b\"}",
          GOAT_MODULE_METADATA, GOAT_MODULE_JSON_DUPLICATE_KEY);
    check("{\"formatVersion\":1,\"dependencies\":{\"x\":1,\"x\":2}}",
          GOAT_MODULE_METADATA, GOAT_MODULE_JSON_DUPLICATE_KEY);
    check("{\"formatVersion\":1,\"unrecognized\":1}", GOAT_MODULE_METADATA,
          GOAT_MODULE_JSON_UNKNOWN_FIELD);
    check("{\"formatVersion\":1,\"name\":\"g\",\"version\":\"1\","
          "\"main\":\"a\",\"native\":{\"mode\":\"invalid\"}}",
          GOAT_MODULE_GMOD, GOAT_MODULE_JSON_INVALID_FIELD);
    check("{\"formatVersion\":1,\"name\":\"g\",\"version\":\"1\","
          "\"main\":\"a\",\"native\":{\"unknown\":1}}",
          GOAT_MODULE_GMOD, GOAT_MODULE_JSON_UNKNOWN_FIELD);
    check("{\"formatVersion\":1,\"name\":\"g\",\"version\":\"1\","
          "\"main\":\"a\",\"dependencies\":{\"x\":2}}",
          GOAT_MODULE_GMOD, GOAT_MODULE_JSON_INVALID_FIELD);
    check("{\"formatVersion\":1,\"name\":\"\\uD83D\\uDCD0\","
          "\"version\":\"1\",\"main\":\"a\"}",
          GOAT_MODULE_GMOD, GOAT_MODULE_JSON_OK);
    check("{\"formatVersion\":1,\"name\":\"\\uD83D\","
          "\"version\":\"1\",\"main\":\"a\"}",
          GOAT_MODULE_GMOD, GOAT_MODULE_JSON_SYNTAX);
    yyjson_doc *doc = NULL;
    const char bom[] = "\xef\xbb\xbf{\"formatVersion\":1}";
    assert(goat_module_json_read(bom, sizeof(bom) - 1, GOAT_MODULE_METADATA, &doc)
           == GOAT_MODULE_JSON_SYNTAX);
    const char invalid[] = "{\"formatVersion\":1,\"name\":\"\xc3\x28\"}";
    assert(goat_module_json_read(invalid, sizeof(invalid) - 1, GOAT_MODULE_METADATA, &doc)
           == GOAT_MODULE_JSON_SYNTAX);
    puts("module JSON tests passed");
    return 0;
}
