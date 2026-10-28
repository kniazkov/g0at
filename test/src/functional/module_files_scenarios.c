/** @file module_files_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief File identity and VM entry fixtures replacing check_module_files.py.
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/format_json.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"

#include <stdlib.h>
#include <string.h>

#define N(a) (sizeof(a) / sizeof((a)[0]))

enum { OK, ARGUMENT, JSON, IMAGE, FILES };

typedef struct {
    unsigned char *data;
    size_t size;
} bytes_t;

typedef struct {
    const char *exe, *vectors;
    size_t count;
    yyjson_mut_doc *base;
} configuration_t;

static bool release_doc(void *p) {
    yyjson_mut_doc_free(p);
    return true;
}

static bool release_malloc(void *p) {
    free(p);
    return true;
}

static void *owned(scenario_run_t *r, void *p) {
    scenario_require(r, p != NULL, "allocation");
    scenario_defer(r, release_malloc, p);
    return p;
}

static bytes_t read_file(scenario_run_t *r, configuration_t *c, const char *name) {
    size_t n;
    const char *p = language_read(r, language_join(r, c->vectors, name), &n);
    return (bytes_t){(unsigned char *)p, n};
}

static yyjson_mut_doc *parse(scenario_run_t *r, const void *data, size_t size) {
    yyjson_doc *source = yyjson_read(data, size, 0);
    scenario_require(r, source != NULL, "fixture JSON parse");
    yyjson_mut_doc *d = yyjson_doc_mut_copy(source, NULL);
    yyjson_doc_free(source);
    scenario_require(r, d != NULL, "fixture JSON mutable copy");
    scenario_defer(r, release_doc, d);
    return d;
}

static yyjson_mut_doc *clone(scenario_run_t *r, yyjson_mut_doc *d) {
    yyjson_mut_doc *p = yyjson_mut_doc_mut_copy(d, NULL);
    scenario_require(r, p != NULL, "JSON copy");
    scenario_defer(r, release_doc, p);
    return p;
}

static yyjson_mut_val *root(yyjson_mut_doc *d) {
    return yyjson_mut_doc_get_root(d);
}

static yyjson_mut_val *field(yyjson_mut_doc *d, const char *key) {
    return yyjson_mut_obj_get(root(d), key);
}

static yyjson_mut_val *item(yyjson_mut_doc *d, const char *key, size_t i) {
    return yyjson_mut_arr_get(field(d, key), i);
}

static void
set(scenario_run_t *r, yyjson_mut_doc *d, yyjson_mut_val *obj, const char *key, yyjson_mut_val *v) {
    scenario_require(r, v && yyjson_mut_obj_put(obj, yyjson_mut_str(d, key), v), "JSON set");
}

static void literal(scenario_run_t *r,
                    yyjson_mut_doc *d,
                    yyjson_mut_val *obj,
                    const char *key,
                    const char *value) {
    yyjson_mut_doc *p = parse(r, value, strlen(value));
    set(r, d, obj, key, yyjson_mut_val_mut_copy(d, root(p)));
}

static yyjson_mut_doc *
changed(scenario_run_t *r, yyjson_mut_doc *d, const char *key, const char *value) {
    d = clone(r, d);
    literal(r, d, root(d), key, value);
    return d;
}

static void put(unsigned char *p, uint32_t v) {
    for (unsigned i = 0; i < 4; i++)
        p[i] = (unsigned char)(v >> (8 * i));
}

/* Encode independently of production writers: sequential file ranges, explicit
 * entry offsets, locations and one UTF-32LE string. Metadata names deliberately
 * do not exist on disk: VM entry uses file IDs, never source I/O. */
