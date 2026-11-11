/** @file module_merge_corpus_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief C-only conversion, frozen equivalence audits and compact-corpus controls.
 */
#include "lib/files/file_system.h"
#include "test/support/format_binary.h"
#include "test/support/inspect_report.h"
#include "test/support/merge_corpus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GET(v, k) yyjson_mut_obj_get((v), (k))
#define REQUIRE(c)                                                                                 \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "merge corpus control failed at line %d: %s\n", __LINE__, #c);         \
            return false;                                                                          \
        }                                                                                          \
    } while (0)
#define DOCUMENT(entries, snapshots, cases)                                                        \
    "{\"format\":\"module-merge-corpus-v1\",\"entries\":" entries ",\"snapshots\":" snapshots      \
    ",\"cases\":" cases "}"
#define ENTRIES "[[\"z\",{\"blob\":\"fixed\",\"metadata\":{\"b\":[3,2,1],\"a\":null}}]]"
#define CASE "{\"id\":\"one\",\"files\":0,\"after\":0}"

static char *read_file(const char *path, size_t *size) {
    FILE *f = FOPEN(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END)) {
        fclose(f);
        return NULL;
    }
    long length = ftell(f);
    if (length < 0 || length > 64 * 1024 * 1024 || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        return NULL;
    }
    char *text = malloc((size_t)length + 1);
    if (!text) {
        fclose(f);
        return NULL;
    }
    bool ok = fread(text, 1, (size_t)length, f) == (size_t)length;
    if (fclose(f))
        ok = false;
    if (!ok) {
        free(text);
        return NULL;
    }
    text[length] = 0;
    *size = (size_t)length;
    return text;
}

static bool emit(FILE *f, yyjson_mut_val *value) {
    size_t size;
    char *text = yyjson_mut_val_write(value, 0, &size);
    if (!text)
        return false;
    bool ok = fwrite(text, 1, size, f) == size;
    free(text);
    return ok;
}

/* One entry/snapshot per line; one case field per line. No giant minified line
 * and no thousands of repeated expanded trees. Table and field order is stable. */
static bool write_packed(FILE *f, yyjson_mut_doc *doc) {
    yyjson_mut_val *root = yyjson_mut_doc_get_root(doc);
    if (fputs("{\n  \"format\": \"module-merge-corpus-v1\",\n", f) < 0)
        return false;
    const char *tables[] = {"entries", "snapshots", "cases"};
    for (size_t t = 0; t < 3; t++) {
        if (fprintf(f, "  \"%s\": [\n", tables[t]) < 0)
            return false;
        size_t i, n;
        yyjson_mut_val *value;
        yyjson_mut_arr_foreach(GET(root, tables[t]), i, n, value) {
            if (t < 2) {
                if (fputs("    ", f) < 0 || !emit(f, value))
                    return false;
            } else {
                if (fputs("    {\n", f) < 0)
                    return false;
                size_t j, count;
                yyjson_mut_val *key, *field;
                yyjson_mut_obj_foreach(value, j, count, key, field) {
                    if (fputs("      ", f) < 0 || !emit(f, key) || fputs(": ", f) < 0
                        || !emit(f, field) || fputs(j + 1 == count ? "\n" : ",\n", f) < 0)
                        return false;
                }
                if (fputs("    }", f) < 0)
                    return false;
            }
            if (fputs(i + 1 == n ? "\n" : ",\n", f) < 0)
                return false;
        }
        if (fputs(t == 2 ? "  ]\n}\n" : "  ],\n", f) < 0)
            return false;
    }
    return true;
}

static yyjson_mut_doc *expand(const char *text) {
    char error[256];
    yyjson_mut_doc *doc = merge_corpus_expand(text, strlen(text), error, sizeof(error));
    if (!doc)
        fprintf(stderr, "%s\n", error);
    return doc;
}

static bool serialized_equal(yyjson_mut_doc *a, yyjson_mut_doc *b) {
    size_t an = 0, bn = 0;
    char *at = yyjson_mut_write(a, 0, &an), *bt = yyjson_mut_write(b, 0, &bn);
    bool same = at && bt && an == bn && !memcmp(at, bt, an);
    free(at);
    free(bt);
    return same;
}

