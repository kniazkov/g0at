/** @file module_native_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent native-selection fixtures replacing check_module_native.py.
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/format_binary.h"
#include "test/support/format_zip.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"

#include <stdlib.h>
#include <string.h>

#define N(a) (sizeof(a) / sizeof((a)[0]))

enum { OK, ARGUMENT, INVALID, NO_MATCH, AMBIGUOUS };

typedef struct {
    unsigned char *data;
    size_t size;
} bytes_t;

typedef struct {
    const char *exe, *vectors;
    yyjson_mut_doc *doc, *schema;
    yyjson_mut_val *base, *descriptor;
    bytes_t bytecode;
    size_t count;
} configuration_t;

static bool free_mut(void *p) {
    yyjson_mut_doc_free(p);
    return true;
}

static bool free_doc(void *p) {
    yyjson_doc_free(p);
    return true;
}

static bool free_heap(void *p) {
    free(p);
    return true;
}

static void *owned(scenario_run_t *r, void *p) {
    scenario_require(r, p != NULL, "allocation");
    scenario_defer(r, free_heap, p);
    return p;
}

static bytes_t read_file(scenario_run_t *r, const char *dir, const char *name) {
    size_t n;
    const char *p = language_read(r, language_join(r, dir, name), &n);
    return (bytes_t){(unsigned char *)p, n};
}

static yyjson_mut_val *
parse_n(scenario_run_t *r, configuration_t *c, const char *json, size_t size) {
    yyjson_doc *d = yyjson_read(json, size, 0);
    scenario_require(r, d != NULL, "fixture JSON");
    yyjson_mut_val *v = yyjson_val_mut_copy(c->doc, yyjson_doc_get_root(d));
    yyjson_doc_free(d);
    scenario_require(r, v != NULL, "JSON copy");
    return v;
}

static yyjson_mut_val *parse(scenario_run_t *r, configuration_t *c, const char *json) {
    return parse_n(r, c, json, strlen(json));
}

static yyjson_mut_val *copy(configuration_t *c, yyjson_mut_val *v) {
    return yyjson_mut_val_mut_copy(c->doc, v);
}

static yyjson_mut_val *get(yyjson_mut_val *v, const char *k) {
    return yyjson_mut_obj_get(v, k);
}

static void put(configuration_t *c, yyjson_mut_val *v, const char *k, yyjson_mut_val *x) {
    yyjson_mut_obj_put(v, yyjson_mut_str(c->doc, k), x);
}

static void str(configuration_t *c, yyjson_mut_val *v, const char *k, const char *x) {
    put(c, v, k, yyjson_mut_strcpy(c->doc, x));
}

static void number(configuration_t *c, yyjson_mut_val *v, const char *k, uint64_t x) {
    put(c, v, k, yyjson_mut_uint(c->doc, x));
}

static bytes_t json(scenario_run_t *r, yyjson_mut_val *v) {
    size_t n;
    char *s = yyjson_mut_val_write(v, 0, &n);
    return (bytes_t){owned(r, s), n};
}

static yyjson_mut_val *target(scenario_run_t *r, configuration_t *c, int profile) {
    yyjson_mut_val *v = parse(r,
                              c,
                              "{\"os\":\"linux\",\"architecture\":\"x86_64\",\"bits\":64,"
                              "\"endian\":\"little\",\"minimumOs\":[5,4],\"cpuFeatures\":[\"sse2\"]"
                              ",\"runtime\":{\"family\":\"glibc\",\"minimumVersion\":[0]}}");
    if (profile == 1) {
        str(c, v, "architecture", "aarch64");
        put(c, v, "cpuFeatures", parse(r, c, "[\"fp\",\"neon\"]"));
    }
    if (profile == 2)
        str(c, get(v, "runtime"), "family", "musl");
    if (profile >= 3) {
        str(c, v, "os", "windows");
        put(c, v, "minimumOs", parse(r, c, "[10]"));
        str(c, get(v, "runtime"), "family", profile == 4 ? "ucrt" : "msvcrt");
    }
    if (profile == 5) {
        str(c, v, "architecture", "x86");
        number(c, v, "bits", 32);
        put(c, v, "cpuFeatures", parse(r, c, "[\"i686\"]"));
    }
    return v;
}

static yyjson_mut_val *variant(scenario_run_t *r,
                               configuration_t *c,
                               yyjson_mut_val *profile,
                               const char *name,
                               uint32_t abi) {
    yyjson_mut_val *v = yyjson_mut_obj(c->doc);
    str(c, v, "library", name ? name : "native/test.so");
    char digest[65];
    format_binary_sha256("unloaded-test-bytes", 19, digest);
    str(c, v, "hash", digest);
    number(c, v, "goatAbiVersion", abi);
    put(c, v, "target", profile ? copy(c, profile) : target(r, c, 0));
    put(c, v, "specializations", copy(c, get(c->descriptor, "specializations")));
    return v;
}

static yyjson_mut_val *
array(configuration_t *c, yyjson_mut_val *a, yyjson_mut_val *b, yyjson_mut_val *d) {
    yyjson_mut_val *v = yyjson_mut_arr(c->doc);
    if (a)
        yyjson_mut_arr_append(v, copy(c, a));
    if (b)
        yyjson_mut_arr_append(v, copy(c, b));
    if (d)
        yyjson_mut_arr_append(v, copy(c, d));
    return v;
}

static yyjson_val *execute(scenario_run_t *r,
                           configuration_t *c,
                           yyjson_mut_val *variants,
                           yyjson_mut_val *profile,
                           const char *raw_profile,
                           const uint32_t *abis,
                           size_t na,
                           const char *mode,
                           const char *path,
                           const char *payload_name,
                           bytes_t payload) {
    yyjson_mut_val *metadata = copy(c, c->base);
    put(c,
        metadata,
        "functions",
        parse(r,
              c,
              "[{\"functionId\":1,\"file\":0,\"instruction\":0,\"parameters\":[{\"name\":\"a\"},{"
              "\"name\":\"b\"}]}]"));
    char hash[65], error[256] = {0};
    scenario_require(r,
                     format_binary_main_hash(metadata,
                                             yyjson_mut_doc_get_root(c->schema),
                                             c->bytecode.data,
                                             c->bytecode.size,
                                             hash,
                                             error,
                                             sizeof(error)),
                     error);
    str(c, metadata, "mainHash", hash);
    put(c, metadata, "native", variants);
    size_t nv = yyjson_mut_arr_size(variants);
    format_zip_entry_t *entries = owned(r, calloc(nv + 2, sizeof(*entries)));
    for (size_t i = 0; i < nv; i++) {
        const char *name =
            yyjson_mut_get_str(get(yyjson_mut_arr_get(variants, nv - 1 - i), "library"));
        bool overridden = payload_name && !strcmp(name, payload_name);
        format_zip_entry_init(&entries[i],
                              name,
                              overridden ? payload.data
                                         : (const unsigned char *)"unloaded-test-bytes",
                              overridden ? payload.size : 19,
                              true);
    }
    format_zip_entry_init(&entries[nv], "bytecode.bin", c->bytecode.data, c->bytecode.size, true);
    bytes_t meta = json(r, metadata);
    format_zip_entry_init(&entries[nv + 1], "module.json", meta.data, meta.size, true);
    bytes_t zip = {0};
    zip.data = format_zip_build(entries, nv + 2, NULL, 0, false, &zip.size, error, sizeof(error));
    owned(r, zip.data);
    bytes_t p = raw_profile ? (bytes_t){(unsigned char *)raw_profile, strlen(raw_profile)}
                            : json(r, profile ? profile : target(r, c, 0));
    size_t size = 8 + p.size + 4 * na + zip.size;
    unsigned char *frame = owned(r, calloc(size, 1));
    format_bytes_patch_le(frame, size, 0, p.size, 4);
    memcpy(frame + 4, p.data, p.size);
    format_bytes_patch_le(frame, size, 4 + p.size, na, 4);
    for (size_t i = 0; i < na; i++)
        format_bytes_patch_le(frame, size, 8 + p.size + 4 * i, abis[i], 4);
    memcpy(frame + 8 + p.size + 4 * na, zip.data, zip.size);
    const char *args[] = {c->exe, mode ? mode : "select", path, NULL};
    child_options_t options = {.executable = c->exe,
                               .arguments = args,
                               .stdin_data = frame,
                               .stdin_size = size};
    scenario_command_result_t result = scenario_command_capture(r, &options);
    c->count++;
    scenario_require(r, result.exit_code == 0, "native host exit");
    yyjson_doc *d = yyjson_read(result.out, result.out_size, 0);
    scenario_require(r, d != NULL, "native host JSON");
    scenario_defer(r, free_doc, d);
    return yyjson_doc_get_root(d);
}

static yyjson_val *run(scenario_run_t *r,
                       configuration_t *c,
                       yyjson_mut_val *vs,
                       yyjson_mut_val *p,
                       const uint32_t *abis,
                       size_t na,
                       const char *mode) {
    return execute(r, c, vs, p, NULL, abis, na, mode, NULL, NULL, (bytes_t){0});
}

static yyjson_val *check(scenario_run_t *r,
                         configuration_t *c,
                         yyjson_mut_val *vs,
                         yyjson_mut_val *p,
                         int status,
                         const char *mode) {
    uint32_t abi = 1;
    yyjson_val *v = run(r, c, vs, p, &abi, 1, mode);
    yyjson_val *actual = yyjson_obj_get(v, "status");
    scenario_require(r,
                     yyjson_is_int(actual) && yyjson_get_int(actual) == status,
                     "native selection status");
    if (status)
        scenario_require(r,
                         yyjson_is_null(yyjson_obj_get(v, "selected")),
                         "failed selection cleared");
    return v;
}

static void field(scenario_run_t *r, yyjson_val *v, const char *key, uint64_t expected) {
    yyjson_val *actual = yyjson_obj_get(v, key);
    scenario_require(r, yyjson_is_uint(actual) && yyjson_get_uint(actual) == expected, key);
    if (!strcmp(key, "status") && expected)
        scenario_require(r,
                         yyjson_is_null(yyjson_obj_get(v, "selected")),
                         "failed selection cleared");
}

static yyjson_mut_val *one(scenario_run_t *r, configuration_t *c, yyjson_mut_val *p) {
    return array(c, variant(r, c, p, NULL, 1), NULL, NULL);
}

static void reasons(scenario_run_t *r,
                    configuration_t *c,
                    yyjson_mut_val *p,
                    yyjson_mut_val *env,
                    const char *mode,
                    uint64_t expected,
                    uint64_t missing) {
    uint32_t abi = 1;
    yyjson_val *v = run(r, c, one(r, c, p), env, &abi, 1, mode);
    v = yyjson_arr_get(yyjson_obj_get(v, "compatibility"), 0);
    field(r, v, "reasons", expected);
    scenario_require(r,
                     yyjson_is_uint(yyjson_obj_get(v, "status"))
                         && yyjson_get_uint(yyjson_obj_get(v, "status"))
                                == (expected ? NO_MATCH : OK),
                     "compatibility status");
    if (missing != UINT64_MAX)
        field(r, v, "missingCpu", missing);
}

static void
profile_error(scenario_run_t *r, configuration_t *c, yyjson_mut_val *p, const char *raw) {
    uint32_t abi = 1;
    yyjson_val *v = execute(r, c, one(r, c, NULL), p, raw, &abi, 1, NULL, NULL, NULL, (bytes_t){0});
    scenario_require(r,
                     yyjson_get_uint(yyjson_obj_get(v, "profileError")) != 0,
                     "invalid profile rejected");
}

static void selection(scenario_run_t *r, configuration_t *c) {
    yyjson_val *v = check(r, c, one(r, c, NULL), NULL, OK, NULL);
    scenario_require(r,
                     !strcmp(yyjson_get_str(yyjson_obj_get(v, "library")), "native/test.so"),
                     "selected library");
    check(r, c, array(c, NULL, NULL, NULL), NULL, NO_MATCH, NULL);
    const uint32_t supported[][3] = {{0}, {2}, {3, 1, 2}, {1, 1}, {2}, {3}, {UINT32_MAX}};
    const size_t counts[] = {0, 1, 3, 2, 1, 1, 1};
    for (size_t i = 0; i < N(counts); i++)
        field(r,
              run(r, c, one(r, c, NULL), NULL, supported[i], counts[i], NULL),
              "status",
              i == 2 || i == 3 ? OK : NO_MATCH);
    uint32_t versions[] = {2, 3, UINT32_MAX};
    for (size_t i = 0; i < N(versions); i++) {
        yyjson_mut_val *vs = array(c, variant(r, c, NULL, NULL, versions[i]), NULL, NULL);
        check(r, c, vs, NULL, NO_MATCH, NULL);
        field(r, run(r, c, vs, NULL, &versions[i], 1, NULL), "status", OK);
    }
    reasons(r, c, NULL, NULL, NULL, 0, 0);
    v = run(r, c, one(r, c, NULL), NULL, NULL, 0, NULL);
    field(r, yyjson_arr_get(yyjson_obj_get(v, "compatibility"), 0), "reasons", 1 << 11);
    const char *modes[] = {"unknown-os",
                           "unknown-arch",
                           "big-endian",
                           "unknown-runtime",
                           "unknown-os-version",
                           "no-features"};
    const unsigned bits[] = {0, 1, 3, 4, 7, 9};
    for (size_t i = 0; i < N(modes); i++) {
        reasons(r, c, NULL, NULL, modes[i], 1u << bits[i], i == 5 ? 2 : 0);
        check(r, c, one(r, c, NULL), NULL, NO_MATCH, modes[i]);
    }
    yyjson_mut_val *p = target(r, c, 0), *e = target(r, c, 0);
    put(c, e, "minimumOs", parse(r, c, "[4]"));
    reasons(r, c, p, e, NULL, 1 << 5, 0);
    put(c, get(p, "runtime"), "minimumVersion", parse(r, c, "[2,28]"));
    e = target(r, c, 0);
    put(c, get(e, "runtime"), "minimumVersion", parse(r, c, "[2,9]"));
    reasons(r, c, p, e, NULL, 1 << 6, 0);
    reasons(r, c, p, NULL, "unknown-runtime-version", 1 << 8, 0);
    check(r, c, one(r, c, p), NULL, NO_MATCH, "unknown-runtime-version");
    reasons(r, c, NULL, NULL, "unknown-runtime-version", 0, 0);
    check(r, c, one(r, c, NULL), NULL, OK, "unknown-runtime-version");
    p = target(r, c, 0);
    put(c, p, "cpuFeatures", parse(r, c, "[\"sse2\",\"future-cpu\"]"));
    reasons(r, c, p, NULL, NULL, 1 << 10, 0);
    put(c, p, "cpuFeatures", parse(r, c, "[\"avx2\"]"));
    reasons(r, c, p, NULL, NULL, 1 << 9, (1 << 6) | (1 << 7));
    p = target(r, c, 5);
    put(c, get(p, "runtime"), "minimumVersion", parse(r, c, "[2]"));
    e = target(r, c, 0);
    put(c, e, "minimumOs", parse(r, c, "[1]"));
    uint32_t abi = 1;
    v = run(r, c, array(c, variant(r, c, p, "native/other.dll", 2), NULL, NULL), e, &abi, 1, NULL);
    field(r,
          yyjson_arr_get(yyjson_obj_get(v, "compatibility"), 0),
          "reasons",
          (1 << 0) | (1 << 1) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 6) | (1 << 9) | (1 << 11));
    for (int i = 0; i < 6; i++)
        for (int j = 0; j < 6; j++)
            check(r,
                  c,
                  array(c,
                        variant(r,
                                c,
                                target(r, c, i),
                                i >= 3 ? "native/profile.dll" : "native/profile.so",
                                1),
                        NULL,
                        NULL),
                  target(r, c, j),
                  i == j ? OK : NO_MATCH,
                  NULL);
    check(r,
          c,
          array(c, variant(r, c, target(r, c, 3), "native/64.dll", 1), NULL, NULL),
          target(r, c, 5),
          NO_MATCH,
          NULL);
    const char *need[] = {"[2,28]",
                          "[2,9]",
                          "[5,4]",
                          "[5,4]",
                          "[5,4,1]",
                          "[5,4]",
                          "[0]",
                          "[4294967295]",
                          "[1,4294967295]"};
    const char *have[] =
        {"[2,9]", "[2,28]", "[5,4]", "[5,4,0,0]", "[5,4]", "[5,5]", "[0]", "[4294967294]", "[2]"};
    const bool good[] = {false, true, true, true, false, true, true, false, true};
    for (int f = 0; f < 2; f++)
        for (size_t i = 0; i < N(need); i++) {
            p = target(r, c, 0);
            e = target(r, c, 0);
            put(c,
                f ? get(p, "runtime") : p,
                f ? "minimumVersion" : "minimumOs",
                parse(r, c, need[i]));
            put(c,
                f ? get(e, "runtime") : e,
                f ? "minimumVersion" : "minimumOs",
                parse(r, c, have[i]));
            check(r, c, one(r, c, p), e, good[i] ? OK : NO_MATCH, NULL);
        }
    const char *windows[] = {"[10,0,19044]", "[10,0,19045]", "[10,0,22621]", "[11]"};
    for (size_t i = 0; i < N(windows); i++) {
        p = target(r, c, 3);
        e = target(r, c, 3);
        put(c, p, "minimumOs", parse(r, c, "[10,0,19045]"));
        put(c, e, "minimumOs", parse(r, c, windows[i]));
        check(r,
              c,
              array(c, variant(r, c, p, "native/win.dll", 1), NULL, NULL),
              e,
              i ? OK : NO_MATCH,
              NULL);
    }
}

static void cpu(scenario_run_t *r, configuration_t *c) {
    for (int i = 0; i < 6; i++) {
        yyjson_mut_val *p = target(r, c, i), *s = copy(c, p);
        put(c, s, "cpuFeatures", parse(r, c, "[]"));
        const char *name = i >= 3 ? "native/x.dll" : "native/x.so";
        check(r, c, array(c, variant(r, c, p, name, 1), NULL, NULL), s, NO_MATCH, NULL);
        check(r, c, array(c, variant(r, c, s, name, 1), NULL, NULL), p, OK, NULL);
        check(r, c, array(c, variant(r, c, s, name, 1), NULL, NULL), s, NO_MATCH, NULL);
    }
    const char *unknown[] = {"[\"sse2\",\"future-cpu\"]",
                             "[\"sse2\",\"SSE2\"]",
                             "[\"sse2\",\"sse2\\u0000ignored\"]",
                             "[\"sse2\",\"🚀\"]"};
    for (size_t i = 0; i < N(unknown); i++) {
        yyjson_mut_val *p = target(r, c, 0);
        put(c, p, "cpuFeatures", parse(r, c, unknown[i]));
        check(r, c, one(r, c, p), NULL, NO_MATCH, NULL);
        check(r, c, one(r, c, p), p, NO_MATCH, NULL);
    }
    for (int i = 0; i < 2; i++) {
        yyjson_mut_val *p = target(r, c, i);
        put(c,
            p,
            "cpuFeatures",
            parse(r, c, i ? "[\"fp\",\"neon\",\"avx2\"]" : "[\"sse2\",\"neon\"]"));
        check(r, c, one(r, c, p), NULL, INVALID, NULL);
    }
    check(r,
          c,
          array(c, variant(r, c, NULL, "native/linux.dll", 1), NULL, NULL),
          NULL,
          INVALID,
          NULL);
    check(r,
          c,
          array(c, variant(r, c, target(r, c, 3), "native/windows.so", 1), NULL, NULL),
          NULL,
          INVALID,
          NULL);
    yyjson_mut_val *foreign = target(r, c, 1);
    put(c, foreign, "cpuFeatures", parse(r, c, "[\"avx\"]"));
    yyjson_mut_val *bad = variant(r, c, foreign, "native/z-foreign.so", 1),
                   *a = variant(r, c, NULL, "native/a.so", 1),
                   *b = variant(r, c, NULL, "native/b.so", 1);
    check(r, c, array(c, a, b, bad), NULL, INVALID, NULL);
    check(r, c, array(c, bad, a, b), NULL, INVALID, NULL);
    profile_error(r, c, foreign, NULL);
    const char *avx[] = {"avx2", "fma", "avx512f"};
    for (size_t i = 0; i < N(avx); i++) {
        yyjson_mut_val *p = target(r, c, 0), *fs = array(c,
                                                         yyjson_mut_str(c->doc, "sse2"),
                                                         yyjson_mut_str(c->doc, avx[i]),
                                                         NULL);
        put(c, p, "cpuFeatures", fs);
        check(r, c, one(r, c, p), p, NO_MATCH, NULL);
        yyjson_mut_val *e = copy(c, p);
        yyjson_mut_arr_insert(get(e, "cpuFeatures"), yyjson_mut_str(c->doc, "avx"), 1);
        check(r, c, one(r, c, p), e, OK, NULL);
    }
    const char *chain[] = {"sse2", "sse3", "ssse3", "sse4_1", "sse4_2"};
    for (size_t i = 1; i < N(chain); i++) {
        yyjson_mut_val *p = target(r, c, 0);
        put(c, p, "cpuFeatures", array(c, yyjson_mut_str(c->doc, chain[i]), NULL, NULL));
        yyjson_mut_val *e = target(r, c, 0), *fs = yyjson_mut_arr(c->doc);
        for (size_t j = 0; j <= i; j++)
            yyjson_mut_arr_append(fs, yyjson_mut_str(c->doc, chain[j]));
        put(c, e, "cpuFeatures", fs);
        check(r, c, one(r, c, p), e, OK, NULL);
        for (size_t j = 0; j < i; j++) {
            yyjson_mut_val *incomplete = copy(c, e);
            yyjson_mut_arr_remove(get(incomplete, "cpuFeatures"), j);
            check(r, c, one(r, c, p), incomplete, NO_MATCH, NULL);
        }
    }
    yyjson_mut_val *p = target(r, c, 1), *e = target(r, c, 1);
    put(c, p, "cpuFeatures", parse(r, c, "[\"sve2\"]"));
    put(c, e, "cpuFeatures", parse(r, c, "[\"fp\",\"neon\",\"sve2\"]"));
    check(r, c, one(r, c, p), e, NO_MATCH, NULL);
    put(c, e, "cpuFeatures", parse(r, c, "[\"fp\",\"neon\",\"sve\",\"sve2\"]"));
    check(r, c, one(r, c, p), e, OK, NULL);
}

static void metadata(scenario_run_t *r, configuration_t *c) {
    yyjson_mut_val *a = variant(r, c, NULL, "native/a.so", 1),
                   *b = variant(r, c, NULL, "native/b.so", 1);
    for (int reverse = 0; reverse < 2; reverse++) {
        yyjson_mut_val *vs = array(c, reverse ? b : a, reverse ? a : b, NULL);
        check(r, c, vs, NULL, AMBIGUOUS, NULL);
        check(r, c, vs, target(r, c, 1), NO_MATCH, NULL);
    }
    yyjson_mut_val *p = target(r, c, 0);
    put(c, get(p, "runtime"), "minimumVersion", parse(r, c, "[2,28]"));
    yyjson_mut_val *newer = variant(r, c, p, "native/new.so", 1), *e = target(r, c, 0);
    put(c, get(e, "runtime"), "minimumVersion", parse(r, c, "[2,39]"));
    check(r, c, array(c, a, newer, NULL), e, AMBIGUOUS, NULL);
    put(c, get(e, "runtime"), "minimumVersion", parse(r, c, "[2,9]"));
    check(r, c, array(c, a, newer, NULL), e, OK, NULL);
    yyjson_mut_val *foreign = variant(r, c, target(r, c, 1), "native/foreign.so", 1);
    for (int reverse = 0; reverse < 2; reverse++) {
        yyjson_val *v = check(r,
                              c,
                              array(c, reverse ? a : foreign, reverse ? foreign : a, NULL),
                              NULL,
                              OK,
                              NULL);
        scenario_require(r,
                         !strcmp(yyjson_get_str(yyjson_obj_get(v, "library")), "native/a.so"),
                         "unique suitable library");
    }
    p = target(r, c, 0);
    put(c, p, "cpuFeatures", parse(r, c, "[\"sse2\",\"future\"]"));
    yyjson_mut_val *incompatible[] = {foreign,
                                      variant(r, c, NULL, "native/future.so", 2),
                                      variant(r, c, target(r, c, 2), "native/musl.so", 1),
                                      variant(r, c, p, "native/future.so", 1)};
    for (size_t i = 0; i < N(incompatible); i++)
        for (int j = 0; j < 2; j++) {
            const char *name = yyjson_mut_get_str(get(j ? incompatible[i] : a, "library"));
            uint32_t abi = 1;
            yyjson_val *v = execute(r,
                                    c,
                                    array(c, a, incompatible[i], NULL),
                                    NULL,
                                    NULL,
                                    &abi,
                                    1,
                                    NULL,
                                    NULL,
                                    name,
                                    (bytes_t){(unsigned char *)"tampered", 8});
            field(r, v, "containerError", 13);
            yyjson_mut_val *vs = array(c, a, incompatible[i], NULL);
            str(c,
                yyjson_mut_arr_get(vs, j),
                "hash",
                "0000000000000000000000000000000000000000000000000000000000000000");
            field(r, run(r, c, vs, NULL, &abi, 1, NULL), "containerError", 13);
        }
    yyjson_val *v = check(r, c, one(r, c, NULL), NULL, OK, NULL);
    const char *hash = yyjson_get_str(yyjson_obj_get(v, "mainHash"));
    const char *valid[] = {"{\"name\":\"GCC\",\"version\":\"16.1\"}",
                           "{\"name\":\"Clang\",\"version\":\"20\"}",
                           "{\"name\":\" GCC \",\"version\":\" A\\u0000B \"}"};
    const char *invalid[] = {"null",
                             "{}",
                             "{\"name\":\"GCC\"}",
                             "{\"name\":\"\",\"version\":\"1\"}",
                             "{\"name\":\"GCC\",\"version\":\"\"}",
                             "{\"name\":1,\"version\":\"1\"}",
                             "{\"name\":\"GCC\",\"version\":\"1\",\"extra\":true}"};
    for (size_t i = 0; i < N(valid); i++) {
        yyjson_mut_val *item = variant(r, c, NULL, NULL, 1);
        put(c, item, "cCompiler", parse(r, c, valid[i]));
        v = check(r, c, array(c, item, NULL, NULL), NULL, OK, NULL);
        scenario_require(r,
                         !strcmp(hash, yyjson_get_str(yyjson_obj_get(v, "mainHash"))),
                         "compiler ignored by hash");
    }
    for (size_t i = 0; i < N(invalid); i++) {
        yyjson_mut_val *item = variant(r, c, NULL, NULL, 1);
        put(c, item, "cCompiler", parse(r, c, invalid[i]));
        uint32_t abi = 1;
        field(r, run(r, c, array(c, item, NULL, NULL), NULL, &abi, 1, NULL), "containerError", 11);
    }
    const char *keys[] = {"unknown",
                          "bits",
                          "endian",
                          "minimumOs",
                          "minimumOs",
                          "minimumOs",
                          "minimumOs",
                          "minimumOs",
                          "cpuFeatures",
                          "family",
                          "minimumVersion"};
    const char *values[] = {"true",
                            "32",
                            "\"big\"",
                            "[]",
                            "[1,2,3,4,5]",
                            "[-1]",
                            "[4294967296]",
                            "[1.0]",
                            "[\"sse2\",\"sse2\"]",
                            "\"ucrt\"",
                            "null"};
    for (size_t i = 0; i < N(keys); i++) {
        p = target(r, c, 0);
        put(c, i >= 9 ? get(p, "runtime") : p, keys[i], parse(r, c, values[i]));
        profile_error(r, c, p, NULL);
    }
    const char *required[] =
        {"os", "architecture", "bits", "endian", "minimumOs", "cpuFeatures", "runtime"};
    for (size_t i = 0; i < N(required); i++) {
        p = target(r, c, 0);
        yyjson_mut_obj_remove_key(p, required[i]);
        profile_error(r, c, p, NULL);
    }
    profile_error(r, c, NULL, "{\"os\":\"linux\",\"os\":\"linux\"}");
    p = target(r, c, 3);
    put(c, p, "minimumOs", parse(r, c, "[6,1]"));
    profile_error(r, c, p, NULL);
}

static yyjson_mut_val *host_report(scenario_run_t *r, configuration_t *c, const char *const *env) {
    const char *args[] = {c->exe, "host", NULL};
    child_options_t opts = {.executable = c->exe, .arguments = args, .environment = env};
    scenario_command_result_t result = scenario_command_capture(r, &opts);
    scenario_require(r, result.exit_code == 0, "host report exit");
    yyjson_doc *d = yyjson_read(result.out, result.out_size, 0);
    scenario_require(r, d != NULL, "host report JSON");
    yyjson_mut_val *v = yyjson_val_mut_copy(c->doc, yyjson_doc_get_root(d));
    yyjson_doc_free(d);
    return v;
}

static void real_library(scenario_run_t *r, configuration_t *c) {
    yyjson_mut_val *host = host_report(r, c, NULL);
    const char *fake[] = {"MINGW32", "MINGW64", "UCRT64", "not-a-runtime"};
    for (size_t i = 0; i < N(fake); i++) {
        const char *const *base = language_environment_override(r, "MSYSTEM", fake[i]);
        size_t n = 0;
        while (base[n])
            n++;
        const char **env = owned(r, calloc(n + 2, sizeof(*env)));
        size_t k = 0;
        for (size_t j = 0; j < n; j++)
            if (
#ifdef _WIN32
                _strnicmp(base[j], "PATH=", 5)
#else
                strncmp(base[j], "PATH=", 5)
#endif
            )
                env[k++] = base[j];
        env[k++] = "PATH=nonexistent";
        scenario_require(r,
                         format_json_equal(host, host_report(r, c, env)),
                         "environment cannot spoof host facts");
    }
    unsigned arch = (unsigned)yyjson_mut_get_uint(get(host, "architecture")),
             os = (unsigned)yyjson_mut_get_uint(get(host, "os")),
             runtime = (unsigned)yyjson_mut_get_uint(get(host, "runtime"));
    scenario_require(r, yyjson_mut_get_bool(get(host, "little")), "host little endian");
    scenario_require(r,
                     arch >= 1 && arch <= 3
                         && yyjson_mut_get_uint(get(host, "bits")) == (arch == 1 ? 32 : 64),
                     "host architecture and bits");
    scenario_require(r, os >= 1 && os <= 2 && runtime >= 1 && runtime <= 4, "host OS/runtime");
    scenario_require(r, yyjson_mut_get_bool(get(get(host, "osVersion"), "known")), "host OS known");
    if (runtime == 1)
        scenario_require(r,
                         yyjson_mut_get_bool(get(get(host, "runtimeVersion"), "known")),
                         "glibc version known");
    uint64_t features = yyjson_mut_get_uint(get(host, "features"));
    uint64_t baseline = arch == 1 ? 1 : arch == 2 ? 2 : (UINT64_C(1) << 12) | (UINT64_C(1) << 13);
    scenario_require(r, (features & baseline) == baseline, "host baseline features");
    yyjson_mut_val *p = target(r, c, arch == 3 ? 1 : arch == 1 ? 5 : 0);
    str(c, p, "os", os == 2 ? "windows" : "linux");
    str(c,
        get(p, "runtime"),
        "family",
        runtime == 1   ? "glibc"
        : runtime == 2 ? "musl"
        : runtime == 3 ? "msvcrt"
                       : "ucrt");
    put(c, p, "minimumOs", copy(c, get(get(host, "osVersion"), "components")));
    const char *name = os == 2 ? "native/fixture.dll" : "native/fixture.so";
    check(r,
          c,
          array(c, variant(r, c, p, os == 2 ? "native/host.dll" : "native/host.so", 1), NULL, NULL),
          NULL,
          OK,
          "detect");
    const char *work = scenario_work_directory(scenario_run_context(r));
    const char *source = language_join(r, work, "fixture.c"),
               *library = language_join(r, work, os == 2 ? "fixture.dll" : "fixture.so");
    const char text[] = "#ifdef _WIN32\n__declspec(dllexport)\n#endif\nint "
                        "goat_metadata_fixture(int value) { return value * 2 + 4; }\n";
    language_write(r, source, text, sizeof(text) - 1);
    const char *compiler = getenv("CC");
    if (!compiler || !*compiler)
        compiler = MIGRATION_CC;
    const char *march = arch == 1 ? "-march=i686" : arch == 2 ? "-march=x86-64" : "-march=armv8-a";
    const char *macro_args[] = {compiler,
                                march,
                                "-O2",
                                "-std=c11",
                                "-Wall",
                                "-Wextra",
                                "-Werror",
                                "-include",
                                "stdlib.h",
                                "-dM",
                                "-E",
                                "-x",
                                "c",
                                source,
                                NULL};
    scenario_command_result_t macros = language_command(r, compiler, macro_args, NULL);
    scenario_require(r, macros.exit_code == 0, "compiler macros");
    char *macro_text = owned(r, calloc(macros.out_size + 1, 1));
    memcpy(macro_text, macros.out, macros.out_size);
    if (os == 2)
        scenario_require(r,
                         runtime == (strstr(macro_text, "#define _UCRT ") ? 4u : 3u),
                         "compiler Windows runtime");
    else if (strstr(macro_text, "#define __GLIBC__ "))
        scenario_require(r, runtime == 1, "compiler glibc runtime");
    if (arch == 2) {
        scenario_require(r,
                         strstr(macro_text, "#define __SSE2__ 1") != NULL,
                         "compiler SSE2 baseline");
        scenario_require(r,
                         !strstr(macro_text, "#define __AVX__ ")
                             && !strstr(macro_text, "#define __AVX2__ ")
                             && !strstr(macro_text, "#define __AVX512F__ "),
                         "compiler no elevated x86 features");
    } else if (arch == 3) {
        scenario_require(r,
                         strstr(macro_text, "#define __ARM_NEON 1") != NULL,
                         "compiler NEON baseline");
        scenario_require(r,
                         !strstr(macro_text, "#define __ARM_FEATURE_SVE ")
                             && !strstr(macro_text, "#define __ARM_FEATURE_CRYPTO ")
                             && !strstr(macro_text, "#define __ARM_FEATURE_ATOMICS "),
                         "compiler no elevated ARM features");
    }
    const char *args[16] =
        {compiler, march, "-O2", "-std=c11", "-Wall", "-Wextra", "-Werror", "-shared"};
    size_t n = 8;
    if (os != 2)
        args[n++] = "-fPIC";
    args[n++] = source;
    args[n++] = "-o";
    args[n++] = library;
    args[n] = NULL;
    scenario_command_result_t built = language_command(r, compiler, args, NULL);
    scenario_require(r, built.exit_code == 0, "compile native fixture");
    size_t payload_size;
    const char *payload = language_read(r, library, &payload_size);
    yyjson_mut_val *item = variant(r, c, p, name, 1);
    char digest[65];
    format_binary_sha256(payload, payload_size, digest);
    str(c, item, "hash", digest);
    uint32_t abi = 1;
    yyjson_val *result = execute(r,
                                 c,
                                 array(c, item, NULL, NULL),
                                 NULL,
                                 NULL,
                                 &abi,
                                 1,
                                 "load",
                                 library,
                                 name,
                                 (bytes_t){(unsigned char *)payload, payload_size});
    field(r, result, "status", OK);
    field(r, result, "nativeResult", 42);
    printf("Native metadata checks passed: %zu; real baseline native call\n", c->count);
}

static void body(scenario_run_t *r, void *user) {
    configuration_t *c = user;
    c->doc = yyjson_mut_doc_new(NULL);
    scenario_require(r, c->doc != NULL, "JSON arena");
    scenario_defer(r, free_mut, c->doc);
    bytes_t b = read_file(r, c->vectors, "empty.module.json");
    c->base = parse_n(r, c, (const char *)b.data, b.size);
    b = read_file(r, c->vectors, "native-description.json");
    c->descriptor = parse_n(r, c, (const char *)b.data, b.size);
    b = read_file(r, c->vectors, "../schema-source.json");
    char error[256] = {0};
    c->schema = format_json_parse((const char *)b.data, b.size, error, sizeof(error));
    scenario_require(r, c->schema != NULL, error);
    scenario_defer(r, free_mut, c->schema);
    c->bytecode = read_file(r, c->vectors, "empty.bytecode.bin");
    const char *args[] = {c->exe, NULL};
    scenario_command_result_t api = language_command(r, c->exe, args, NULL);
    scenario_require(r, api.exit_code == 0, "native API checks");
    selection(r, c);
    cpu(r, c);
    metadata(r, c);
    real_library(r, c);
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
        puts("integration.module_native");
        result = 0;
    } else if (argc == 6 || argc == 7) {
        fs_error_t error = {0};
        char *exe = absolute_fs_path(argv[2], &error);
        clear_fs_error(&error);
        char *vectors = absolute_fs_path(argv[5], &error);
        clear_fs_error(&error);
        if (exe && vectors) {
            configuration_t c = {.exe = exe, .vectors = vectors};
            scenario_definition_t definition = {"integration.module_native",
                                                180000,
                                                body,
                                                &c,
                                                NULL};
            scenario_options_t options = {.build_directory = argv[3], .data_directory = argv[4]};
            const char *selection[] = {argv[1], argc == 7 ? argv[6] : NULL};
            result = scenario_registry_main(&definition, 1, argc == 7 ? 2 : 1, selection, &options);
        }
        FREE(vectors);
        FREE(exe);
    } else
        fputs("usage: module_native_scenarios ID HOST BUILD DATA VECTORS [--keep-work]\n", stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
