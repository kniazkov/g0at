/** @file module_abi_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent descriptor, scalar guard and public ABI scenarios.
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/format_metadata.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#define G yyjson_mut_obj_get
#define A yyjson_mut_arr_get
#define N(a) (sizeof(a) / sizeof((a)[0]))

typedef struct {
    const char *exe, *vectors, *data;
    yyjson_mut_doc *base;
    size_t count;
} config_t;

static bool free_malloc(void *p) {
    free(p);
    return true;
}

static void *owned(scenario_run_t *r, void *p) {
    scenario_require(r, p != NULL, "allocation");
    scenario_defer(r, free_malloc, p);
    return p;
}

static bool free_doc(void *p) {
    yyjson_mut_doc_free(p);
    return true;
}

static yyjson_mut_doc *parse(scenario_run_t *r, const char *s, size_t n) {
    yyjson_doc *q = yyjson_read(s, n, 0);
    scenario_require(r, q != NULL, "JSON input");
    yyjson_mut_doc *d = yyjson_doc_mut_copy(q, NULL);
    yyjson_doc_free(q);
    scenario_require(r, d != NULL, "JSON copy");
    scenario_defer(r, free_doc, d);
    return d;
}

static yyjson_mut_doc *copy(scenario_run_t *r, yyjson_mut_doc *d) {
    yyjson_mut_doc *p = yyjson_mut_doc_mut_copy(d, NULL);
    scenario_require(r, p != NULL, "JSON copy");
    scenario_defer(r, free_doc, p);
    return p;
}

static yyjson_mut_val *R(yyjson_mut_doc *d) {
    return yyjson_mut_doc_get_root(d);
}

static void set(yyjson_mut_doc *d, yyjson_mut_val *v, const char *k, yyjson_mut_val *x) {
    yyjson_mut_obj_put(v, yyjson_mut_str(d, k), x);
}

static char *canonical(scenario_run_t *r, yyjson_mut_val *v, size_t *n) {
    return owned(r, format_json_canonical(v, n));
}

static scenario_command_result_t run(scenario_run_t *r,
                                     config_t *c,
                                     const char *mode,
                                     const void *in,
                                     size_t n,
                                     const char *a,
                                     const char *b,
                                     const char *e) {
    const char *args[] = {c->exe, mode, a, b, e, NULL};
    child_options_t o = {.executable = c->exe,
                         .arguments = args,
                         .stdin_data = in,
                         .stdin_size = n};
    scenario_command_result_t out = scenario_command_capture(r, &o);
    scenario_require(r, out.exit_code == 0, "ABI host exit");
    c->count++;
    return out;
}

static int status(scenario_command_result_t v) {
    const unsigned char *s = v.out;
    size_t n = v.out_size;
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t'))
        n--;
    if (n != 1 || s[0] < '0' || s[0] > '9')
        return -999;
    return s[0] - '0';
}

static unsigned char *envelope(scenario_run_t *r, yyjson_mut_val *v, size_t *n) {
    size_t z;
    char *s = canonical(r, v, &z);
    unsigned char *p = owned(r, malloc(z + 17));
    memcpy(p, "GOATND01", 8);
    for (int i = 0; i < 8; i++)
        p[8 + i] = (unsigned char)((uint64_t)z >> (8 * i));
    memcpy(p + 16, s, z);
    p[z + 16] = 0;
    *n = z + 17;
    return p;
}

static void check(scenario_run_t *r, config_t *c, yyjson_mut_doc *d, int expected) {
    size_t n;
    unsigned char *p = envelope(r, R(d), &n);
    scenario_require(r,
                     status(run(r, c, "descriptor", p, n, NULL, NULL, NULL)) == expected,
                     "descriptor status");
}

static yyjson_mut_doc *spec(scenario_run_t *r, config_t *c, const char *kind) {
    yyjson_mut_doc *d = copy(r, c->base);
    char buf[1024];
    snprintf(buf,
             sizeof(buf),
             "[{\"specializationId\":1,\"functionId\":1,\"symbol\":\"goat_fn_1_v1\",\"signature\":{"
             "\"id\":\"goat.adapter.v1\",\"arguments\":[\"%s\"],\"result\":\"%s\"},\"when\":{"
             "\"argumentTypes\":[\"%s\"]}}]",
             kind,
             kind,
             kind);
    yyjson_mut_doc *s = parse(r, buf, strlen(buf));
    set(d, R(d), "specializations", yyjson_mut_val_mut_copy(d, R(s)));
    return d;
}

static void descriptors(scenario_run_t *r, config_t *c) {
    check(r, c, c->base, 0);
    size_t n;
    unsigned char *p = envelope(r, R(c->base), &n);
    size_t sizes[] = {0, 8, n - 1, n + 1, n, n, n, n, n};
    for (size_t i = 0; i < N(sizes); i++) {
        unsigned char *q = owned(r, calloc(n + 1, 1));
        memcpy(q, p, n);
        if (i == 4)
            memcpy(q, "BADMAGIC", 8);
        if (i == 5)
            q[n - 1] = 'X';
        if (i >= 6) {
            uint64_t z = i == 6 ? UINT64_MAX : i == 7 ? n : 0;
            for (int j = 0; j < 8; j++)
                q[8 + j] = (unsigned char)(z >> (8 * j));
        }
        scenario_require(r,
                         status(run(r, c, "descriptor", q, sizes[i], NULL, NULL, NULL)) == 2,
                         "bad envelope");
    }
    size_t z;
    char *raw = canonical(r, R(c->base), &z);
    const char *bad[] = {"{\"formatVersion\":1,\"formatVersion\":1}", "{}", "{\"x\":\"\xff\"}"};
    for (int i = 0; i < 6; i++) {
        const char *s;
        size_t len;
        char *q = owned(r, malloc(z * 2 + 8));
        if (i < 3) {
            if (i == 0) {
                bool quoted = false, escaped = false;
                len = 0;
                for (size_t j = 0; j < z; j++) {
                    char ch = raw[j];
                    q[len++] = ch;
                    if (escaped) {
                        escaped = false;
                        continue;
                    }
                    if (quoted && ch == '\\') {
                        escaped = true;
                        continue;
                    }
                    if (ch == '"')
                        quoted = !quoted;
                    if (!quoted && (ch == ',' || ch == ':'))
                        q[len++] = ' ';
                }
            } else if (i == 1) {
                memcpy(q, raw, z);
                q[z] = '\n';
                len = z + 1;
            } else {
                memcpy(q, "\xef\xbb\xbf", 3);
                memcpy(q + 3, raw, z);
                len = z + 3;
            }
            s = q;
        } else {
            s = bad[i - 3];
            len = strlen(s);
        }
        unsigned char *b = owned(r, calloc(len + 17, 1));
        memcpy(b, "GOATND01", 8);
        for (int j = 0; j < 8; j++)
            b[j + 8] = (unsigned char)((uint64_t)len >> (8 * j));
        memcpy(b + 16, s, len);
        scenario_require(r,
                         status(run(r, c, "descriptor", b, len + 17, NULL, NULL, NULL)) == 2,
                         "noncanonical JSON");
    }
    for (int i = 0; i < 2; i++) {
        yyjson_mut_doc *d = copy(r, c->base);
        set(d, R(d), "goatAbiVersion", yyjson_mut_uint(d, i ? UINT32_MAX : 2));
        check(r, c, d, 3);
    }
    size_t idx, max;
    yyjson_mut_val *k, *v;
    yyjson_mut_obj_foreach(R(c->base), idx, max, k, v) {
        yyjson_mut_doc *d = copy(r, c->base);
        yyjson_mut_obj_remove_str(R(d), yyjson_mut_get_str(k));
        check(r, c, d, 2);
    }
    const char *fields[] =
        {"formatVersion", "goatAbiVersion", "unknown", "mainHash", "module", "specializations"};
    const char *values[] = {"2",
                            "0",
                            "1",
                            "\"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\"",
                            "\"../module\"",
                            "[]"};
    for (size_t i = 0; i < N(fields); i++) {
        yyjson_mut_doc *d = copy(r, c->base), *x = parse(r, values[i], strlen(values[i]));
        set(d, R(d), fields[i], yyjson_mut_val_mut_copy(d, R(x)));
        check(r, c, d, 2);
    }
    const char *sf[] =
        {"symbol", "functionId", "functionId", "specializationId", "signature", "when"};
    const char *sv[] = {"\"goat_fn_2_v1\"", "0", "4294967296", "0", "{}", "{}"};
    for (size_t i = 0; i < N(sf); i++) {
        yyjson_mut_doc *d = copy(r, c->base), *x = parse(r, sv[i], strlen(sv[i]));
        set(d, A(G(R(d), "specializations"), 0), sf[i], yyjson_mut_val_mut_copy(d, R(x)));
        size_t len;
        unsigned char *b = envelope(r, R(d), &len);
        int got = status(run(r, c, "descriptor", b, len, NULL, NULL, NULL));
        scenario_require(r, got == 2 || got == 4, "invalid specialization");
    }
}

static void combinations(scenario_run_t *r, config_t *c) {
    for (int which = 0; which < 7; which++) {
        if (which == 3)
            continue; /* No duplicate of the overlapping pair case. */
        yyjson_mut_doc *d = spec(r, c, "integer");
        yyjson_mut_val *array = G(R(d), "specializations"), *first = A(array, 0);
        int expected = 0;
        if (which == 0) {
            set(d, first, "specializationId", yyjson_mut_uint(d, UINT32_MAX));
            set(d, first, "functionId", yyjson_mut_uint(d, UINT32_MAX));
            set(d, first, "symbol", yyjson_mut_str(d, "goat_fn_4294967295_v1"));
        } else {
            yyjson_mut_val *second;
            if (which == 5) {
                yyjson_mut_doc *x = spec(r, c, "real");
                second = yyjson_mut_val_mut_copy(d, A(G(R(x), "specializations"), 0));
            } else
                second = yyjson_mut_val_mut_copy(d, first);
            unsigned sid = which == 4 ? 1 : which == 5 ? 2 : 3;
            set(d, second, "specializationId", yyjson_mut_uint(d, sid));
            set(d,
                second,
                "symbol",
                yyjson_mut_str(d,
                               sid == 1   ? "goat_fn_1_v1"
                               : sid == 2 ? "goat_fn_2_v1"
                                          : "goat_fn_3_v1"));
            if (which == 1 || which == 4)
                set(d, second, "functionId", yyjson_mut_uint(d, which == 1 ? 3 : 2));
            yyjson_mut_arr_append(array, second);
            expected = which == 4 ? 2 : (which == 2 || which == 3 || which == 6) ? 4 : 0;
            if (which == 6)
                set(d, R(d), "goatAbiVersion", yyjson_mut_uint(d, 2));
        }
        check(r, c, d, expected);
    }
    size_t n;
    const char *s = language_read(r, language_join(r, c->vectors, "empty.module.json"), &n);
    yyjson_mut_doc *d = parse(r, s, n);
    yyjson_mut_obj_remove_str(R(d), "functions");
    yyjson_mut_val *v = yyjson_mut_obj(d), *a = yyjson_mut_arr(d);
    yyjson_mut_obj_add_str(d, v, "library", "native/test.so");
    yyjson_mut_obj_add_str(d,
                           v,
                           "hash",
                           "1111111111111111111111111111111111111111111111111111111111111111");
    yyjson_mut_obj_add_uint(d, v, "goatAbiVersion", 1);
    set(d, v, "target", yyjson_mut_val_mut_copy(d, G(R(c->base), "target")));
    set(d, v, "specializations", yyjson_mut_val_mut_copy(d, G(R(c->base), "specializations")));
    yyjson_mut_arr_append(a, v);
    set(d, R(d), "native", a);
    char *raw = canonical(r, R(d), &n);
    scenario_require(r,
                     status(run(r, c, "metadata", raw, n, NULL, NULL, NULL)) == 2,
                     "missing native function");
}

