/** @file module_snapshot_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Pinned immutable module snapshots and synchronized mutation fixtures (#353).
 */
#ifndef _WIN32
#    define _POSIX_C_SOURCE 200809L
#endif
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/format_binary.h"
#ifndef _WIN32
#    include <sys/resource.h>
#    include <unistd.h>
#endif
#include "test/support/format_zip.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"

#include <stdlib.h>
#include <string.h>
#define N(a) (sizeof(a) / sizeof((a)[0]))

typedef struct {
    const char *exe, *vectors, *self;
    yyjson_mut_doc *schema;
} configuration_t;

static bool free_mut(void *p) {
    yyjson_mut_doc_free(p);
    return true;
}

static bool free_doc(void *p) {
    yyjson_doc_free(p);
    return true;
}

static bool free_malloc(void *p) {
    free(p);
    return true;
}

static void *owned(scenario_run_t *r, void *p) {
    scenario_require(r, p != NULL, "allocation");
    scenario_defer(r, free_malloc, p);
    return p;
}

static yyjson_val *get(yyjson_val *v, const char *key) {
    return yyjson_obj_get(v, key);
}

static const char *string(scenario_run_t *r, yyjson_val *v) {
    scenario_require(r, yyjson_is_str(v), "JSON string");
    return yyjson_get_str(v);
}

static yyjson_val *parse(scenario_run_t *r, const void *s, size_t n) {
    yyjson_doc *d = yyjson_read(s, n, 0);
    scenario_require(r, d != NULL, "JSON response");
    scenario_defer(r, free_doc, d);
    return yyjson_doc_get_root(d);
}

static yyjson_mut_doc *mut(scenario_run_t *r, const char *s, size_t n) {
    char error[256] = {0};
    yyjson_mut_doc *d = format_json_parse(s, n, error, sizeof(error));
    scenario_require(r, d != NULL, error);
    scenario_defer(r, free_mut, d);
    return d;
}

static const char *path(scenario_run_t *r, const char *s) {
    return language_join(r, scenario_work_directory(scenario_run_context(r)), s);
}

static const char *join(scenario_run_t *r, const char *a, const char *b) {
    return language_join(r, a, b);
}

static const char *put(scenario_run_t *r, const char *p, const void *data, size_t n) {
    char *parent = language_owned(r, ALLOC(strlen(p) + 1));
    strcpy(parent, p);
    for (char *q = parent + 1; *q; q++)
        if (*q == '/') {
            *q = 0;
            fs_info_t info;
            fs_error_t e = {0};
            bool exists = inspect_fs_path(parent, true, &info, &e);
            clear_fs_error(&e);
            if (!exists)
                language_directory(r, parent);
            else
                scenario_require(r, info.kind == FS_DIRECTORY, "fixture parent");
            *q = '/';
        }
    language_write(r, p, data, n);
    return p;
}

static void number(unsigned char *p, uint32_t n) {
    for (unsigned i = 0; i < 4; i++)
        p[i] = (unsigned char)(n >> (8 * i));
}

#define REQUIRE(x) scenario_require(r, (x), #x)
#define ZERO "0000000000000000000000000000000000000000000000000000000000000000"

#define OLD "старый 😀"

typedef struct {
    const char *record, *path;
    const void *data;
    size_t size;
} module_t;

static const char *list(scenario_run_t *r, const char *a, const char *b) {
    char *s = owned(r, malloc((a ? strlen(a) : 0) + (b ? strlen(b) : 0) + 4));
    sprintf(s, "[%s%s%s]", a ? a : "", a && b ? "," : "", b ? b : "");
    return s;
}

static void integer(scenario_run_t *r, yyjson_val *v, const char *k, int x) {
    REQUIRE(yyjson_is_int(get(v, k)) && yyjson_get_int(get(v, k)) == x);
}

/* The byte layout intentionally follows the independent Python image constructor, not
 * production bytecode writers. Inputs are fixed fixture literals, not external text.
 * Even combining all optional branches with the suite's maximum 11 files uses at most
 * 83 instructions, 15 descriptors, 1 function, 12 locations, and 264 payload bytes.
 * The longest string is the eight-codepoint OLD label (36 bytes including UTF-32 NUL).
 * The 512-node graph creates 512 separate one-file images, not one larger image. */