static bytes_t image(scenario_run_t *r,
                     configuration_t *c,
                     size_t count,
                     unsigned padding,
                     yyjson_mut_doc **metadata) {
    size_t ni = count * (padding + 3), od = 64 + ni * 8, of = od + 16;
    size_t on = of + count * 16, start = on + count * 24, size = start + 8;
    bytes_t b = {owned(r, calloc(1, size)), size};
    memcpy(b.data, "GOATBC01", 8);
    uint32_t header[] = {1,
                         64,
                         (uint32_t)size,
                         (uint32_t)ni,
                         1,
                         (uint32_t)count,
                         0,
                         (uint32_t)count,
                         64,
                         (uint32_t)od,
                         (uint32_t)of,
                         (uint32_t)on,
                         (uint32_t)on,
                         (uint32_t)start};
    for (size_t i = 0; i < N(header); i++)
        put(b.data + 8 + 4 * i, header[i]);
    put(b.data + od + 4, 8);
    put(b.data + od + 8, 1);
    put(b.data + od + 12, 1);
    put(b.data + start, 'x');
    *metadata = clone(r, c->base);
    yyjson_mut_doc *d = *metadata;
    yyjson_mut_val *files = yyjson_mut_arr(d);
    set(r, d, root(d), "files", files);
    for (size_t i = 0; i < count; i++) {
        uint32_t first = (uint32_t)i * (padding + 3), entry = first + padding;
        unsigned char *ins = b.data + 64 + 8 * entry;
        ins[0] = 10;
        put(ins + 4, (uint32_t)(100 + i));
        ins[8] = 16;
        ins[16] = 2;
        unsigned char *file = b.data + of + 16 * i;
        put(file, first);
        put(file + 4, padding + 3);
        put(file + 8, entry);
        unsigned char *location = b.data + on + 24 * i;
        put(location, first);
        put(location + 4, padding + 3);
        put(location + 8, (uint32_t)i);
        put(location + 12, 1);
        put(location + 16, 1);
        yyjson_mut_val *v = yyjson_mut_obj(d);
        char name[64];
        snprintf(name, sizeof(name), "absent-%zu.goat", i);
        set(r, d, v, "name", yyjson_mut_strcpy(d, name));
        set(r, d, v, "entry", yyjson_mut_uint(d, entry));
        set(r, d, v, "external", yyjson_mut_false(d));
        scenario_require(r, yyjson_mut_arr_append(files, v), "append file metadata");
    }
    return b;
}

static void check_raw(scenario_run_t *r,
                      configuration_t *c,
                      bytes_t json,
                      bytes_t code,
                      int status,
                      unsigned main,
                      unsigned entry) {
    bytes_t input = {owned(r, malloc(4 + json.size + code.size)), 4 + json.size + code.size};
    put(input.data, (uint32_t)json.size);
    memcpy(input.data + 4, json.data, json.size);
    memcpy(input.data + 4 + json.size, code.data, code.size);
    const char *args[] = {c->exe, "run", NULL};
    child_options_t options = {.executable = c->exe,
                               .arguments = args,
                               .stdin_data = input.data,
                               .stdin_size = input.size};
    c->count++;
    scenario_command_result_t result = scenario_command_capture(r, &options);
    char label[120];
    snprintf(label,
             sizeof(label),
             "file case %zu: expected status %d, got %lld",
             c->count,
             status,
             (long long)result.exit_code);
    scenario_require(r, result.exit_code == status, label);
    if (status)
        scenario_require_bytes(r, "failure stdout", "", 0, result.out, result.out_size, false);
    else {
        char expected[64];
        int n = snprintf(expected, sizeof(expected), "%u %u\n", main, entry);
        scenario_require_bytes(r,
                               "file and entry",
                               expected,
                               (size_t)n,
                               result.out,
                               result.out_size,
                               true);
    }
}

static void
check(scenario_run_t *r, configuration_t *c, yyjson_mut_doc *d, bytes_t code, int status) {
    size_t n;
    char *p = yyjson_mut_write(d, 0, &n);
    unsigned main = status ? 0 : (unsigned)yyjson_mut_get_uint(field(d, "main"));
    unsigned entry =
        status ? 0
               : (unsigned)yyjson_mut_get_uint(yyjson_mut_obj_get(item(d, "files", main), "entry"));
    check_raw(r, c, (bytes_t){owned(r, p), n}, code, status, main, entry);
}