static void scalar(scenario_run_t *r, config_t *c) {
    const char *kinds[] = {"null", "integer", "real", "string", "boolean", "character"};
    const char *constants[] = {
        "[null]",
        "[-9223372036854775808,9223372036854775807,-1,0,1]",
        "[\"fff0000000000000\",\"8000000000000000\",\"0000000000000000\",\"3ff0000000000000\","
        "\"7ff0000000000000\",\"7ff8000000000000\",\"0000000000000001\"]",
        "[\"\",\"A\\u0000😀\",\"A\\u0000😁\"]",
        "[false,true]",
        "[\"\\u0000\",\"A\",\"퟿\",\"\",\"􏿿\"]"};
    uint64_t chars[] = {0, 65, 0xd7ff, 0xe000, 0x10ffff};
    for (size_t kind = 0; kind < N(kinds); kind++) {
        yyjson_mut_doc *d = spec(r, c, kinds[kind]);
        size_t n;
        char *s = canonical(r, R(d), &n);
        char tag[20], wrong[20];
        snprintf(tag, sizeof(tag), "%zu", kind);
        snprintf(wrong, sizeof(wrong), "%zu", (kind + 1) % 6);
        for (int count = 0; count <= 2; count++) {
            char count_s[10];
            snprintf(count_s, sizeof(count_s), "%d", count);
            scenario_require(r,
                             status(run(r, c, "match", s, n, count_s, tag, kind == 3 ? "1" : "0"))
                                 == (count || kind == 0),
                             "scalar arity");
        }
        scenario_require(r,
                         status(run(r, c, "match", s, n, "1", wrong, "0")) == 0,
                         "wrong scalar tag");
        yyjson_mut_doc *cs = parse(r, constants[kind], strlen(constants[kind]));
        size_t i, max;
        yyjson_mut_val *v;
        yyjson_mut_arr_foreach(R(cs), i, max, v) {
            uint64_t bits = kind == 1   ? (uint64_t)yyjson_mut_get_sint(v)
                            : kind == 2 ? strtoull(yyjson_mut_get_str(v), NULL, 16)
                            : kind == 3 ? 1
                            : kind == 4 ? yyjson_mut_get_bool(v)
                            : kind == 5 ? chars[i]
                                        : 0;
            for (int op = 0; op < 2; op++) {
                yyjson_mut_doc *x = copy(r, d);
                yyjson_mut_val *cond = yyjson_mut_obj(x), *constant = yyjson_mut_obj(x),
                               *arr = yyjson_mut_arr(x);
                yyjson_mut_obj_add_uint(x, cond, "argument", 0);
                yyjson_mut_obj_add_str(x, cond, "operation", op ? "notEqual" : "equal");
                yyjson_mut_obj_add_str(x, constant, "type", kinds[kind]);
                set(x, constant, "value", yyjson_mut_val_mut_copy(x, v));
                set(x, cond, "constant", constant);
                yyjson_mut_arr_append(arr, cond);
                set(x, G(A(G(R(x), "specializations"), 0), "when"), "conditions", arr);
                char *raw = canonical(r, R(x), &n);
                char payload[32];
                snprintf(payload, sizeof(payload), "%" PRIx64, bits);
                bool equal = !(kind == 2 && i == 5) && !(kind == 3 && i != 1);
                scenario_require(r,
                                 status(run(r, c, "match", raw, n, "1", tag, payload))
                                     == (op ? !equal : equal),
                                 "scalar predicate");
            }
        }
    }
}