typedef struct {
    unsigned char payload[4096];
    uint32_t descriptors[64][4], ins[256][2], files[16][4], functions[16][8], locations[32][6];
    size_t np, nd, ni, nf, nn, nl;
} image_t;

static uint32_t data(image_t *i, const void *p, size_t n, uint32_t kind, uint32_t count) {
    i->np = (i->np + 7) & ~(size_t)7;
    uint32_t id = (uint32_t)i->nd++;
    uint32_t *d = i->descriptors[id];
    d[0] = (uint32_t)i->np;
    d[1] = (uint32_t)n;
    d[2] = kind;
    d[3] = count;
    memcpy(i->payload + i->np, p, n);
    i->np += n;
    return id;
}

static uint32_t str(image_t *i, const char *s) {
    unsigned char bytes[512];
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p;) {
        uint32_t cp = *p++;
        if (cp >= 0xf0) {
            cp &= 7;
            for (int k = 0; k < 3; k++)
                cp = (cp << 6) | (*p++ & 63);
        } else if (cp >= 0xe0) {
            cp &= 15;
            for (int k = 0; k < 2; k++)
                cp = (cp << 6) | (*p++ & 63);
        } else if (cp >= 0xc0) {
            cp &= 31;
            cp = (cp << 6) | (*p++ & 63);
        }
        number(bytes + 4 * n++, cp);
    }
    number(bytes + 4 * n, 0);
    return data(i, bytes, 4 * (n + 1), 1, (uint32_t)n);
}

static void op(image_t *i, uint32_t code, uint32_t arg) {
    i->ins[i->ni][0] = code;
    i->ins[i->ni++][1] = arg;
}

