/** @file native_requirements_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent frozen native-image inspection and runtime stack scenarios (#379).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"
#include "yyjson.h"

#include <string.h>
#ifdef __linux__
#    include <sys/resource.h>
#    include <unistd.h>
#endif
#define REQUIRE(x) scenario_require(r, (x), #x)

typedef struct {
    const char *host, *self, *fixtures, *stack;
    yyjson_val *image;
} config_t;

static const char *field(scenario_run_t *r, yyjson_val *object, const char *name) {
    yyjson_val *value = yyjson_obj_get(object, name);
    const char *text = yyjson_get_str(value);
    REQUIRE(text && strlen(text) == yyjson_get_len(value));
    return text;
}

static bool utf8(const unsigned char *p, size_t n) {
    for (size_t i = 0; i < n;) {
        unsigned c = p[i++], value, remaining, minimum;
        if (c < 128)
            continue;
        if (c >= 0xc2 && c <= 0xdf) {
            value = c & 31;
            remaining = 1;
            minimum = 128;
        } else if (c >= 0xe0 && c <= 0xef) {
            value = c & 15;
            remaining = 2;
            minimum = 2048;
        } else if (c >= 0xf0 && c <= 0xf4) {
            value = c & 7;
            remaining = 3;
            minimum = 65536;
        } else
            return false;
        if (remaining > n - i)
            return false;
        while (remaining--) {
            c = p[i++];
            if ((c & 0xc0) != 0x80)
                return false;
            value = (value << 6) | (c & 63);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
            return false;
    }
    return true;
}

static bool contains(const unsigned char *bytes, size_t size, const char *text) {
    size_t length = strlen(text);
    if (length > size)
        return false;
    for (size_t i = 0; i <= size - length; i++)
        if (!memcmp(bytes + i, text, length))
            return true;
    return false;
}

/* Python text=True performs universal-newline translation, not whitespace trimming. */
static size_t newline_text(unsigned char *bytes, size_t size) {
    size_t out = 0;
    for (size_t i = 0; i < size; i++) {
        unsigned char c = bytes[i];
        if (c == '\r') {
            if (i + 1 < size && bytes[i + 1] == '\n')
                i++;
            c = '\n';
        }
        bytes[out++] = c;
    }
    return out;
}

typedef struct {
    child_options_t options;
    scenario_command_result_t result;
} command_t;

static void command(scenario_run_t *r, void *user) {
    command_t *call = user;
    call->result = scenario_command_capture(r, &call->options);
}

static void body(scenario_run_t *r, void *user) {
    config_t *c = user;
    const char *work = scenario_work_directory(scenario_run_context(r));
    const char *arguments[5] = {c->host, NULL, NULL, NULL, NULL};
    const char *expected = NULL;
    if (c->stack) {
        if (!strcmp(c->stack, "limited")) {
            arguments[0] = c->self;
            arguments[1] = "--limit-stack";
            arguments[2] = c->host;
            expected = "0\n";
        } else {
            char *mode = language_owned(r, ALLOC(strlen(c->stack) + 9));
            sprintf(mode, "--stack-%s", c->stack);
            arguments[1] = mode;
            expected = !strcmp(c->stack, "small") ? "0\n" : "1\n";
        }
    } else {
        yyjson_val *files = yyjson_obj_get(c->image, "files");
        REQUIRE(yyjson_is_obj(files) && yyjson_obj_size(files) > 0);
        size_t index, count;
        yyjson_val *key, *value;
        yyjson_obj_foreach(files, index, count, key, value) {
            const char *name = yyjson_get_str(key), *path = yyjson_get_str(value);
            REQUIRE(name && strlen(name) == yyjson_get_len(key));
            REQUIRE(path && strlen(path) == yyjson_get_len(value));
            REQUIRE(!strchr(name, '/') && !strchr(name, '\\') && strcmp(name, ".")
                    && strcmp(name, ".."));
            REQUIRE(!strncmp(path, "blobs/", 6) && !strstr(path, "..") && !strchr(path, '\\'));
            size_t size;
            const char *bytes = language_read(r, language_join(r, c->fixtures, path), &size);
            language_write(r, language_join(r, work, name), bytes, size);
        }
        arguments[1] = language_join(r, work, "image.bin");
        arguments[2] = work;
        arguments[3] = field(r, c->image, "target");
    }
    child_options_t options = {.executable = arguments[0],
                               .arguments = arguments,
                               .working_directory = work,
                               .empty_stdin = true};
    command_t call = {.options = options};
    scenario_with_timeout(r, 10000, command, &call);
    scenario_command_result_t result = call.result;
    REQUIRE(utf8(result.out, result.out_size));
    REQUIRE(utf8(result.err, result.err_size));
    if (c->stack) {
        REQUIRE(result.exit_code == 0);
        unsigned char *output = language_owned(r, ALLOC(result.out_size + 1));
        memcpy(output, result.out, result.out_size);
        size_t size = newline_text(output, result.out_size);
        if (!strcmp(c->stack, "blocked")) {
            REQUIRE((size == 2 && !memcmp(output, "0\n", 2))
                    || (size == 9 && !memcmp(output, "not-musl\n", 9)));
        } else
            scenario_require_bytes(r,
                                   "stack stdout",
                                   expected,
                                   strlen(expected),
                                   output,
                                   size,
                                   false);
    } else {
        yyjson_val *succeeds = yyjson_obj_get(c->image, "succeeds");
        REQUIRE(yyjson_is_bool(succeeds));
        REQUIRE((result.exit_code == 0) == yyjson_get_bool(succeeds));
        unsigned char *combined = language_owned(r, ALLOC(result.out_size + result.err_size + 1));
        memcpy(combined, result.out, result.out_size);
        size_t size = newline_text(combined, result.out_size);
        memcpy(combined + size, result.err, result.err_size);
        size += newline_text(combined + size, result.err_size);
        yyjson_val *tokens = yyjson_obj_get(c->image, "contains");
        REQUIRE(yyjson_is_arr(tokens));
        size_t index, count;
        yyjson_val *token;
        yyjson_arr_foreach(tokens, index, count, token) {
            const char *text = yyjson_get_str(token);
            REQUIRE(text && strlen(text) == yyjson_get_len(token));
            REQUIRE(contains(combined, size, text));
        }
    }
}

