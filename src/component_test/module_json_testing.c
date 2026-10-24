#include "lib/file_system.h"
#include "module_hash.h"
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
static int read_stdin(goat_module_json_kind kind, int canonical) {
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
    if (canonical == 4 || canonical == 5) {
        char *separator = memchr(buffer, 0, length);
        if (!separator) {
            free(buffer);
            return 2;
        }
        yyjson_doc *metadata = NULL;
        goat_module_json_error error = goat_module_json_read(buffer,
                                                             (size_t)(separator - buffer),
                                                             GOAT_MODULE_METADATA,
                                                             &metadata);
        char digest[GOAT_MODULE_HASH_HEX_SIZE];
        bool matches = false;
        size_t bytes = length - (size_t)(separator + 1 - buffer);
        if (!error) {
            if (canonical == 4)
                error = goat_module_main_hash(metadata, separator + 1, bytes, digest);
            else
                error = goat_module_main_hash_verify(metadata, separator + 1, bytes, &matches);
        }
        if (!error && canonical == 4)
            FWRITE(digest, 1, 64, stdout);
        yyjson_doc_free(metadata);
        free(buffer);
        return error ? 1 : canonical == 5 && !matches ? 3 : 0;
    }
    yyjson_doc *doc = NULL;
    goat_module_json_error err;
    if (canonical == 1) {
        doc = length >= 3 && !memcmp(buffer, "\xef\xbb\xbf", 3)
                  ? NULL
                  : yyjson_read(buffer, length, YYJSON_READ_NUMBER_AS_RAW);
        err = doc ? GOAT_MODULE_JSON_OK : GOAT_MODULE_JSON_SYNTAX;
    } else
        err = goat_module_json_read(buffer, length, kind, &doc);
    /* The returned tree must own all strings and raw integer tokens. */
    memset(buffer, 0xAA, length);
    free(buffer);
    if (err != GOAT_MODULE_JSON_OK) {
        FPRINTF(stderr, "%s\n", goat_module_json_error_string(err));
        return 1;
    }
    char *output = NULL;
    size_t output_length = 0;
    if (canonical == 6) {
        yyjson_doc *info = NULL;
        err = goat_module_information(doc, kind, &info);
        yyjson_doc_free(doc);
        doc = info; /* Projection must own its bytes independently. */
        if (!err)
            err = goat_json_canonical(yyjson_doc_get_root(doc), &output, &output_length);
    } else if (canonical == 1)
        err = goat_json_canonical(yyjson_doc_get_root(doc), &output, &output_length);
    else if (canonical)
        err = goat_module_json_write(doc, kind, canonical == 3, &output, &output_length);
    else {
        output = yyjson_write(doc, 0, &output_length);
        err = output ? GOAT_MODULE_JSON_OK : GOAT_MODULE_JSON_MEMORY;
    }
    if (err) {
        FPRINTF(stderr, "%s\n", goat_module_json_error_string(err));
        yyjson_doc_free(doc);
        return 1;
    }
    FWRITE(output, 1, output_length, stdout);
    goat_module_json_free(output);
    yyjson_doc_free(doc);
    return 0;
}