static module_t archive(scenario_run_t *r,
                        configuration_t *c,
                        const char *name,
                        const char *version,
                        const char *deps,
                        int value,
                        const char *label,
                        unsigned files,
                        bool throwing,
                        bool native,
                        const char *dormant) {
    image_t *i = owned(r, calloc(1, sizeof(*i)));
    uint32_t x = str(i, "x"), lk = str(i, "label"), lv = str(i, label),
             imp = dormant ? str(i, dormant) : 0;
    for (unsigned f = 0; f < files; f++) {
        uint32_t start = (uint32_t)i->ni;
        unsigned char decl[16];
        number(decl, 0x4445434c);
        number(decl + 4, x);
        number(decl + 8, f);
        number(decl + 12, 0);
        uint32_t d = data(i, decl, 16, 3, 1);
        op(i, 10, (uint32_t)value + f);
        op(i, 69, d);
        op(i, 13, lv);
        op(i, 16, lk);
        if (dormant && !f) {
            uint32_t here = (uint32_t)i->ni;
            op(i, 3, here + 3);
            op(i, 74, imp);
            op(i, 5, 0);
        }
        if (native && !f) {
            uint32_t here = (uint32_t)i->ni, fn = here + 1;
            op(i, 1, here + 4);
            op(i, 47, 0);
            op(i, 5, 0);
            op(i, 2, 0);
            op(i, 7, 0);
            op(i, 50, 0);
            uint32_t fdata[] = {1, f, fn, fn + 3, 0, 0, 0xffffffff, 0};
            memcpy(i->functions[i->nn++], fdata, sizeof(fdata));
            uint32_t loc[] = {start, fn + 3 - start, f, 1, 1, 0};
            memcpy(i->locations[i->nl++], loc, sizeof(loc));
            uint32_t loc2[] = {fn + 3, 2, f, 2, 1, 1};
            memcpy(i->locations[i->nl++], loc2, sizeof(loc2));
        } else {
            if (throwing) {
                op(i, 10, 73);
                op(i, 55, 0);
            }
            op(i, 2, 0);
            uint32_t loc[] = {start, (uint32_t)i->ni - start, f, 1, 1, 0};
            memcpy(i->locations[i->nl++], loc, sizeof(loc));
        }
        uint32_t rec[] = {start, (uint32_t)i->ni - start, start, 0};
        memcpy(i->files[i->nf++], rec, sizeof(rec));
    }
    size_t od = 64 + i->ni * 8, of = od + i->nd * 16, on = of + files * 16, ol = on + i->nn * 32,
           start = ol + i->nl * 24, sz = start + i->np;
    unsigned char *code = owned(r, calloc(1, sz));
    memcpy(code, "GOATBC01", 8);
    uint32_t hdr[] = {1,
                      64,
                      (uint32_t)sz,
                      (uint32_t)i->ni,
                      (uint32_t)i->nd,
                      files,
                      (uint32_t)i->nn,
                      (uint32_t)i->nl,
                      64,
                      (uint32_t)od,
                      (uint32_t)of,
                      (uint32_t)on,
                      (uint32_t)ol,
                      (uint32_t)start};
    for (size_t j = 0; j < N(hdr); j++)
        number(code + 8 + 4 * j, hdr[j]);
    for (size_t j = 0; j < i->ni; j++) {
        code[64 + j * 8] = (unsigned char)i->ins[j][0];
        number(code + 68 + j * 8, i->ins[j][1]);
    }
#define TABLE(at, field, count, width)                                                             \
    for (size_t j = 0; j < (count); j++)                                                           \
        for (size_t k = 0; k < (width); k++)                                                       \
            number(code + (at) + j * (width) * 4 + k * 4, i->field[j][k]);
    TABLE(od, descriptors, i->nd, 4);
    TABLE(of, files, i->nf, 4);
    TABLE(on, functions, i->nn, 8);
    TABLE(ol, locations, i->nl, 6);
#undef TABLE
    memcpy(code + start, i->payload, i->np);
    size_t n;
    const char *base = language_read(r, join(r, c->vectors, "empty.module.json"), &n);
    yyjson_mut_doc *d = mut(r, base, n);
    yyjson_mut_val *v = yyjson_mut_doc_get_root(d);
#define SET(o, k, x) REQUIRE(yyjson_mut_obj_put(o, yyjson_mut_str(d, k), x))
    SET(v, "name", yyjson_mut_strcpy(d, name));
    SET(v, "version", yyjson_mut_strcpy(d, version));
    SET(v, "main", yyjson_mut_uint(d, files - 1));
    yyjson_mut_val *fa = yyjson_mut_arr(d);
    SET(v, "files", fa);
    for (unsigned j = 0; j < files; j++) {
        char fn[64];
        snprintf(fn, sizeof(fn), "absent-%u.goat", j);
        yyjson_mut_val *f = yyjson_mut_obj(d);
        SET(f, "name", yyjson_mut_strcpy(d, fn));
        SET(f, "entry", yyjson_mut_uint(d, i->files[j][2]));
        SET(f, "external", yyjson_mut_false(d));
        REQUIRE(yyjson_mut_arr_append(fa, f));
    }
    if (deps)
        SET(v,
            "dependencies",
            yyjson_mut_val_mut_copy(d, yyjson_mut_doc_get_root(mut(r, deps, strlen(deps)))));
    if (i->nn) {
        yyjson_mut_val *fns = yyjson_mut_arr(d);
        SET(v, "functions", fns);
        for (size_t j = 0; j < i->nn; j++) {
            yyjson_mut_val *f = yyjson_mut_obj(d);
            SET(f, "functionId", yyjson_mut_uint(d, i->functions[j][0]));
            SET(f, "file", yyjson_mut_uint(d, i->functions[j][1]));
            SET(f, "instruction", yyjson_mut_uint(d, i->functions[j][2]));
            SET(f, "parameters", yyjson_mut_arr(d));
            REQUIRE(yyjson_mut_arr_append(fns, f));
        }
    }
    char *lib = owned(r, malloc(strlen(label) + 32));
    sprintf(lib, "never OS-load: %s", label);
    if (native) {
        char hash[65];
        format_binary_sha256(lib, strlen(lib), hash);
        const char *target = language_read(r, join(r, c->vectors, "native-description.json"), &n);
        yyjson_mut_doc *td = mut(r, target, n);
        const char *spec = "[{\"functionId\":1,\"specializationId\":1,\"symbol\":\"goat_fn_1_v1\","
                           "\"signature\":{\"id\":\"goat.adapter.v1\",\"arguments\":[],\"result\":"
                           "\"null\"},\"when\":{\"argumentTypes\":[]}}]";
        yyjson_mut_val *a = yyjson_mut_arr(d), *e = yyjson_mut_obj(d);
        SET(e, "library", yyjson_mut_str(d, "native/opaque.so"));
        SET(e, "hash", yyjson_mut_strcpy(d, hash));
        SET(e, "goatAbiVersion", yyjson_mut_uint(d, 1));
        SET(e,
            "target",
            yyjson_mut_val_mut_copy(d, yyjson_mut_obj_get(yyjson_mut_doc_get_root(td), "target")));
        SET(e,
            "specializations",
            yyjson_mut_val_mut_copy(d, yyjson_mut_doc_get_root(mut(r, spec, strlen(spec)))));
        REQUIRE(yyjson_mut_arr_append(a, e));
        SET(v, "native", a);
    }
    if (!c->schema) {
        const char *schema = language_read(r, join(r, c->vectors, "../schema-source.json"), &n);
        c->schema = mut(r, schema, n);
    }
    char hash[65], error[256] = {0};
    REQUIRE(format_binary_main_hash(v,
                                    yyjson_mut_doc_get_root(c->schema),
                                    code,
                                    sz,
                                    hash,
                                    error,
                                    sizeof(error)));
    SET(v, "mainHash", yyjson_mut_strcpy(d, hash));
#undef SET
    char *json = owned(r, yyjson_mut_write(d, 0, &n));
    format_zip_entry_t entries[3];
    format_zip_entry_init(entries, "module.json", json, n, false);
    format_zip_entry_init(entries + 1, "bytecode.bin", code, sz, false);
    format_zip_entry_init(entries + 2, "native/opaque.so", lib, strlen(lib), false);
    for (int j = 0; j < 3; j++)
        entries[j].flags = 0;
    module_t m = {0};
    m.data = owned(
        r,
        format_zip_build(entries, native ? 3 : 2, NULL, 0, false, &m.size, error, sizeof(error)));
    char *record = owned(r, malloc(strlen(name) + strlen(version) + 128));
    sprintf(record, "{\"name\":\"%s\",\"version\":\"%s\",\"hash\":\"%s\"}", name, version, hash);
    m.record = record;
    return m;
}

