/** @file module_snapshot_fixture.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent snapshot/identity archive fixture implementation.
 */
#include "test/support/module_snapshot_fixture.h"

#include "test/support/format_zip.h"

#include <stdlib.h>
#include <string.h>
#define N(a) (sizeof(a) / sizeof((a)[0]))

static bool free_mut(void *p) {
    yyjson_mut_doc_free(p);
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

static yyjson_mut_doc *mut(scenario_run_t *r, const char *s, size_t n) {
    char error[256] = {0};
    yyjson_mut_doc *d = format_json_parse(s, n, error, sizeof(error));
    scenario_require(r, d != NULL, error);
    scenario_defer(r, free_mut, d);
    return d;
}

static const char *join(scenario_run_t *r, const char *a, const char *b) {
    return language_join(r, a, b);
}

static void number(unsigned char *p, uint32_t n) {
    for (unsigned i = 0; i < 4; i++)
        p[i] = (unsigned char)(n >> (8 * i));
}

#define REQUIRE(x) scenario_require(r, (x), #x)

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

snapshot_fixture_t snapshot_fixture_archive(scenario_run_t *r,
                                            const char *vectors,
                                            yyjson_mut_doc **schema,
                                            const char *name,
                                            const char *version,
                                            const char *deps,
                                            int value,
                                            const char *label,
                                            unsigned files,
                                            bool throwing,
                                            bool native,
                                            const char *dormant) {
    REQUIRE(files >= 1 && files <= 11);
    REQUIRE(strlen(label) <= 64 && (!dormant || strlen(dormant) <= 64));
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
    const char *base = language_read(r, join(r, vectors, "empty.module.json"), &n);
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
        const char *target = language_read(r, join(r, vectors, "native-description.json"), &n);
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
    if (!(*schema)) {
        const char *schema_text = language_read(r, join(r, vectors, "../schema-source.json"), &n);
        (*schema) = mut(r, schema_text, n);
    }
    char hash[65], error[256] = {0};
    REQUIRE(format_binary_main_hash(v,
                                    yyjson_mut_doc_get_root((*schema)),
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
    snapshot_fixture_t m = {0};
    m.data = owned(
        r,
        format_zip_build(entries, native ? 3 : 2, NULL, 0, false, &m.size, error, sizeof(error)));
    char *record = owned(r, malloc(strlen(name) + strlen(version) + 128));
    sprintf(record, "{\"name\":\"%s\",\"version\":\"%s\",\"hash\":\"%s\"}", name, version, hash);
    m.record = record;
    return m;
}
