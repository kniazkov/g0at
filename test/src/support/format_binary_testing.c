/** @file format_binary_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent binary oracle parity and deliberate-corruption checks.
 */
#include "test/support/format_binary.h"
#include "test/support/format_fixtures.h"
#include "test/support/format_zip.h"
#include "test/support/registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c);                                \
            ok = false;                                                                            \
            goto done;                                                                             \
        }                                                                                          \
    } while (0)

static const char *str(yyjson_mut_val *v, const char *k) {
    return yyjson_mut_get_str(yyjson_mut_obj_get(v, k));
}

static unsigned char *read_bytes(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END)) {
        fclose(f);
        return NULL;
    }
    long n = ftell(f);
    if (n < 0 || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        return NULL;
    }
    unsigned char *p = malloc((size_t)n + 1);
    if (!p) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(p, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) {
        free(p);
        return NULL;
    }
    p[n] = 0;
    *size = (size_t)n;
    return p;
}

static yyjson_mut_doc *read_json(const char *path) {
    size_t n;
    unsigned char *p = read_bytes(path, &n);
    if (!p)
        return NULL;
    char error[256];
    yyjson_mut_doc *doc = format_json_parse((char *)p, n, error, sizeof(error));
    free(p);
    return doc;
}

static unsigned char *unhex(const char *text, size_t *size) {
    if (!text)
        return NULL;
    size_t n = strlen(text);
    while (n && (text[n - 1] == '\n' || text[n - 1] == '\r'))
        n--;
    if (n % 2)
        return NULL;
    unsigned char *p = malloc(n / 2 + 1);
    if (!p)
        return NULL;
    for (size_t i = 0; i < n; i += 2) {
        unsigned a = (unsigned)(text[i] >= 'a' ? text[i] - 'a' + 10 : text[i] - '0');
        unsigned b = (unsigned)(text[i + 1] >= 'a' ? text[i + 1] - 'a' + 10 : text[i + 1] - '0');
        if (a > 15 || b > 15) {
            free(p);
            return NULL;
        }
        p[i / 2] = (unsigned char)(a * 16 + b);
    }
    *size = n / 2;
    return p;
}

static bool same(const unsigned char *a, size_t na, const unsigned char *b, size_t nb) {
    return a && b && na == nb && !memcmp(a, b, na);
}

static bool
corpus_row(yyjson_mut_val *row, const char *group, yyjson_mut_val *source, const char *mutant) {
    bool ok = true;
    char error[256] = {0};
    size_t n = 0, out_n = 0, expected_n = 0;
    unsigned char *in = unhex(str(row, "inputHex"), &n), *out = NULL, *expected = NULL,
                  *snapshot = NULL;
    yyjson_mut_doc *copy = NULL;
    CHECK(in);
    snapshot = malloc(n + 1);
    CHECK(snapshot);
    memcpy(snapshot, in, n);
    if (!strcmp(group, "fixtures")) {
        copy = yyjson_mut_doc_new(NULL);
        CHECK(copy);
        yyjson_mut_val *metadata =
            yyjson_mut_val_mut_copy(copy, yyjson_mut_obj_get(row, "metadata"));
        CHECK(metadata);
        yyjson_mut_doc_set_root(copy, metadata);
        out = format_fixture_external_declarations(copy,
                                                   metadata,
                                                   in,
                                                   n,
                                                   &out_n,
                                                   error,
                                                   sizeof(error));
        expected = unhex(str(row, "outputHex"), &expected_n);
        if (mutant && !strcmp(mutant, "corrupt-output") && out_n)
            out[0] ^= 1;
        CHECK(same(out, out_n, expected, expected_n));
        CHECK(format_json_equal(metadata, yyjson_mut_obj_get(row, "expectedMetadata")));
        CHECK(format_binary_check(out, out_n, error, sizeof(error)));
        yyjson_mut_val *exports = yyjson_mut_obj_get(metadata, "externals");
        if (yyjson_mut_obj_size(exports)) {
            size_t before_n = 0, after_n = 0;
            char *before = format_json_canonical(metadata, &before_n);
            size_t rejected_n = 0;
            unsigned char *rejected = format_fixture_external_declarations(copy,
                                                                           metadata,
                                                                           in,
                                                                           63,
                                                                           &rejected_n,
                                                                           error,
                                                                           sizeof(error));
            bool rejected_short = rejected == NULL;
            free(rejected);
            unsigned char *bad = malloc(n);
            CHECK(bad);
            memcpy(bad, in, n);
            bad[28] = 2;
            rejected = format_fixture_external_declarations(copy,
                                                            metadata,
                                                            bad,
                                                            n,
                                                            &rejected_n,
                                                            error,
                                                            sizeof(error));
            bool rejected_multifile = rejected == NULL;
            free(rejected);
            free(bad);
            char *after = format_json_canonical(metadata, &after_n);
            bool unchanged =
                same((unsigned char *)before, before_n, (unsigned char *)after, after_n);
            free(before);
            free(after);
            CHECK(rejected_short && rejected_multifile && unchanged);
        }
    } else {
        bool valid = !strcmp(group, "bytecode")
                         ? format_binary_check(in, n, error, sizeof(error))
                         : format_binary_check_module(yyjson_mut_obj_get(row, "metadata"),
                                                      source,
                                                      in,
                                                      n,
                                                      error,
                                                      sizeof(error));
        bool wanted = yyjson_mut_get_bool(yyjson_mut_obj_get(row, "valid"));
        if (mutant && !strcmp(mutant, "accept-invalid") && !wanted)
            valid = true;
        CHECK(valid == wanted);
    }
    CHECK(!memcmp(in, snapshot, n));
done:
    free(snapshot);
    if (!ok)
        fprintf(stderr, "Corpus case %s: %s\n", str(row, "name"), error);
    free(in);
    free(out);
    free(expected);
    yyjson_mut_doc_free(copy);
    return ok;
}