static module_t install(scenario_run_t *r, const char *p, module_t m) {
    m.path = put(r, p, m.data, m.size);
    return m;
}

static yyjson_val *node(scenario_run_t *r, yyjson_val *v, const char *name, const char *version) {
    size_t j, n;
    yyjson_val *x;
    yyjson_arr_foreach(get(v, "nodes"), j, n, x) if (!strcmp(string(r, get(x, "name")), name)
                                                     && !strcmp(string(r, get(x, "version")),
                                                                version)) return x;
    REQUIRE(false);
    return NULL;
}

static void values(scenario_run_t *r,
                   yyjson_val *v,
                   const char *name,
                   const char *version,
                   int first,
                   size_t count) {
    yyjson_val *a = get(node(r, v, name, version), "values");
    REQUIRE(yyjson_arr_size(a) == count);
    for (size_t j = 0; j < count; j++)
        REQUIRE(yyjson_is_int(yyjson_arr_get(a, j))
                && yyjson_get_int(yyjson_arr_get(a, j)) == first + (int)j);
}

static yyjson_val *run(scenario_run_t *r,
                       configuration_t *c,
                       const char *root,
                       int status,
                       const char *mode,
                       scenario_body_t action,
                       void *user,
                       bool small) {
    const char *args[] = {c->exe, root, action ? "sync" : mode, NULL};
    const char *exe = c->exe;
    const char *const *argv = args;
#ifndef _WIN32
    const char *limited[] = {c->self, "--small-stack", c->exe, root, mode, NULL};
    if (small) {
        exe = c->self;
        argv = limited;
    }
#else
    (void)small;
#endif
    child_options_t opts = {.executable = exe,
                            .arguments = argv,
                            .environment = language_environment(r, false)};
    scenario_command_result_t out =
        action ? scenario_command_barrier(r, &opts, "READY\n", action, user)
               : scenario_command_capture(r, &opts);
    REQUIRE(out.exit_code == 0);
    const unsigned char *text = out.out;
    size_t n = out.out_size;
    if (!status) {
        size_t prefix = n >= 7 && !memcmp(text, "READY\r\n", 7) ? 7 : 6;
        REQUIRE(n >= prefix);
        scenario_require_bytes(r, "READY", "READY\n", 6, text, prefix, true);
        text += prefix;
        n -= prefix;
    }
    yyjson_val *v = parse(r, text, n);
    integer(r, v, "status", status);
    if (!status)
        integer(r, v, "checks", (int)yyjson_arr_size(get(v, "nodes")));
    else {
        REQUIRE(!get(v, "nodes"));
        if (strcmp(mode, "no-diagnostics"))
            REQUIRE(yyjson_get_int(get(v, "issues")) > 0);
    }
    return v;
}