static bool roundtrip(void) {
    /* Differently ordered descriptor objects must NOT be interned together. */
    const char *source =
        "{\"cases\":[{\"id\":\"unicode-α-\\u0061\",\"args\":[\"\\u0000\",true,null],"
        "\"files\":{\"\\u007a\":{\"blob\":\"fixed\",\"mode\":384},"
        "\"a\":{\"mode\":384,\"blob\":\"fixed\"}},"
        "\"after\":{\"a\":{\"mode\":384,\"blob\":\"fixed\"},"
        "\"z\":{\"mode\":384,\"blob\":\"fixed\"}},\"during\":null,"
        "\"more\":{\"z\":[1.5,-2,123456789012345678901234567890],\"a\":false}},"
        "{\"id\":\"two\",\"files\":{},\"after\":{},\"during\":\"corrupt_after_read\"}]}";
    yyjson_mut_doc *frozen = inspect_report_parse(source, strlen(source), NULL, 0);
    yyjson_mut_doc *packed = merge_corpus_pack(source, strlen(source), NULL, 0);
    REQUIRE(frozen && packed);
    REQUIRE(yyjson_mut_arr_size(GET(yyjson_mut_doc_get_root(packed), "entries")) == 3);
    REQUIRE(yyjson_mut_arr_size(GET(yyjson_mut_doc_get_root(packed), "snapshots")) == 3);
    size_t n;
    char *text = yyjson_mut_write(packed, 0, &n);
    REQUIRE(text);
    yyjson_mut_doc *expanded = merge_corpus_expand(text, n, NULL, 0);
    free(text);
    REQUIRE(expanded && serialized_equal(frozen, expanded));
    REQUIRE(merge_corpus_equal(yyjson_mut_doc_get_root(frozen), yyjson_mut_doc_get_root(expanded)));
    text = yyjson_mut_write(expanded, 0, &n);
    REQUIRE(text);
    yyjson_mut_doc *again = merge_corpus_pack(text, n, NULL, 0);
    free(text);
    REQUIRE(again && serialized_equal(packed, again));
    yyjson_mut_doc_free(again);
    yyjson_mut_doc_free(expanded);
    yyjson_mut_doc_free(packed);
    yyjson_mut_doc_free(frozen);
    /* This comparison is order-sensitive even when ordinary JSON equality is not. */
    yyjson_mut_doc *a = inspect_report_parse("{\"z\":1,\"a\":2}", 13, NULL, 0);
    yyjson_mut_doc *b = inspect_report_parse("{\"a\":2,\"z\":1}", 13, NULL, 0);
    REQUIRE(a && b && !merge_corpus_equal(yyjson_mut_doc_get_root(a), yyjson_mut_doc_get_root(b)));
    yyjson_mut_doc_free(a);
    yyjson_mut_doc_free(b);
    return true;
}

static bool bounded(void) {
    const char *source = DOCUMENT(ENTRIES, "[[0]]", "[]");
    yyjson_mut_doc *input = inspect_report_parse(source, strlen(source), NULL, 0);
    REQUIRE(input);
    size_t length = 1024 * 1024;
    char *payload = malloc(length + 1);
    REQUIRE(payload);
    memset(payload, 'x', length);
    payload[length] = 0;
    yyjson_mut_val *root = yyjson_mut_doc_get_root(input);
    yyjson_mut_val *entry = yyjson_mut_arr_get(GET(root, "entries"), 0);
    REQUIRE(yyjson_mut_obj_add_strcpy(input, yyjson_mut_arr_get(entry, 1), "large", payload));
    free(payload);
    yyjson_mut_val *cases = GET(root, "cases");
    for (unsigned i = 0; i < 40; i++) {
        char id[32];
        snprintf(id, sizeof(id), "repeated-%u", i);
        yyjson_mut_val *row = yyjson_mut_obj(input);
        REQUIRE(row && yyjson_mut_obj_add_strcpy(input, row, "id", id)
                && yyjson_mut_obj_add_uint(input, row, "files", 0)
                && yyjson_mut_obj_add_uint(input, row, "after", 0)
                && yyjson_mut_arr_append(cases, row));
    }
    size_t n;
    char *serialized = yyjson_mut_write(input, 0, &n), error[256] = {0};
    REQUIRE(serialized);
    yyjson_mut_doc *doc = merge_corpus_expand(serialized, n, error, sizeof(error));
    free(serialized);
    REQUIRE(!doc && strstr(error, "budget"));
    REQUIRE(yyjson_mut_arr_remove_range(cases, 2, 38));
    serialized = yyjson_mut_write(input, 0, &n);
    REQUIRE(serialized);
    doc = merge_corpus_expand(serialized, n, error, sizeof(error));
    free(serialized);
    REQUIRE(doc);
    yyjson_mut_doc_free(doc);
    yyjson_mut_doc_free(input);
    /* This must reject the declared size without reading past the tiny buffer. */
    doc = merge_corpus_expand("{}", 64U * 1024U * 1024U + 1, error, sizeof(error));
    REQUIRE(!doc && strstr(error, "input exceeds"));
    return true;
}