static bool
corpus(const char *data, const char *group, yyjson_mut_val *source, const char *mutant) {
    char path[4096];
    snprintf(path, sizeof(path), "%s/binary-oracle/%s.json", data, group);
    yyjson_mut_doc *doc = read_json(path);
    if (!doc) {
        fprintf(stderr, "Cannot read %s\n", path);
        return false;
    }
    yyjson_mut_val *root = yyjson_mut_doc_get_root(doc), *row;
    size_t i, max, count = 0;
    bool ok = true;
    yyjson_mut_arr_foreach(root, i, max, row) {
        if (!corpus_row(row, group, source, mutant)) {
            ok = false;
            break;
        }
        count++;
    }
    yyjson_mut_doc_free(doc);
    printf("%s: %zu Python parity cases\n", group, count);
    return ok && count > 0;
}

static unsigned char *vector(const char *schema, const char *name, size_t *n) {
    char path[4096];
    const char *slash = strrchr(schema, '/');
    size_t prefix = slash ? (size_t)(slash - schema) : 0;
    if (prefix + strlen(name) + 10 >= sizeof(path))
        return NULL;
    snprintf(path, sizeof(path), "%.*s/vectors/%s", (int)prefix, schema, name);
    return read_bytes(path, n);
}

static yyjson_mut_doc *vector_json(const char *schema, const char *name) {
    size_t n;
    unsigned char *p = vector(schema, name, &n);
    if (!p)
        return NULL;
    char error[256];
    yyjson_mut_doc *d = format_json_parse((char *)p, n, error, sizeof(error));
    free(p);
    return d;
}