typedef struct {
    const char *root, *bp, *cp;
    module_t root_new, b_new, c_new;
    int kind;
} mutation_t;

static void mutate_files(scenario_run_t *r, void *arg) {
    mutation_t *m = arg;
    const char *paths[] = {m->root, m->bp, m->cp};
    module_t mods[] = {m->root_new, m->b_new, m->c_new};
    for (size_t j = 0; j < 3; j++)
        if (paths[j]) {
            if (m->kind == 0)
                put(r, paths[j], mods[j].data, mods[j].size);
            else if (m->kind == 1)
                language_remove(r, paths[j]);
            else
                put(r, paths[j], "not a zip", 9);
        }
}

static void remove_tree(scenario_run_t *r, void *arg) {
    fs_error_t e = {0};
    bool ok = remove_fs_tree(arg, &e);
    clear_fs_error(&e);
    REQUIRE(ok);
}

#define ARCH(name, version, deps, value, label, files, throwing, native, dormant)                  \
    archive(r, c, name, version, deps, value, label, files, throwing, native, dormant)
#define MOD(name, deps, value) ARCH(name, "1", deps, value, OLD, 1, false, false, NULL)
#define ROOT(deps, value, files, throwing, native)                                                 \
    install(r, root, ARCH("app", "1", deps, value, OLD, files, throwing, native, NULL))
#define RUN(status) run(r, c, root, status, "run", NULL, NULL, false)

static void files(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    const char *root = path(r, "root.gbin");
    const char *ha[] = {c->exe, NULL};
    REQUIRE(language_command(r, c->exe, ha, NULL).exit_code == 0);
    unsigned counts[] = {1, 3, 11};
    for (size_t j = 0; j < N(counts); j++) {
        ROOT(NULL, 100, counts[j], false, false);
        yyjson_val *v = RUN(0);
        values(r, v, "app", "1", 100, counts[j]);
        REQUIRE(!strcmp(string(r, get(node(r, v, "app", "1"), "label")), OLD));
    }
}