static bool invalid(void) {
    REQUIRE(bounded());
    const char *inputs[] = {
        "{}",
        "[]",
        "{\"format\":\"module-merge-corpus-v2\",\"entries\":[],\"snapshots\":[],\"cases\":[]}",
        "{\"format\":\"module-merge-corpus-v1\\u0000\",\"entries\":[],\"snapshots\":[],\"cases\":[]"
        "}",
        "{\"format\":\"module-merge-corpus-v1\",\"entries\":[],\"snapshots\":[],\"cases\":[],"
        "\"extra\":0}",
        "{\"format\":\"module-merge-corpus-v1\",\"entries\":[],\"entries\":[],\"snapshots\":[],"
        "\"cases\":[]}",
        DOCUMENT("{}", "[]", "[]"),
        DOCUMENT("[]", "{}", "[]"),
        DOCUMENT("[]", "[]", "{}"),
        DOCUMENT("[null]", "[]", "[]"),
        DOCUMENT("[[\"x\"]]", "[]", "[]"),
        DOCUMENT("[[\"x\",{},0]]", "[]", "[]"),
        DOCUMENT("[[0,{}]]", "[]", "[]"),
        DOCUMENT("[[\"\",{}]]", "[]", "[]"),
        DOCUMENT("[[\"x\\u0000y\",{}]]", "[]", "[]"),
        DOCUMENT("[[\"x\",[]]]", "[]", "[]"),
        DOCUMENT("[[\"x\",{\"a\":1,\"a\":2}]]", "[]", "[]"),
        DOCUMENT(ENTRIES, "[{}]", "[]"),
        DOCUMENT(ENTRIES, "[[1]]", "[]"),
        DOCUMENT(ENTRIES, "[[-1]]", "[]"),
        DOCUMENT(ENTRIES, "[[false]]", "[]"),
        DOCUMENT(ENTRIES, "[[0.0]]", "[]"),
        DOCUMENT(ENTRIES, "[[\"0\"]]", "[]"),
        DOCUMENT(ENTRIES, "[[null]]", "[]"),
        DOCUMENT(ENTRIES, "[[18446744073709551616]]", "[]"),
        DOCUMENT(ENTRIES, "[[0,0]]", "[]"),
        DOCUMENT("[[\"x\",{}],[\"x\",{\"different\":true}]]", "[[0,1]]", "[]"),
        DOCUMENT(ENTRIES, "[[0]]", "[null]"),
        DOCUMENT(ENTRIES, "[[0]]", "[{}]"),
        DOCUMENT(ENTRIES, "[[0]]", "[{\"id\":true,\"files\":0,\"after\":0}]"),
        DOCUMENT(ENTRIES, "[[0]]", "[{\"id\":\"x\\u0000y\",\"files\":0,\"after\":0}]"),
        DOCUMENT(ENTRIES, "[[0]]", "[{\"id\":\"x\",\"files\":0}]"),
        DOCUMENT(ENTRIES, "[[0]]", "[{\"id\":\"x\",\"after\":0}]"),
        DOCUMENT(ENTRIES, "[[0]]", "[" CASE "," CASE "]"),
        DOCUMENT(ENTRIES, "[[0]]", "[{\"id\":\"x\",\"files\":1,\"after\":0}]"),
        DOCUMENT(ENTRIES, "[[0]]", "[{\"id\":\"x\",\"files\":0,\"after\":null}]"),
        DOCUMENT(ENTRIES, "[[0]]", "[{\"id\":\"x\",\"files\":false,\"after\":0}]"),
        DOCUMENT(ENTRIES, "[[0]]", "[{\"id\":\"x\",\"files\":-1,\"after\":0}]"),
        DOCUMENT(ENTRIES, "[[0]]", "[{\"id\":\"x\",\"files\":0.0,\"after\":0}]"),
        DOCUMENT(ENTRIES, "[[0]]", "[{\"id\":\"x\",\"files\":\"0\",\"after\":0}]"),
        DOCUMENT(ENTRIES, "[[0]]", "[{\"id\":\"x\",\"files\":{},\"after\":0}]"),
        DOCUMENT(ENTRIES, "[[0]]", "[{\"id\":\"x\",\"files\":18446744073709551616,\"after\":0}]"),
        DOCUMENT(ENTRIES, "[[0]]", "[{\"id\":\"x\",\"files\":0,\"after\":0,\"during\":true}]"),
        DOCUMENT(ENTRIES, "[[0]]", "[{\"id\":\"x\",\"files\":0,\"after\":0,\"during\":{}}]"),
        DOCUMENT(ENTRIES, "[[0]]", "[{\"id\":\"x\",\"files\":0,\"after\":0,\"during\":1}]"),
        DOCUMENT(ENTRIES, "[[0]]", "[" CASE "]") " trailing",
    };
    for (size_t i = 0; i < sizeof(inputs) / sizeof(*inputs); i++) {
        char error[256] = {0};
        yyjson_mut_doc *doc =
            merge_corpus_expand(inputs[i], strlen(inputs[i]), error, sizeof(error));
        bool rejected = !doc && error[0];
        yyjson_mut_doc_free(doc);
        if (!rejected)
            fprintf(stderr, "Accepted invalid compact input %zu: %s\n", i, inputs[i]);
        REQUIRE(rejected);
    }
    const char *bad_frozen[] = {"{}",
                                "{\"cases\":{}}",
                                "{\"cases\":[],\"extra\":1}",
                                "{\"cases\":[{\"id\":\"x\",\"files\":{},\"after\":null}]}"};
    for (size_t i = 0; i < sizeof(bad_frozen) / sizeof(*bad_frozen); i++) {
        char error[256] = {0};
        yyjson_mut_doc *doc =
            merge_corpus_pack(bad_frozen[i], strlen(bad_frozen[i]), error, sizeof(error));
        bool rejected = !doc && error[0];
        yyjson_mut_doc_free(doc);
        REQUIRE(rejected);
    }
    printf("%zu malformed compact inputs and %zu malformed frozen inputs rejected\n",
           sizeof(inputs) / sizeof(*inputs),
           sizeof(bad_frozen) / sizeof(*bad_frozen));
    return true;
}