/* Programmatically constructed values must not bypass canonical validation. */
static void canonical_api_checks(void) {
    char *out = (char *)(uintptr_t)1;
    size_t length = 123;
    assert(goat_json_canonical(NULL, &out, &length) == GOAT_MODULE_JSON_INVALID_FIELD);
    assert(!out && !length);
    yyjson_val value = {0};
    yyjson_set_uint(&value, UINT64_MAX);
    assert(goat_json_canonical(&value, &out, &length) == GOAT_MODULE_JSON_INVALID_FIELD);
    yyjson_set_real(&value, 1.0);
    assert(goat_json_canonical(&value, &out, &length) == GOAT_MODULE_JSON_INVALID_FIELD);
    const char *bad[] = {"", "-", "+1", "01", "-01", "1.0", "1e0", "null", "[1]"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
        yyjson_set_raw(&value, bad[i], strlen(bad[i]));
        assert(goat_json_canonical(&value, &out, &length) == GOAT_MODULE_JSON_INVALID_FIELD);
        assert(!out && !length);
    }
    yyjson_set_strn(&value, "\xed\xa0\x80", 3);
    assert(goat_json_canonical(&value, &out, &length) == GOAT_MODULE_JSON_INVALID_FIELD);
    yyjson_set_sint(&value, INT64_MIN);
    assert(goat_json_canonical(&value, &out, &length) == GOAT_MODULE_JSON_OK);
    assert(length == 20 && !memcmp(out, "-9223372036854775808", 20) && !out[length]);
    goat_module_json_free(out);
    assert(goat_json_canonical(&value, NULL, &length) == GOAT_MODULE_JSON_INVALID_FIELD);
    assert(!length);
    assert(goat_json_canonical(&value, &out, NULL) == GOAT_MODULE_JSON_INVALID_FIELD);
    assert(!out);
    assert(goat_module_json_write(NULL, GOAT_MODULE_METADATA, false, &out, &length)
           == GOAT_MODULE_JSON_INVALID_FIELD);
    assert(!out && !length);
    goat_module_json_free(NULL);
}

typedef struct {
    size_t count;
    goat_module_json_error errors[16];
    char paths[16][128];
    size_t lengths[16];
} json_diagnostics;

static void
collect_diagnostic(goat_module_json_error error, const char *path, size_t length, void *context) {
    json_diagnostics *result = context;
    assert(result->count < 16 && length < 128);
    size_t i = result->count++;
    result->errors[i] = error;
    result->lengths[i] = length;
    memcpy(result->paths[i], path, length);
    result->paths[i][length] = 0;
}