static void mutation(scenario_run_t *r, void *arg, int kind) {
    configuration_t *c = arg;
    const char *root = path(r, "root.gbin"), *bp = path(r, "modules/b/1.gbin"),
               *cp = path(r, "modules/c/1.gbin");
    module_t cm = install(r, cp, ARCH("c", "1", NULL, 30, OLD, 1, false, true, NULL));
    module_t b =
        install(r, bp, ARCH("b", "1", list(r, cm.record, NULL), 20, OLD, 1, false, false, "c@1"));
    ROOT(list(r, b.record, NULL), 10, 3, false, true);
    mutation_t m = {.root = root, .bp = bp, .cp = cp, .kind = kind};
    if (kind == 0) {
        m.c_new = ARCH("c", "1", NULL, 300, "new", 1, false, true, NULL);
        m.b_new = ARCH("b", "1", list(r, m.c_new.record, NULL), 200, "new", 1, false, false, NULL);
        m.root_new =
            ARCH("app", "1", list(r, m.b_new.record, NULL), 1000, "new", 3, false, true, NULL);
    }
    yyjson_val *v = run(r, c, root, 0, "run", mutate_files, &m, false);
    values(r, v, "app", "1", 10, 3);
    values(r, v, "b", "1", 20, 1);
    values(r, v, "c", "1", 30, 1);
    char hash[65];
    const char *lib = "never OS-load: " OLD;
    format_binary_sha256(lib, strlen(lib), hash);
    const char *names[] = {"app", "c"};
    for (size_t j = 0; j < 2; j++) {
        yyjson_val *a = get(node(r, v, names[j], "1"), "native");
        REQUIRE(yyjson_arr_size(a) == 1);
        REQUIRE(!strcmp(string(r, yyjson_arr_get(a, 0)), hash));
    }
    if (!kind) {
        v = RUN(0);
        values(r, v, "app", "1", 1000, 3);
        values(r, v, "b", "1", 200, 1);
        values(r, v, "c", "1", 300, 1);
        yyjson_val *a = get(node(r, v, "c", "1"), "native");
        REQUIRE(yyjson_arr_size(a) == 1);
        REQUIRE(strcmp(string(r, yyjson_arr_get(a, 0)), hash));
    } else {
        if (kind == 1)
            ROOT(list(r, b.record, NULL), 100, 1, false, false);
        RUN(2);
    }
}

static void replace_case(scenario_run_t *r, void *a) {
    mutation(r, a, 0);
}

static void delete_case(scenario_run_t *r, void *a) {
    mutation(r, a, 1);
}

static void corrupt_case(scenario_run_t *r, void *a) {
    mutation(r, a, 2);
}

static void diamond(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    const char *root = path(r, "root.gbin");
    module_t cm = install(r, path(r, "modules/c/1.gbin"), MOD("c", NULL, 3));
    module_t b = install(r, path(r, "modules/b/1.gbin"), MOD("b", list(r, cm.record, NULL), 2));
    module_t d = install(r, path(r, "modules/d/1.gbin"), MOD("d", list(r, cm.record, NULL), 4));
    ROOT(list(r, b.record, d.record), 1, 1, false, false);
    put(r, path(r, "modules/unreachable/1.gbin"), "corrupt but not reachable", 25);
    yyjson_val *v = RUN(0);
    integer(r, v, "checks", 4);
    REQUIRE(
        yyjson_equals(get(node(r, v, "b", "1"), "targets"), get(node(r, v, "d", "1"), "targets")));
    REQUIRE(yyjson_arr_size(get(node(r, v, "app", "1"), "targets")) == 2);
}

static void versions(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    const char *root = path(r, "root.gbin");
    module_t one = install(r, path(r, "modules/same/1.gbin"), MOD("same", NULL, 11));
    module_t two = install(r,
                           path(r, "modules/same/2.gbin"),
                           ARCH("same", "2", NULL, 22, OLD, 1, false, false, NULL));
    module_t b = install(r, path(r, "modules/b/1.gbin"), MOD("b", list(r, one.record, NULL), 1));
    ROOT(list(r, b.record, two.record), 100, 1, false, false);
    yyjson_val *v = run(r, c, root, 0, "run", remove_tree, (void *)path(r, "modules"), false);
    values(r, v, "same", "1", 11, 1);
    values(r, v, "same", "2", 22, 1);
}

static void invalid(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    const char *root = path(r, "root.gbin"), *cp = path(r, "modules/c/1.gbin");
    for (int failure = 0; failure < 4; failure++) {
        module_t cm = MOD("c", NULL, 100);
        const char *record =
            failure == 2 ? "{\"name\":\"c\",\"version\":\"1\",\"hash\":\"" ZERO "\"}" : cm.record;
        module_t b =
            install(r,
                    path(r, "modules/b/1.gbin"),
                    ARCH("b", "1", list(r, record, NULL), 100, OLD, 1, false, false, "c@1"));
        ROOT(list(r, b.record, NULL), 100, 1, false, true);
        if (failure == 1)
            put(r, cp, "bad", 3);
        else if (failure == 2)
            install(r, cp, cm);
        else if (failure == 3)
            install(r, cp, MOD("wrong", NULL, 100));
        RUN(2);
        run(r, c, root, 2, "no-diagnostics", NULL, NULL, false);
    }
}