static bool isolation(void) {
    const char *source =
        DOCUMENT(ENTRIES,
                 "[[0]]",
                 "[" CASE ", {\"id\":\"two\",\"files\":0,\"after\":0,\"during\":0}]");
    char *owned = malloc(strlen(source) + 1);
    REQUIRE(owned);
    strcpy(owned, source);
    yyjson_mut_doc *a = expand(owned), *b = expand(owned);
    memset(owned, '!', strlen(owned));
    free(owned); /* Expanded keys, strings, and nested values must survive. */
    REQUIRE(a && b && serialized_equal(a, b));
    yyjson_mut_val *cases = GET(yyjson_mut_doc_get_root(a), "cases");
    yyjson_mut_val *first = yyjson_mut_arr_get(cases, 0), *second = yyjson_mut_arr_get(cases, 1);
    yyjson_mut_val *before = GET(GET(first, "files"), "z"), *after = GET(GET(first, "after"), "z");
    REQUIRE(before != after && GET(before, "metadata") != GET(after, "metadata"));
    REQUIRE(yyjson_mut_set_str(GET(before, "blob"), "changed"));
    REQUIRE(yyjson_mut_arr_append(GET(GET(before, "metadata"), "b"), yyjson_mut_uint(a, 99)));
    REQUIRE(yyjson_mut_obj_add_str(a, GET(first, "files"), "new", "only here"));
    REQUIRE(!strcmp(yyjson_mut_get_str(GET(after, "blob")), "fixed"));
    REQUIRE(yyjson_mut_arr_size(GET(GET(after, "metadata"), "b")) == 3);
    REQUIRE(merge_corpus_equal(GET(first, "after"), GET(second, "files")));
    REQUIRE(merge_corpus_equal(GET(second, "files"), GET(second, "after")));
    REQUIRE(merge_corpus_equal(GET(second, "files"), GET(second, "during")));
    yyjson_mut_doc *fresh = expand(source);
    REQUIRE(fresh && serialized_equal(b, fresh) && !serialized_equal(a, b));
    yyjson_mut_doc_free(a);
    REQUIRE(serialized_equal(b, fresh));
    yyjson_mut_doc_free(b);
    yyjson_mut_doc_free(fresh);
    return true;
}