static void diagnostic_checks(void) {
    const char *cases[] = {
        "{\"formatVersion\":1,\"name\":42,\"version\":false,\"main\":\"a\"}",
        "{\"formatVersion\":1,\"name\":\"g\",\"version\":\"1\",\"main\":\"a\","
        "\"native\":false,\"authors\":[{\"name\":3},{\"email\":false}]}",
        "{\"formatVersion\":1,\"name\":\"g\",\"version\":\"1\",\"main\":\"a\","
        "\"a/b~c\":0,\"nul\\u0000key\":0}",
        "{\"formatVersion\":1,\"name\":false,",
        "{\"formatVersion\":1,\"name\":false,\"name\":3}",
        "{\"formatVersion\":1,\"name\":\"g\",\"version\":\"1\",\"main\":\"a\"}",
    };
    const size_t counts[] = {2, 4, 2, 1, 1, 0};
    const goat_module_json_error errors[] = {GOAT_MODULE_JSON_INVALID_FIELD,
                                             GOAT_MODULE_JSON_INVALID_FIELD,
                                             GOAT_MODULE_JSON_UNKNOWN_FIELD,
                                             GOAT_MODULE_JSON_SYNTAX,
                                             GOAT_MODULE_JSON_DUPLICATE_KEY,
                                             GOAT_MODULE_JSON_OK};
    json_diagnostics results[6] = {0};
    for (size_t i = 0; i < 6; i++) {
        yyjson_doc *doc = NULL;
        assert(goat_module_json_read_diagnostics(cases[i],
                                                 strlen(cases[i]),
                                                 GOAT_MODULE_GMOD,
                                                 &doc,
                                                 collect_diagnostic,
                                                 &results[i])
               == errors[i]);
        assert(results[i].count == counts[i]);
        assert((doc != NULL) == (errors[i] == GOAT_MODULE_JSON_OK));
        yyjson_doc_free(doc);
        json_diagnostics again = {0};
        assert(goat_module_json_read_diagnostics(cases[i],
                                                 strlen(cases[i]),
                                                 GOAT_MODULE_GMOD,
                                                 &doc,
                                                 collect_diagnostic,
                                                 &again)
               == errors[i]);
        assert(!memcmp(&again, &results[i], sizeof(again)));
        yyjson_doc_free(doc);
    }
    assert(!strcmp(results[0].paths[0], "/name"));
    assert(!strcmp(results[0].paths[1], "/version"));
    assert(!strcmp(results[1].paths[0], "/native"));
    assert(!strcmp(results[1].paths[1], "/authors/0/name"));
    assert(!strcmp(results[1].paths[2], "/authors/1/email"));
    assert(!strcmp(results[1].paths[3], "/authors/1/name"));
    assert(!strcmp(results[2].paths[0], "/a~1b~0c"));
    assert(results[2].lengths[1] == 8 && !memcmp(results[2].paths[1], "/nul\0key", 8));
    assert(!results[3].lengths[0] && !results[4].lengths[0]);
    puts("[ ok ] module JSON independent diagnostics");
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--information-module"))
        return read_stdin(GOAT_MODULE_METADATA, 6);
    if (argc == 2 && !strcmp(argv[1], "--information-gmod"))
        return read_stdin(GOAT_MODULE_GMOD, 6);
    if (argc == 2 && !strcmp(argv[1], "--gmod"))
        return read_stdin(GOAT_MODULE_GMOD, 0);
    if (argc == 2 && !strcmp(argv[1], "--module"))
        return read_stdin(GOAT_MODULE_METADATA, 0);
    if (argc == 2 && !strcmp(argv[1], "--canonical"))
        return read_stdin(GOAT_MODULE_METADATA, 1);
    if (argc == 2 && !strcmp(argv[1], "--canonical-module"))
        return read_stdin(GOAT_MODULE_METADATA, 2);
    if (argc == 2 && !strcmp(argv[1], "--canonical-gmod"))
        return read_stdin(GOAT_MODULE_GMOD, 2);
    if (argc == 2 && !strcmp(argv[1], "--main-metadata"))
        return read_stdin(GOAT_MODULE_METADATA, 3);
    if (argc == 2 && !strcmp(argv[1], "--hash"))
        return read_stdin(GOAT_MODULE_METADATA, 4);
    if (argc == 2 && !strcmp(argv[1], "--verify-hash"))
        return read_stdin(GOAT_MODULE_METADATA, 5);
    char hash[GOAT_MODULE_HASH_HEX_SIZE];
    memset(hash, 'x', sizeof(hash));
    assert(goat_module_main_hash(NULL, NULL, 0, hash) == GOAT_MODULE_JSON_INVALID_FIELD);
    for (size_t i = 0; i < sizeof(hash); i++)
        assert(!hash[i]);
    assert(goat_module_main_hash(NULL, NULL, 1, hash) == GOAT_MODULE_JSON_INVALID_FIELD);
    assert(goat_module_main_hash(NULL, NULL, 0, NULL) == GOAT_MODULE_JSON_INVALID_FIELD);
    bool matches = true;
    assert(goat_module_main_hash_verify(NULL, NULL, 0, &matches) == GOAT_MODULE_JSON_INVALID_FIELD);
    assert(!matches);
    assert(goat_module_main_hash_verify(NULL, NULL, 0, NULL) == GOAT_MODULE_JSON_INVALID_FIELD);
#if SIZE_MAX > UINT32_MAX
    assert(goat_module_main_hash(NULL, "", SIZE_MAX, hash) == GOAT_MODULE_JSON_LIMIT);
#endif
    yyjson_doc *info = (yyjson_doc *)(uintptr_t)1;
    assert(goat_module_information(NULL, GOAT_MODULE_METADATA, &info)
               == GOAT_MODULE_JSON_INVALID_FIELD
           && !info);
    const char *empty = "{}";
    yyjson_doc *bad_info = yyjson_read(empty, 2, 0);
    assert(goat_module_information(bad_info, GOAT_MODULE_METADATA, &info)
               == GOAT_MODULE_JSON_INVALID_FIELD
           && !info);
    assert(goat_module_information(bad_info, GOAT_MODULE_NATIVE, &info)
               == GOAT_MODULE_JSON_INVALID_FIELD
           && !info);
    assert(goat_module_information(bad_info, GOAT_MODULE_GMOD, NULL)
           == GOAT_MODULE_JSON_INVALID_FIELD);
    yyjson_doc_free(bad_info);
    canonical_api_checks();
    diagnostic_checks();
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
