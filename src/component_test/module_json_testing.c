#include "lib/file_system.h"
#include "module_json.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#endif

static int total = 0;

static void check(const char *json, goat_module_json_kind kind, goat_module_json_error expected) {
    yyjson_doc *doc = NULL;
    goat_module_json_error actual = goat_module_json_read(json, strlen(json), kind, &doc);
    if (actual != expected) {
        FPRINTF(stderr,
                "Unexpected result: %s (expected %s) for %s\n",
                goat_module_json_error_string(actual),
                goat_module_json_error_string(expected),
                json);
    }
    assert(actual == expected);
    assert((doc != NULL) == (expected == GOAT_MODULE_JSON_OK));
    total++;
    printf("[ ok ] module JSON case %d\n", total);
    yyjson_doc_free(doc);
}

/* A bounded stdin fixture for cross-platform black-box parser tests. */
static int read_stdin(goat_module_json_kind kind) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    size_t capacity = 2 * 1024 * 1024;
    char *buffer = malloc(capacity);
    if (!buffer)
        return 2;
    size_t length = FREAD(buffer, 1, capacity, stdin);
    if (FERROR(stdin) || length == capacity) {
        free(buffer);
        return 2;
    }
    yyjson_doc *doc = NULL;
    goat_module_json_error err = goat_module_json_read(buffer, length, kind, &doc);
    /* The returned tree must own all strings and raw integer tokens. */
    memset(buffer, 0xAA, length);
    free(buffer);
    if (err != GOAT_MODULE_JSON_OK) {
        FPRINTF(stderr, "%s\n", goat_module_json_error_string(err));
        return 1;
    }
    char *output = yyjson_write(doc, 0, NULL);
    if (!output) {
        yyjson_doc_free(doc);
        return 2;
    }
    FPUTS(output, stdout);
    free(output);
    yyjson_doc_free(doc);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--gmod"))
        return read_stdin(GOAT_MODULE_GMOD);
    if (argc == 2 && !strcmp(argv[1], "--module"))
        return read_stdin(GOAT_MODULE_METADATA);
    puts("Starting module JSON testing...");
    yyjson_doc *invalid_doc = (yyjson_doc *)(uintptr_t)1;
    assert(goat_module_json_read("{}", 2, (goat_module_json_kind)42, &invalid_doc)
           == GOAT_MODULE_JSON_INVALID_FIELD);
    assert(invalid_doc == NULL);
    assert(goat_module_json_read("{}", 2, GOAT_MODULE_GMOD, NULL)
           == GOAT_MODULE_JSON_INVALID_FIELD);
    assert(goat_module_json_read(NULL, 0, GOAT_MODULE_GMOD, &invalid_doc)
           == GOAT_MODULE_JSON_SYNTAX);
    assert(invalid_doc == NULL);
    const char *good = "{\"formatVersion\":1,\"name\":\"geometry\","
                       "\"version\":\"1.2.0\",\"main\":\"src/main.goat\"}";
    check(good, GOAT_MODULE_GMOD, GOAT_MODULE_JSON_OK);
    check("{\"formatVersion\":1,\"name\":\"g\",\"version\":\"1\","
          "\"main\":\"a\",\"dependencies\":{\"math\":\"2.0.0\"},"
          "\"native\":{\"mode\":\"auto\",\"target\":\"host\"}}",
          GOAT_MODULE_GMOD,
          GOAT_MODULE_JSON_OK);
    check("{\"formatVersion\":1}", GOAT_MODULE_METADATA, GOAT_MODULE_JSON_INVALID_FIELD);
    check("{\"formatVersion\":1.0,\"name\":\"g\",\"version\":\"1\","
          "\"main\":\"a\"}",
          GOAT_MODULE_GMOD,
          GOAT_MODULE_JSON_INVALID_FIELD);
    check("{\"formatVersion\":1e0}", GOAT_MODULE_METADATA, GOAT_MODULE_JSON_INVALID_FIELD);
    check("{\"formatVersion\":18446744073709551616}",
          GOAT_MODULE_METADATA,
          GOAT_MODULE_JSON_INVALID_FIELD);
    check("{\"formatVersion\":2}", GOAT_MODULE_METADATA, GOAT_MODULE_JSON_UNSUPPORTED_VERSION);
    check("{\"formatVersion\":1,\"formatVersion\":1}",
          GOAT_MODULE_METADATA,
          GOAT_MODULE_JSON_DUPLICATE_KEY);
    check("{\"formatVersion\":1,\"\\u006eame\":\"a\",\"name\":\"b\"}",
          GOAT_MODULE_METADATA,
          GOAT_MODULE_JSON_DUPLICATE_KEY);
    check("{\"formatVersion\":1,\"dependencies\":{\"x\":1,\"x\":2}}",
          GOAT_MODULE_METADATA,
          GOAT_MODULE_JSON_DUPLICATE_KEY);
    check("{\"formatVersion\":1,\"unrecognized\":1}",
          GOAT_MODULE_METADATA,
          GOAT_MODULE_JSON_UNKNOWN_FIELD);
    check("{\"formatVersion\":1,\"name\":\"g\",\"version\":\"1\","
          "\"main\":\"a\",\"native\":{\"mode\":\"invalid\"}}",
          GOAT_MODULE_GMOD,
          GOAT_MODULE_JSON_INVALID_FIELD);
    check("{\"formatVersion\":1,\"name\":\"g\",\"version\":\"1\","
          "\"main\":\"a\",\"native\":{\"unknown\":1}}",
          GOAT_MODULE_GMOD,
          GOAT_MODULE_JSON_UNKNOWN_FIELD);
    check("{\"formatVersion\":1,\"name\":\"g\",\"version\":\"1\","
          "\"main\":\"a\",\"dependencies\":{\"x\":2}}",
          GOAT_MODULE_GMOD,
          GOAT_MODULE_JSON_INVALID_FIELD);
    check("{\"formatVersion\":1,\"name\":\"\\uD83D\\uDCD0\","
          "\"version\":\"1\",\"main\":\"a\"}",
          GOAT_MODULE_GMOD,
          GOAT_MODULE_JSON_INVALID_FIELD);
    check("{\"formatVersion\":1,\"name\":\"\\uD83D\","
          "\"version\":\"1\",\"main\":\"a\"}",
          GOAT_MODULE_GMOD,
          GOAT_MODULE_JSON_SYNTAX);
    yyjson_doc *doc = NULL;
    const char bom[] = "\xef\xbb\xbf{\"formatVersion\":1}";
    assert(goat_module_json_read(bom, sizeof(bom) - 1, GOAT_MODULE_METADATA, &doc)
           == GOAT_MODULE_JSON_SYNTAX);
    const char invalid[] = "{\"formatVersion\":1,\"name\":\"\xc3\x28\"}";
    assert(goat_module_json_read(invalid, sizeof(invalid) - 1, GOAT_MODULE_METADATA, &doc)
           == GOAT_MODULE_JSON_SYNTAX);
    printf("Module JSON testing done; total: %d, passed: %d, failed: 0\n", total, total);
    return 0;
}