static void throwing(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    const char *root = path(r, "root.gbin");
    ROOT(NULL, 100, 1, true, false);
    mutation_t m = {.root = root, .kind = 1};
    yyjson_val *v = run(r, c, root, 0, "run", mutate_files, &m, false);
    integer(r, node(r, v, "app", "1"), "failures", 1);
    values(r, v, "app", "1", 100, 1);
}

static void deep(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    const char *root = path(r, "root.gbin");
    const char *dep = NULL;
    for (int j = 511; j >= 0; j--) {
        char name[32], file[80];
        snprintf(name, sizeof(name), "m%d", j);
        snprintf(file, sizeof(file), "modules/m%d/1.gbin", j);
        module_t m = install(r, path(r, file), MOD(name, dep ? list(r, dep, NULL) : NULL, j));
        dep = m.record;
    }
    ROOT(list(r, dep, NULL), 100, 1, false, false);
    yyjson_val *v = run(r, c, root, 0, "run", NULL, NULL, true);
    integer(r, v, "checks", 513);
    values(r, v, "m511", "1", 511, 1);
}

int main(int argc, char **argv) {
#ifndef _WIN32
    if (argc >= 4 && !strcmp(argv[1], "--small-stack")) {
        struct rlimit limit;
        if (getrlimit(RLIMIT_STACK, &limit))
            return 125;
        limit.rlim_cur = 256 * 1024;
        if (setrlimit(RLIMIT_STACK, &limit))
            return 125;
        execv(argv[2], argv + 2);
        return 125;
    }
#else
    fs_error_t ae = {0};
    if (!fs_arguments(&argc, &argv, &ae)) {
        clear_fs_error(&ae);
        return 1;
    }
#endif
    const char *ids[] = {"integration.module_snapshot.files",
                         "integration.module_snapshot.replace",
                         "integration.module_snapshot.delete",
                         "integration.module_snapshot.corrupt",
                         "integration.module_snapshot.diamond",
                         "integration.module_snapshot.versions",
                         "integration.module_snapshot.invalid",
                         "integration.module_snapshot.throw",
                         "integration.module_snapshot.deep"};
    scenario_body_t bodies[] = {files,
                                replace_case,
                                delete_case,
                                corrupt_case,
                                diamond,
                                versions,
                                invalid,
                                throwing,
                                deep};
    int result = 1;
    if (argc == 2 && !strcmp(argv[1], "--list")) {
        for (size_t j = 0; j < N(ids); j++)
            puts(ids[j]);
        result = 0;
    } else if (argc == 6 || argc == 7) {
        fs_error_t e = {0};
        char *exe = absolute_fs_path(argv[2], &e);
        clear_fs_error(&e);
        char *vectors = absolute_fs_path(argv[5], &e);
        clear_fs_error(&e);
        char *self = absolute_fs_path(argv[0], &e);
        clear_fs_error(&e);
        if (exe && vectors && self) {
            configuration_t c = {.exe = exe, .vectors = vectors, .self = self};
            scenario_definition_t defs[N(ids)];
            for (size_t j = 0; j < N(ids); j++)
                defs[j] =
                    (scenario_definition_t){ids[j], j == 8 ? 300000 : 180000, bodies[j], &c, NULL};
            scenario_options_t o = {.build_directory = argv[3], .data_directory = argv[4]};
            const char *selection[] = {argv[1], argc == 7 ? argv[6] : NULL};
            result = scenario_registry_main(defs, N(ids), argc == 7 ? 2 : 1, selection, &o);
        }
        FREE(exe);
        FREE(vectors);
        FREE(self);
    } else
        fputs("usage: module_snapshot_scenarios ID HOST BUILD DATA VECTORS [--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