static void overlaps(scenario_run_t *r, config_t *c) {
    size_t n;
    const char *s =
        language_read(r, language_join(r, c->data, "python-samples/abi-guard-pairs.json"), &n);
    yyjson_mut_doc *d = parse(r, s, n);
    scenario_require(r, yyjson_mut_arr_size(R(d)) == 600, "600 frozen pairs");
    scenario_command_result_t out = run(r, c, "overlap", s, n, NULL, NULL, NULL);
    char *expected = owned(r, malloc(1201));
    size_t i, max;
    yyjson_mut_val *v;
    yyjson_mut_arr_foreach(R(d), i, max, v) {
        expected[i * 2] = format_metadata_guards_overlap(A(v, 0), A(v, 1)) ? '1' : '0';
        expected[i * 2 + 1] = '\n';
    }
    scenario_require_bytes(r,
                           "independent overlap oracle",
                           expected,
                           1200,
                           out.out,
                           out.out_size,
                           true);
}

extern void module_abi_real(scenario_run_t *, const char *, const char *);

static void body(scenario_run_t *r, void *u) {
    config_t *c = u;
    const char *args[] = {c->exe, NULL};
    child_options_t o = {.executable = c->exe, .arguments = args};
    scenario_command(r, &o, 0);
    size_t n;
    const char *s = language_read(r, language_join(r, c->vectors, "native-description.json"), &n);
    c->base = parse(r, s, n);
    descriptors(r, c);
    combinations(r, c);
    scalar(r, c);
    overlaps(r, c);
    module_abi_real(r, c->exe, c->vectors);
    printf("Module ABI checks passed: %zu commands plus real library\n", c->count);
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
    if (argc == 2 && !strcmp(argv[1], "--list")) {
        puts("integration.module_abi");
        result = 0;
    } else if (argc == 6 || argc == 7) {
        fs_error_t e = {0};
        char *exe = absolute_fs_path(argv[2], &e);
        clear_fs_error(&e);
        char *vectors = absolute_fs_path(argv[5], &e);
        clear_fs_error(&e);
        char *data = absolute_fs_path(argv[4], &e);
        clear_fs_error(&e);
        if (exe && vectors && data) {
            config_t c = {.exe = exe, .vectors = vectors, .data = data};
            scenario_definition_t d = {"integration.module_abi", 210000, body, &c, NULL};
            scenario_options_t o = {.build_directory = argv[3], .data_directory = argv[4]};
            const char *selection[] = {argv[1], argc == 7 ? argv[6] : NULL};
            result = scenario_registry_main(&d, 1, argc == 7 ? 2 : 1, selection, &o);
        }
        FREE(exe);
        FREE(vectors);
        FREE(data);
    } else
        fputs("usage: module_abi_scenarios ID HOST BUILD DATA VECTORS [--keep-work]\n", stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