static void body(scenario_run_t *r, void *user) {
    configuration_t *c = user;
    const char *args[] = {c->exe, NULL};
    child_options_t options = {.executable = c->exe, .arguments = args};
    scenario_command(r, &options, 0);
    bytes_t base = read_file(r, c, "empty.module.json");
    c->base = parse(r, base.data, base.size);
    const size_t counts[] = {1, 2, 3, 20};
    for (size_t i = 0; i < N(counts); i++) {
        for (unsigned padding = 0; padding <= 2; padding += 2) {
            yyjson_mut_doc *d;
            bytes_t code = image(r, c, counts[i], padding, &d);
            for (size_t main = 0; main < counts[i]; main++) {
                set(r, d, root(d), "main", yyjson_mut_uint(d, main));
                check(r, c, d, code, OK);
            }
        }
    }
    yyjson_mut_doc *d;
    bytes_t code = image(r, c, 3, 1, &d);
    const char *bad_main[] = {"3", "-1", "4294967295", "null", "\"1\"", "1.0", "true"};
    for (size_t i = 0; i < N(bad_main); i++)
        check(r, c, changed(r, d, "main", bad_main[i]), code, JSON);
    const char *bad_entry[] = {"0", "999", "2", "5"};
    for (size_t i = 0; i < N(bad_entry); i++) {
        yyjson_mut_doc *bad = clone(r, d);
        literal(r, bad, item(bad, "files", 0), "entry", bad_entry[i]);
        check(r, c, bad, code, FILES);
    }
    const char *fields[] = {"entry", "entry", "entry", "external", "name", "id"};
    const char *values[] = {"null", "1.0", "-1", "\"true\"", "null", "0"};
    for (size_t i = 0; i < N(fields); i++) {
        yyjson_mut_doc *bad = clone(r, d);
        literal(r, bad, item(bad, "files", 0), fields[i], values[i]);
        check(r, c, bad, code, JSON);
    }
    const char *names[] = {"same.goat", "Ж😀.goat"};
    for (size_t i = 0; i < N(names); i++) {
        yyjson_mut_doc *same = clone(r, d);
        for (size_t j = 0; j < 3; j++) {
            yyjson_mut_val *v = item(same, "files", j);
            set(r, same, v, "name", yyjson_mut_strcpy(same, names[i]));
            set(r, same, v, "external", yyjson_mut_true(same));
        }
        check(r, c, same, code, OK);
        set(r, same, item(same, "files", 0), "external", yyjson_mut_false(same));
        check(r, c, same, code, OK);
        set(r, same, item(same, "files", 1), "external", yyjson_mut_false(same));
        check(r, c, same, code, JSON);
    }
    check(r, c, d, (bytes_t){code.data, code.size - 1}, IMAGE);
    check(r, c, d, (bytes_t){owned(r, calloc(1, code.size)), code.size}, IMAGE);
    check_raw(r, c, (bytes_t){(unsigned char *)"{}", 2}, code, JSON, 0, 0);
    scenario_require(r, c->count == 78, "complete legacy case count");
    printf("Module file scopes: %zu fixtures passed\n", c->count);
}

int main(int argc, char **argv) {
#ifdef _WIN32
    fs_error_t argument_error = {0};
    if (!fs_arguments(&argc, &argv, &argument_error)) {
        clear_fs_error(&argument_error);
        return 1;
    }
#endif
    int result = 1;
    if (argc == 2 && !strcmp(argv[1], "--list")) {
        puts("integration.module_files");
        result = 0;
    } else if (argc == 6 || argc == 7) {
        fs_error_t error = {0};
        char *exe = absolute_fs_path(argv[2], &error);
        clear_fs_error(&error);
        char *vectors = absolute_fs_path(argv[5], &error);
        clear_fs_error(&error);
        if (exe && vectors) {
            configuration_t c = {.exe = exe, .vectors = vectors};
            scenario_definition_t definition = {"integration.module_files", 180000, body, &c, NULL};
            scenario_options_t options = {.build_directory = argv[3], .data_directory = argv[4]};
            const char *selection[] = {argv[1], argc == 7 ? argv[6] : NULL};
            result = scenario_registry_main(&definition, 1, argc == 7 ? 2 : 1, selection, &options);
        }
        FREE(vectors);
        FREE(exe);
    } else
        fputs("usage: module_files_scenarios ID HOST BUILD DATA VECTORS [--keep-work]\n", stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