int main(int argc, char **argv) {
#ifdef _WIN32
    if (!fs_arguments(&argc, &argv, NULL))
        return 1;
#endif
#ifdef __linux__
    if (argc == 3 && !strcmp(argv[1], "--limit-stack")) {
        struct rlimit limit = {128 * 1024, 128 * 1024};
        if (setrlimit(RLIMIT_STACK, &limit)) {
            perror("setrlimit RLIMIT_STACK");
            return 2;
        }
        const char *arguments[] = {argv[2], "--stack-main", NULL};
        execv(argv[2], (char *const *)arguments);
        perror("exec stack fixture");
        return 2;
    }
#endif
    int result = 1;
    if (argc == 6 || (argc == 7 && !strcmp(argv[6], "--keep-work"))) {
        fs_error_t error = {0};
        char *host = absolute_fs_path(argv[2], &error);
        clear_fs_error(&error);
        char *self = absolute_fs_path(argv[0], &error);
        clear_fs_error(&error);
        char *path = join_fs_path(argv[5], "scenarios.json", &error);
        clear_fs_error(&error);
        void *data = NULL;
        size_t size = 0;
        bool read = path && read_fs_regular_file(path, 1024 * 1024, &data, &size, &error);
        clear_fs_error(&error);
        yyjson_doc *doc = read ? yyjson_read(data, size, 0) : NULL;
        yyjson_val *images = yyjson_doc_get_root(doc);
        size_t count = yyjson_arr_size(images);
        if (host && self && yyjson_is_arr(images) && count) {
            const char *stacks[] = {"main", "worker", "small", "limited", "blocked"};
            const char *ids[] = {"integration.native_requirements.stack-main",
                                 "integration.native_requirements.stack-worker",
                                 "integration.native_requirements.stack-small",
                                 "integration.native_requirements.stack-limited",
                                 "integration.native_requirements.stack-blocked"};
            config_t *configs = CALLOC((count + 5) * sizeof(*configs));
            scenario_definition_t *defs = CALLOC((count + 5) * sizeof(*defs));
            for (size_t i = 0; i < count + 5; i++) {
                configs[i] = (config_t){host, self, argv[5], NULL, NULL};
                defs[i] = (scenario_definition_t){NULL, 20000, body, configs + i, NULL};
                if (i < count) {
                    configs[i].image = yyjson_arr_get(images, i);
                    defs[i].id = yyjson_get_str(yyjson_obj_get(configs[i].image, "id"));
                } else {
                    configs[i].stack = stacks[i - count];
                    defs[i].id = ids[i - count];
#ifndef __linux__
                    defs[i].skip_reason = "Linux stack mapping/RLIMIT contract";
#endif
                }
            }
            scenario_options_t options = {.build_directory = argv[3], .data_directory = argv[4]};
            const char *selection[] = {argv[1], argc == 7 ? argv[6] : NULL};
            result =
                scenario_registry_main(defs, count + 5, argc == 7 ? 2 : 1, selection, &options);
            FREE(defs);
            FREE(configs);
        } else
            fputs("Cannot load native-requirements fixtures\n", stderr);
        yyjson_doc_free(doc);
        FREE(data);
        FREE(path);
        FREE(host);
        FREE(self);
    } else
        fputs("usage: native_requirements_scenarios ID HOST BUILD DATA FIXTURES [--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