static bool integrity(const char *path) {
    size_t n;
    char *text = read_file(path, &n);
    REQUIRE(text);
    yyjson_mut_doc *a = merge_corpus_expand(text, n, NULL, 0),
                   *b = merge_corpus_expand(text, n, NULL, 0),
                   *compact = inspect_report_parse(text, n, NULL, 0);
    free(text);
    REQUIRE(a && b && compact && serialized_equal(a, b));
    text = yyjson_mut_write(a, 0, &n);
    REQUIRE(text);
    char hash[65];
    format_binary_sha256(text, n, hash);
    /* Frozen before compaction, including every object member's original order.
     * This digest is NOT computed from goat-merge, nor updated during tests. */
    REQUIRE(n == 2146312);
    REQUIRE(!strcmp(hash, "0701ea5f9d73658ea6c571673f65f72548dcb678fb3afe63f23044ad0df5ceea"));
    yyjson_mut_doc *repacked = merge_corpus_pack(text, n, NULL, 0);
    free(text);
    REQUIRE(repacked && serialized_equal(compact, repacked));
    yyjson_mut_val *cases = GET(yyjson_mut_doc_get_root(a), "cases"), *row;
    REQUIRE(yyjson_mut_arr_size(cases) == 196);
    size_t i, count, fields = 0, trees = 0, entries = 0, nulls = 0, actions = 0;
    yyjson_mut_arr_foreach(cases, i, count, row) {
        const char *names[] = {"files", "after", "during"};
        for (size_t k = 0; k < 3; k++) {
            yyjson_mut_val *v = GET(row, names[k]);
            if (!v)
                continue;
            fields++;
            if (yyjson_mut_is_obj(v)) {
                trees++;
                entries += yyjson_mut_obj_size(v);
            } else if (yyjson_mut_is_null(v))
                nulls++;
            else if (yyjson_mut_is_str(v))
                actions++;
            else
                REQUIRE(false);
        }
    }
    REQUIRE(fields == 399 && trees == 392 && entries == 22634 && nulls == 5 && actions == 2);
    printf(
        "196 ordered cases; 399 files/after/during fields; 392 trees; 22634 entries; SHA256 %s\n",
        hash);
    yyjson_mut_doc_free(repacked);
    yyjson_mut_doc_free(compact);
    yyjson_mut_doc_free(b);
    yyjson_mut_doc_free(a);
    return true;
}

int merge_corpus_tool(int argc, char **argv) {
    if (argc != 4) {
        fputs("usage: module_merge_scenarios --corpus-test CONTROL COMPACT | "
              "--corpus-pack FROZEN OUTPUT | --corpus-expand COMPACT OUTPUT | "
              "--corpus-verify FROZEN COMPACT\n",
              stderr);
        return 1;
    }
    if (!strcmp(argv[1], "--corpus-test")) {
        bool ok = !strcmp(argv[2], "roundtrip")   ? roundtrip()
                  : !strcmp(argv[2], "invalid")   ? invalid()
                  : !strcmp(argv[2], "isolation") ? isolation()
                  : !strcmp(argv[2], "integrity") ? integrity(argv[3])
                                                  : false;
        return ok ? 0 : 1;
    }
    bool pack = !strcmp(argv[1], "--corpus-pack"), verify = !strcmp(argv[1], "--corpus-verify");
    if (!pack && !verify && strcmp(argv[1], "--corpus-expand"))
        return 1;
    size_t size;
    char *text = read_file(argv[2], &size), error[256] = "cannot read input";
    yyjson_mut_doc *doc = !text    ? NULL
                          : pack   ? merge_corpus_pack(text, size, error, sizeof(error))
                          : verify ? inspect_report_parse(text, size, error, sizeof(error))
                                   : merge_corpus_expand(text, size, error, sizeof(error));
    free(text);
    if (!doc) {
        fprintf(stderr, "%s: %s\n", argv[2], error);
        return 1;
    }
    bool ok = false;
    if (verify) {
        text = read_file(argv[3], &size);
        yyjson_mut_doc *expanded =
            text ? merge_corpus_expand(text, size, error, sizeof(error)) : NULL;
        free(text);
        ok = expanded && serialized_equal(doc, expanded)
             && merge_corpus_equal(yyjson_mut_doc_get_root(doc), yyjson_mut_doc_get_root(expanded));
        yyjson_mut_doc_free(expanded);
        if (ok)
            puts("Frozen and expanded JSON match exactly, including all object and array order.");
        else
            fputs("Frozen/expanded corpus mismatch or invalid input\n", stderr);
    } else {
        FILE *f = FOPEN(argv[3], "wb");
        if (f) {
            ok = pack ? write_packed(f, doc) : emit(f, yyjson_mut_doc_get_root(doc));
            if (fclose(f))
                ok = false;
        }
        if (!ok)
            fprintf(stderr, "cannot write %s\n", argv[3]);
    }
    yyjson_mut_doc_free(doc);
    return ok ? 0 : 1;
}
