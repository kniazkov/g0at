/** @file module_information_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Frozen information-field and interactive-merge contracts (#364).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/format_binary.h"
#include "test/support/format_json.h"
#include "test/support/format_zip.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"
#include "yyjson.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#define REQUIRE(x) scenario_require(r, (x), #x)

typedef struct {
    const char *reader, *writer, *nm, *vectors;
    yyjson_val *fixture;
} configuration_t;

typedef struct {
    const unsigned char *data;
    size_t size;
} bytes_t;

static bool free_doc(void *p) {
    yyjson_doc_free(p);
    return true;
}

static bool free_mut(void *p) {
    yyjson_mut_doc_free(p);
    return true;
}

static bool free_malloc(void *p) {
    free(p);
    return true;
}

static const char *field(scenario_run_t *r, yyjson_val *obj, const char *key) {
    yyjson_val *v = yyjson_obj_get(obj, key);
    const char *s = yyjson_get_str(v);
    REQUIRE(s && strlen(s) == yyjson_get_len(v));
    return s;
}

static unsigned digit(scenario_run_t *r, char c) {
    if (c >= '0' && c <= '9')
        return (unsigned)(c - '0');
    if (c >= 'a' && c <= 'f')
        return (unsigned)(c - 'a' + 10);
    REQUIRE(false);
    return 0;
}

static bytes_t hex(scenario_run_t *r, yyjson_val *v) {
    REQUIRE(yyjson_is_str(v) && !(yyjson_get_len(v) % 2));
    size_t n = yyjson_get_len(v) / 2;
    unsigned char *data = language_owned(r, ALLOC(n + 1));
    const char *s = yyjson_get_str(v);
    for (size_t i = 0; i < n; i++)
        data[i] = (unsigned char)(digit(r, s[i * 2]) * 16 + digit(r, s[i * 2 + 1]));
    data[n] = 0;
    return (bytes_t){data, n};
}

static yyjson_mut_doc *parse(scenario_run_t *r, const void *data, size_t n) {
    char error[256] = {0};
    yyjson_mut_doc *d = format_json_parse(data, n, error, sizeof(error));
    scenario_require(r, d != NULL, error);
    scenario_defer(r, free_mut, d);
    return d;
}

static yyjson_mut_val *expected(scenario_run_t *r, yyjson_val *value) {
    REQUIRE(value);
    size_t size;
    char *s = yyjson_val_write(value, 0, &size);
    REQUIRE(s);
    scenario_defer(r, free_malloc, s);
    return yyjson_mut_doc_get_root(parse(r, s, size));
}

static void json_equal(scenario_run_t *r, yyjson_val *want, const void *data, size_t size) {
    yyjson_mut_val *actual = yyjson_mut_doc_get_root(parse(r, data, size));
    scenario_require(r,
                     format_json_equal(expected(r, want), actual),
                     "JSON differs from validated original expectation");
}

static bytes_t extract(scenario_run_t *r, bytes_t zip, const char *name) {
    format_zip_info_t info;
    char error[256];
    REQUIRE(format_zip_inspect(zip.data, zip.size, SIZE_MAX, &info, error, sizeof(error)));
    size_t count = info.count;
    bytes_t found = {0};
    for (size_t i = 0; i < count; i++) {
        REQUIRE(format_zip_inspect(zip.data, zip.size, i, &info, error, sizeof(error)));
        if (info.name_size != strlen(name) || memcmp(info.name, name, info.name_size))
            continue;
        REQUIRE(!found.data);
        unsigned char *data = format_zip_extract(zip.data,
                                                 zip.size,
                                                 i,
                                                 16 * 1024 * 1024,
                                                 &found.size,
                                                 error,
                                                 sizeof(error));
        REQUIRE(data);
        scenario_defer(r, free_malloc, data);
        found.data = data;
    }
    REQUIRE(found.data);
    return found;
}

static size_t occurrences(const void *raw, size_t size, const char *part) {
    size_t n = strlen(part), count = 0;
    if (!n || n > size)
        return 0;
    for (size_t i = 0; i <= size - n; i++)
        if (!memcmp((const unsigned char *)raw + i, part, n)) {
            count++;
            i += n - 1;
        }
    return count;
}

static void
hash(scenario_run_t *r, configuration_t *c, bytes_t input, scenario_command_result_t output) {
    const unsigned char *zero = memchr(input.data, 0, input.size);
    REQUIRE(zero);
    size_t metadata_size = (size_t)(zero - input.data);
    yyjson_mut_doc *m = parse(r, input.data, metadata_size);
    size_t schema_size, code_size;
    const char *schema =
        language_read(r, language_join(r, c->vectors, "../schema-source.json"), &schema_size);
    yyjson_mut_doc *s = parse(r, schema, schema_size);
    const char *code =
        language_read(r, language_join(r, c->vectors, "empty.bytecode.bin"), &code_size);
    scenario_require_bytes(r,
                           "hash input bytecode",
                           code,
                           code_size,
                           zero + 1,
                           input.size - metadata_size - 1,
                           false);
    char value[65], error[256] = {0};
    REQUIRE(format_binary_main_hash(yyjson_mut_doc_get_root(m),
                                    yyjson_mut_doc_get_root(s),
                                    zero + 1,
                                    code_size,
                                    value,
                                    error,
                                    sizeof(error)));
    scenario_require_bytes(r,
                           "independent significant hash",
                           value,
                           64,
                           output.out,
                           output.out_size,
                           false);
}

static bool banned(const char *name) {
    const char *exact[] = {"socket", "connect", "getaddrinfo", "gethostbyname", "WSAStartup"};
    const char *prefix[] = {"WSASocket", "WinHttp", "Internet", "curl_"};
    for (size_t i = 0; i < sizeof(exact) / sizeof(exact[0]); i++)
        if (!strcmp(name, exact[i]))
            return true;
    for (size_t i = 0; i < sizeof(prefix) / sizeof(prefix[0]); i++)
        if (!strncmp(name, prefix[i], strlen(prefix[i])))
            return true;
    return false;
}

static void symbols(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    const char *args[] = {c->nm, "-a", c->reader, NULL};
    child_options_t options = {.executable = c->nm, .arguments = args};
    scenario_command_result_t result = scenario_command_capture(r, &options);
    REQUIRE(result.exit_code == 0);
    char *text = language_owned(r, ALLOC(result.out_size + 1));
    memcpy(text, result.out, result.out_size);
    text[result.out_size] = 0;
    REQUIRE(strlen(text) == result.out_size);
    char *line = text;
    while (*line) {
        char *end = strchr(line, '\n');
        if (end)
            *end = 0;
        char *last = line + strlen(line);
        while (last > line && isspace((unsigned char)last[-1]))
            *--last = 0;
        char *token = last;
        while (token > line && !isspace((unsigned char)token[-1]))
            token--;
        if (*token == '_') {
            while (*token == '_')
                token++;
            if (!strncmp(token, "imp_", 4)) {
                token += 3;
                while (*token == '_')
                    token++;
            }
        }
        char *decorated = strchr(token, '@');
        if (decorated)
            *decorated = 0;
        scenario_require(r, !banned(token), token);
        if (!end)
            break;
        line = end + 1;
    }
}

static void body(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    yyjson_val *ops = yyjson_obj_get(c->fixture, "operations"), *op;
    REQUIRE(yyjson_is_arr(ops) && yyjson_arr_size(ops));
    size_t count = yyjson_arr_size(ops), i, n;
    scenario_command_result_t *results = language_owned(r, CALLOC(count * sizeof(*results)));
    yyjson_arr_foreach(ops, i, n, op) {
        const char *mode = field(r, op, "mode");
        bool writer = !strcmp(mode, "writer");
        REQUIRE(writer || !strcmp(mode, "hash") || !strcmp(mode, "information-gmod")
                || !strcmp(mode, "information-module") || !strcmp(mode, "canonical-gmod")
                || !strcmp(mode, "canonical-module") || !strcmp(mode, "information-merge"));
        bytes_t input = hex(r, yyjson_obj_get(op, "inputHex"));
        char *option = language_owned(r, ALLOC(strlen(mode) + 3));
        strcpy(option, "--");
        strcat(option, mode);
        const char *args[] = {writer ? c->writer : c->reader,
                              writer ? "memory" : option,
                              writer ? "unused" : NULL,
                              NULL};
        child_options_t options = {.executable = args[0],
                                   .arguments = args,
                                   .stdin_data = input.data,
                                   .stdin_size = input.size};
        scenario_command_result_t result = scenario_command_capture(r, &options);
        results[i] = result;
        yyjson_val *status = yyjson_obj_get(op, "exitCode");
        REQUIRE(yyjson_is_uint(status) && yyjson_get_uint(status) <= 1);
        REQUIRE(result.exit_code == (int64_t)yyjson_get_uint(status));
        yyjson_val *out = yyjson_obj_get(op, "outputHex"), *want = yyjson_obj_get(op, "expected");
        if (out) {
            bytes_t bytes = hex(r, out);
            scenario_require_bytes(r,
                                   writer ? "writer archive" : "reader output",
                                   bytes.data,
                                   bytes.size,
                                   result.out,
                                   result.out_size,
                                   !writer);
        }
        if (result.exit_code) {
            REQUIRE(!result.out_size);
            if (!strcmp(mode, "information-merge"))
                REQUIRE(result.err_size);
        } else if (writer) {
            bytes_t archive = {(const unsigned char *)result.out, result.out_size};
            bytes_t metadata = extract(r, archive, "module.json"),
                    code = extract(r, archive, "bytecode.bin");
            json_equal(r, want, metadata.data, metadata.size);
            size_t code_size;
            const char *golden_code =
                language_read(r, language_join(r, c->vectors, "empty.bytecode.bin"), &code_size);
            scenario_require_bytes(r,
                                   "writer preserves bytecode",
                                   golden_code,
                                   code_size,
                                   code.data,
                                   code.size,
                                   false);
            if (yyjson_is_true(yyjson_obj_get(c->fixture, "golden"))) {
                size_t size;
                const char *golden =
                    language_read(r, language_join(r, c->vectors, "empty.gbin"), &size);
                scenario_require_bytes(r,
                                       "empty information golden archive",
                                       golden,
                                       size,
                                       result.out,
                                       result.out_size,
                                       false);
            }
        } else if (!strcmp(mode, "hash"))
            hash(r, c, input, result);
        else
            json_equal(r, want, result.out, result.out_size);
        yyjson_val *stderr_value = yyjson_obj_get(op, "stderr");
        if (stderr_value) {
            REQUIRE(yyjson_is_str(stderr_value));
            scenario_require_bytes(r,
                                   "merge prompts/diagnostics",
                                   yyjson_get_str(stderr_value),
                                   yyjson_get_len(stderr_value),
                                   result.err,
                                   result.err_size,
                                   true);
        }
    }
    const char *relations[] = {"equal", "different"};
    for (size_t kind = 0; kind < 2; kind++) {
        yyjson_val *pairs = yyjson_obj_get(c->fixture, relations[kind]), *pair;
        REQUIRE(yyjson_is_arr(pairs));
        yyjson_arr_foreach(pairs, i, n, pair) {
            REQUIRE(yyjson_is_arr(pair) && yyjson_arr_size(pair) == 2);
            size_t a = (size_t)yyjson_get_uint(yyjson_arr_get(pair, 0)),
                   b = (size_t)yyjson_get_uint(yyjson_arr_get(pair, 1));
            REQUIRE(a < count && b < count);
            bool same;
            if (!strcmp(field(r, yyjson_arr_get(ops, a), "mode"), "writer"))
                same = results[a].out_size == results[b].out_size
                       && !memcmp(results[a].out, results[b].out, results[a].out_size);
            else
                same = format_json_equal(
                    yyjson_mut_doc_get_root(parse(r, results[a].out, results[a].out_size)),
                    yyjson_mut_doc_get_root(parse(r, results[b].out, results[b].out_size)));
            scenario_require(r,
                             kind ? !same : same,
                             kind ? "significant text distinctions collapsed"
                                  : "normalization/idempotence relation changed");
        }
    }
    yyjson_val *overlay = yyjson_obj_get(c->fixture, "overlay"), *triple;
    yyjson_arr_foreach(overlay, i, n, triple) {
        REQUIRE(yyjson_arr_size(triple) == 3);
        size_t indices[3];
        yyjson_mut_val *v[3];
        for (size_t j = 0; j < 3; j++) {
            indices[j] = (size_t)yyjson_get_uint(yyjson_arr_get(triple, j));
            REQUIRE(indices[j] < count);
            v[j] = yyjson_mut_doc_get_root(
                parse(r, results[indices[j]].out, results[indices[j]].out_size));
            REQUIRE(yyjson_mut_is_obj(v[j]));
        }
        REQUIRE(yyjson_mut_obj_size(v[0]) == yyjson_mut_obj_size(v[1]) + yyjson_mut_obj_size(v[2]));
        for (size_t j = 1; j < 3; j++) {
            size_t index, total;
            yyjson_mut_val *key, *value;
            yyjson_mut_obj_foreach(v[j], index, total, key, value) {
                yyjson_mut_val *actual =
                    yyjson_mut_obj_getn(v[0], yyjson_mut_get_str(key), yyjson_mut_get_len(key));
                REQUIRE(actual && format_json_equal(actual, value));
            }
        }
    }
    yyjson_val *counts = yyjson_obj_get(c->fixture, "menuCounts"), *key, *value;
    if (counts) {
        REQUIRE(yyjson_is_obj(counts));
        size_t index, total;
        yyjson_obj_foreach(counts, index, total, key, value) {
            REQUIRE(yyjson_is_uint(value));
            REQUIRE(occurrences(results[count - 1].err,
                                results[count - 1].err_size,
                                yyjson_get_str(key))
                    == yyjson_get_uint(value));
        }
    }
    yyjson_val *forbidden = yyjson_obj_get(c->fixture, "forbiddenStderr"), *fragment;
    yyjson_arr_foreach(forbidden, i, n, fragment) {
        REQUIRE(yyjson_is_str(fragment));
        for (size_t j = 0; j < count; j++)
            REQUIRE(!occurrences(results[j].err, results[j].err_size, yyjson_get_str(fragment)));
    }
    printf("%s: %zu host checks passed\n", field(r, c->fixture, "id"), count);
}

int main(int argc, char **argv) {
#ifdef _WIN32
    fs_error_t ae = {0};
    if (!fs_arguments(&argc, &argv, &ae)) {
        clear_fs_error(&ae);
        return 1;
    }
#endif
    int result = 1;
    if (argc == 9 || argc == 10) {
        fs_error_t e = {0};
        char *reader = absolute_fs_path(argv[2], &e);
        clear_fs_error(&e);
        char *writer = absolute_fs_path(argv[3], &e);
        clear_fs_error(&e);
        yyjson_doc *docs[2] = {0};
        void *data[2] = {0};
        const char *files[] = {"information.json", "information_merge.json"};
        size_t count = 1;
        bool ok = reader && writer;
        for (size_t j = 0; j < 2; j++) {
            char relative[80];
            snprintf(relative, sizeof(relative), "module-information/%s", files[j]);
            char *path = join_fs_path(argv[7], relative, &e);
            clear_fs_error(&e);
            size_t size = 0;
            bool read = path && read_fs_regular_file(path, 2 * 1024 * 1024, &data[j], &size, &e);
            clear_fs_error(&e);
            FREE(path);
            docs[j] = read ? yyjson_read(data[j], size, 0) : NULL;
            yyjson_val *root = yyjson_doc_get_root(docs[j]);
            ok = ok && yyjson_is_arr(root) && yyjson_arr_size(root);
            count += yyjson_arr_size(root);
        }
        if (ok) {
            configuration_t *c = CALLOC(count * sizeof(*c));
            scenario_definition_t *defs = CALLOC(count * sizeof(*defs));
            c[0] = (configuration_t){reader, writer, argv[4], argv[8], NULL};
            defs[0] = (scenario_definition_t){"integration.module_information.no-network-symbols",
                                              120000,
                                              symbols,
                                              &c[0],
                                              NULL};
            size_t index = 1;
            for (size_t j = 0; j < 2; j++) {
                yyjson_val *root = yyjson_doc_get_root(docs[j]);
                for (size_t i = 0; i < yyjson_arr_size(root); i++) {
                    yyjson_val *v = yyjson_arr_get(root, i);
                    c[index] = (configuration_t){reader, writer, argv[4], argv[8], v};
                    defs[index] = (scenario_definition_t){yyjson_get_str(yyjson_obj_get(v, "id")),
                                                          120000,
                                                          body,
                                                          &c[index],
                                                          NULL};
                    index++;
                }
            }
            scenario_options_t options = {.build_directory = argv[5], .data_directory = argv[6]};
            const char *selection[] = {argv[1], argc == 10 ? argv[9] : NULL};
            result = scenario_registry_main(defs, count, argc == 10 ? 2 : 1, selection, &options);
            FREE(defs);
            FREE(c);
        } else
            fputs("Cannot load information fixtures\n", stderr);
        for (size_t j = 0; j < 2; j++) {
            yyjson_doc_free(docs[j]);
            FREE(data[j]);
        }
        FREE(reader);
        FREE(writer);
    } else
        fputs("usage: module_information_scenarios ID READER WRITER NM BUILD DATA FIXTURES VECTORS "
              "[--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