static bool vectors(const char *schema, yyjson_mut_val *source, const char *mutant) {
    bool ok = true;
    char error[256] = {0}, hash[65];
    size_t n = 0, m = 0, hn = 0, jn = 0, an = 0;
    unsigned char *actual = NULL, *expected = NULL, *hex = NULL, *decoded = NULL, *archive = NULL,
                  *native = NULL;
    char *json = NULL;
    yyjson_mut_doc *metadata = NULL, *hashes = NULL, *nd = NULL;
    metadata = vector_json(schema, "empty.module.json");
    hashes = vector_json(schema, "hashes.json");
    CHECK(metadata && hashes);
    yyjson_mut_val *value = yyjson_mut_doc_get_root(metadata),
                   *hash_values = yyjson_mut_doc_get_root(hashes);
    actual = format_binary_minimal(&n);
    expected = vector(schema, "empty.bytecode.bin", &m);
    CHECK(same(actual, n, expected, m));
    if (mutant && !strcmp(mutant, "corrupt-output"))
        actual[0] ^= 1;
    hex = vector(schema, "empty.bytecode.hex", &hn);
    CHECK(hex);
    decoded = unhex((char *)hex, &m);
    CHECK(same(actual, n, decoded, m));
    CHECK(format_binary_check_module(value, source, actual, n, error, sizeof(error)));
    CHECK(format_binary_main_hash(value, source, actual, n, hash, error, sizeof(error)));
    CHECK(!strcmp(hash, str(value, "mainHash")));
    format_binary_sha256(actual, n, hash);
    CHECK(!strcmp(hash, str(hash_values, "bytecodeSha256")));
    free(decoded);
    decoded = unhex(str(hash_values, "hashInputHex"), &m);
    CHECK(decoded);
    format_binary_sha256(decoded, m, hash);
    CHECK(!strcmp(hash, str(value, "mainHash")));
    size_t preimage_n = 0;
    unsigned char *preimage =
        format_metadata_hash_input(value, source, actual, n, &preimage_n, error, sizeof(error));
    bool preimage_equal = same(preimage, preimage_n, decoded, m);
    free(preimage);
    CHECK(preimage_equal);
    free(actual);
    actual = format_binary_importing(&n);
    free(expected);
    expected = vector(schema, "two-versions.bytecode.bin", &m);
    CHECK(same(actual, n, expected, m));
    free(hex);
    hex = vector(schema, "two-versions.bytecode.hex", &hn);
    CHECK(hex);
    free(decoded);
    decoded = unhex((char *)hex, &m);
    CHECK(same(actual, n, decoded, m));
    yyjson_mut_doc_free(metadata);
    metadata = vector_json(schema, "two-versions.module.json");
    CHECK(metadata);
    CHECK(format_binary_check_module(yyjson_mut_doc_get_root(metadata),
                                     source,
                                     actual,
                                     n,
                                     error,
                                     sizeof(error)));
    free(actual);
    actual = NULL;
    free(expected);
    expected = NULL;
    free(hex);
    hex = NULL;
    free(decoded);
    decoded = NULL;
    hex = vector(schema, "native-envelope.hex", &hn);
    CHECK(hex);
    native = unhex((char *)hex, &n);
    CHECK(native && n >= 17);
    CHECK(!memcmp(native, "GOATND01", 8) && native[n - 1] == 0);
    size_t length = 0;
    for (unsigned i = 0; i < 8; i++) {
        if (i >= sizeof(size_t))
            CHECK(native[8 + i] == 0);
        else
            length |= (size_t)native[8 + i] << (8 * i);
    }
    CHECK(length == n - 17);
    nd = format_json_parse((char *)native + 16, length, error, sizeof(error));
    CHECK(nd);
    CHECK(format_metadata_normalize(nd,
                                    yyjson_mut_doc_get_root(nd),
                                    "native",
                                    source,
                                    error,
                                    sizeof(error)));
    json = format_json_canonical(yyjson_mut_doc_get_root(nd), &jn);
    CHECK(json && jn == length && !memcmp(json, native + 16, jn));
    free(json);
    json = NULL;
    yyjson_mut_doc_free(nd);
    nd = vector_json(schema, "native-description.json");
    CHECK(nd);
    json = format_json_canonical(yyjson_mut_doc_get_root(nd), &jn);
    CHECK(json && jn == length && !memcmp(json, native + 16, jn));
    archive = vector(schema, "empty.gbin", &an);
    CHECK(archive);
    format_binary_sha256(archive, an, hash);
    CHECK(!strcmp(hash, str(hash_values, "archiveSha256")));
    format_binary_sha256((const unsigned char *)"abc", 3, hash);
    CHECK(!strcmp(hash, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    format_binary_sha256((const unsigned char *)"", 0, hash);
    CHECK(!strcmp(hash, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
done:
    if (!ok)
        fprintf(stderr, "Vectors: %s\n", error);
    free(actual);
    free(expected);
    free(hex);
    free(decoded);
    free(archive);
    free(native);
    free(json);
    yyjson_mut_doc_free(metadata);
    yyjson_mut_doc_free(hashes);
    yyjson_mut_doc_free(nd);
    return ok;
}

static bool zip_tests(const char *schema) {
    bool ok = true;
    char error[256] = {0};
    size_t mn = 0, bn = 0, gn = 0, n = 0, on = 0;
    unsigned char *module = vector(schema, "empty.module.json", &mn),
                  *bytecode = format_binary_minimal(&bn), *gold = vector(schema, "empty.gbin", &gn);
    unsigned char *archive = NULL, *out = NULL, *bad = NULL;
    format_zip_entry_t entries[3];
    format_zip_info_t info;
    CHECK(module && bytecode && gold);
    format_zip_entry_init(&entries[0], "module.json", module, mn, true);
    format_zip_entry_init(&entries[1], "bytecode.bin", bytecode, bn, true);
    archive = format_zip_build(entries, 2, NULL, 0, false, &n, error, sizeof(error));
    CHECK(same(archive, n, gold, gn));
    for (size_t i = 0; i < 2; i++) {
        CHECK(format_zip_inspect(archive, n, i, &info, error, sizeof(error)));
        CHECK(info.count == 2 && info.method == 8 && info.flags == 0x800 && info.date == 33
              && info.time == 0 && info.external == (0100644u << 16));
        out = format_zip_extract(archive, n, i, 1024 * 1024, &on, error, sizeof(error));
        CHECK(same(out, on, i ? bytecode : module, i ? bn : mn));
        free(out);
        out = NULL;
    }
    CHECK(format_zip_inspect(archive, n, 0, &info, error, sizeof(error)));
    bad = malloc(n);
    CHECK(bad);
    /* Mutate both CRC copies, so equality alone cannot hide corrupted content. */
    memcpy(bad, archive, n);
    CHECK(format_bytes_patch_le(bad, n, info.central_offset + 16, info.crc ^ 1, 4));
    CHECK(format_bytes_patch_le(bad, n, info.local_offset + 14, info.crc ^ 1, 4));
    out = format_zip_extract(bad, n, 0, 1024 * 1024, &on, error, sizeof(error));
    CHECK(!out);
    const size_t offsets[] = {info.central_offset + 20,
                              info.central_offset + 24,
                              info.central_offset + 42,
                              info.local_offset + 18,
                              info.local_offset + 22};
    for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
        memcpy(bad, archive, n);
        CHECK(format_bytes_patch_le(bad, n, offsets[i], UINT32_MAX, 4));
        out = format_zip_extract(bad, n, 0, 1024 * 1024, &on, error, sizeof(error));
        CHECK(!out);
    }
    memcpy(bad, archive, n);
    CHECK(format_bytes_patch_le(bad, n, info.local_offset + 6, 1, 2));
    out = format_zip_extract(bad, n, 0, 1024 * 1024, &on, error, sizeof(error));
    CHECK(!out);
    for (size_t i = 0; i < n; i++) {
        out = format_zip_extract(archive, i, 0, 1024 * 1024, &on, error, sizeof(error));
        CHECK(!out);
    }
    CHECK(!format_bytes_patch_le(bad, n, n, 1, 1));
    CHECK(!format_bytes_patch_le(bad, n, n - 1, 1, 2));
    CHECK(!format_bytes_patch_le(bad, n, 0, 1, 3));
    free(archive);
    archive = NULL;
    free(bad);
    bad = NULL;
    /* Duplicates and binary extras/comments are fixtures, not rejected by writer. */
    const unsigned char payload[] = {0, 'a', 0, 255};
    const unsigned char extra[] = {0xfe, 0xca, 2, 0, 0, 255};
    const unsigned char comment[] = {0, 'x', 255};
    format_zip_entry_init(&entries[0], "same", payload, sizeof(payload), false);
    format_zip_entry_init(&entries[1], "same", "second", 6, true);
    format_zip_entry_init(&entries[2], "empty", "", 0, false);
    entries[0].extra = extra;
    entries[0].extra_size = sizeof(extra);
    entries[0].comment = comment;
    entries[0].comment_size = sizeof(comment);
    for (unsigned stream = 0; stream < 2; stream++) {
        archive = format_zip_build(entries,
                                   3,
                                   comment,
                                   sizeof(comment),
                                   stream != 0,
                                   &n,
                                   error,
                                   sizeof(error));
        CHECK(archive);
        for (size_t i = 0; i < 3; i++) {
            CHECK(format_zip_inspect(archive, n, i, &info, error, sizeof(error)));
            CHECK(info.count == 3 && info.name_size == strlen((const char *)entries[i].name)
                  && !memcmp(info.name, entries[i].name, info.name_size));
            CHECK(info.archive_comment_size == sizeof(comment)
                  && !memcmp(info.archive_comment, comment, sizeof(comment)));
            if (i == 0)
                CHECK(info.extra_size == sizeof(extra) && !memcmp(info.extra, extra, sizeof(extra))
                      && info.comment_size == sizeof(comment));
            out = format_zip_extract(archive, n, i, 1024, &on, error, sizeof(error));
            CHECK(same(out, on, entries[i].data, entries[i].data_size));
            free(out);
            out = NULL;
        }
        out = format_zip_extract(archive, n, 1, 5, &on, error, sizeof(error));
        CHECK(!out);
        free(archive);
        archive = NULL;
    }
    /* ZIP descriptors may legally omit their signature. Removing it requires
     * adjusting the EOCD central-directory offset, not local member offsets. */
    archive = format_zip_build(entries + 1, 1, NULL, 0, true, &n, error, sizeof(error));
    CHECK(archive);
    CHECK(format_zip_inspect(archive, n, 0, &info, error, sizeof(error)));
    size_t unsigned_n = 0;
    bad = format_bytes_splice(archive,
                              n,
                              info.data_offset + info.packed_size,
                              4,
                              NULL,
                              0,
                              &unsigned_n);
    CHECK(bad && unsigned_n == n - 4);
    CHECK(format_bytes_patch_le(bad, unsigned_n, unsigned_n - 6, info.central_offset - 4, 4));
    out = format_zip_extract(bad, unsigned_n, 0, 1024, &on, error, sizeof(error));
    CHECK(same(out, on, entries[1].data, entries[1].data_size));
    free(out);
    out = NULL;
    free(bad);
    bad = NULL;
    free(archive);
    archive = NULL;
    const unsigned char splice_input[] = {1, 2, 3, 4}, splice_insert[] = {0, 255, 0},
                        splice_expected[] = {1, 0, 255, 0, 4};
    bad = format_bytes_splice(splice_input, 4, 1, 2, splice_insert, 3, &n);
    CHECK(same(bad, n, splice_expected, 5));
    free(bad);
    bad = NULL;
    bad = format_bytes_splice(splice_input, 4, 5, 0, NULL, 0, &n);
    CHECK(!bad);
    bad = format_bytes_splice(splice_input, 4, 3, 2, NULL, 0, &n);
    CHECK(!bad);
    bad = format_bytes_splice(splice_input, 4, 0, 0, splice_insert, SIZE_MAX, &n);
    CHECK(!bad);
    archive = format_zip_build(NULL, 0, NULL, 0, false, &n, error, sizeof(error));
    CHECK(archive && n == 22);
    CHECK(format_zip_inspect(archive, n, SIZE_MAX, &info, error, sizeof(error)) && info.count == 0);
    out = format_zip_extract(archive, n, 0, 1024, &on, error, sizeof(error));
    CHECK(!out);
done:
    if (!ok)
        fprintf(stderr, "ZIP: %s\n", error);
    free(module);
    free(bytecode);
    free(gold);
    free(archive);
    free(out);
    free(bad);
    return ok;
}

typedef struct {
    const char *group, *schema, *mutant;
} options_t;

static bool release_doc(void *p) {
    yyjson_mut_doc_free(p);
    return true;
}

static void run_group(scenario_run_t *run, void *user) {
    options_t *options = user;
    const char *data = scenario_data_directory(scenario_run_context(run));
    yyjson_mut_doc *schema = read_json(options->schema);
    scenario_require(run, schema != NULL, "Read independent schema");
    scenario_defer(run, release_doc, schema);
    yyjson_mut_val *source = yyjson_mut_doc_get_root(schema);
    const char *groups[] = {"bytecode", "modules", "fixtures", "vectors", "zip"};
    size_t count = 0;
    for (size_t i = 0; i < sizeof(groups) / sizeof(groups[0]); i++) {
        if (strcmp(options->group, "all") && strcmp(options->group, groups[i]))
            continue;
        scenario_checkpoint(run);
        bool ok;
        if (!strcmp(groups[i], "vectors"))
            ok = vectors(options->schema, source, options->mutant);
        else if (!strcmp(groups[i], "zip"))
            ok = zip_tests(options->schema);
        else
            ok = corpus(data, groups[i], source, options->mutant);
        scenario_require(run, ok, "Independent binary oracle conformance");
        count++;
    }
    scenario_require(run, count > 0, "Known binary oracle group");
}

int main(int argc, char **argv) {
    if (argc < 5 || argc > 6) {
        fprintf(stderr,
                "Usage: %s group build data schema-source [corrupt-output|accept-invalid]\n",
                argv[0]);
        return 1;
    }
    if (argc == 6 && strcmp(argv[5], "corrupt-output") && strcmp(argv[5], "accept-invalid"))
        return 1;
    options_t options = {argv[1], argv[4], argc == 6 ? argv[5] : NULL};
    scenario_definition_t definition = {argv[1], 30000, run_group, &options, NULL};
    scenario_options_t base = {NULL, argv[2], argv[3], false, stderr};
    const char *selection[] = {argv[1]};
    return scenario_registry_main(&definition, 1, 1, selection, &base);
}
