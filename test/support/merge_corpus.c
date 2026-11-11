/** @file merge_corpus.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Nonrecursive dictionary expansion of independently frozen test data.
 */
#include "merge_corpus.h"

#include "inspect_report.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CORPUS_LIMIT (64U * 1024U * 1024U)

#define GET(v, k) yyjson_mut_obj_get((v), (k))
#define CHECK(condition, message)                                                                  \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            if (error && error_size)                                                               \
                snprintf(error, error_size, "%s", message);                                        \
            goto fail;                                                                             \
        }                                                                                          \
    } while (0)

static bool text(yyjson_mut_val *v) {
    return yyjson_mut_is_str(v) && yyjson_mut_get_len(v)
           && strlen(yyjson_mut_get_str(v)) == yyjson_mut_get_len(v);
}

static bool named(yyjson_mut_val *v, const char *name) {
    return yyjson_mut_is_str(v) && yyjson_mut_get_len(v) == strlen(name)
           && !memcmp(yyjson_mut_get_str(v), name, strlen(name));
}

static bool snapshot_key(yyjson_mut_val *key) {
    return named(key, "files") || named(key, "after") || named(key, "during");
}

bool merge_corpus_equal(yyjson_mut_val *a, yyjson_mut_val *b) {
    if (!a || !b || yyjson_mut_get_type(a) != yyjson_mut_get_type(b)
        || (yyjson_mut_is_num(a) && yyjson_mut_get_subtype(a) != yyjson_mut_get_subtype(b)))
        return false;
    if (yyjson_mut_is_obj(a)) {
        if (yyjson_mut_obj_size(a) != yyjson_mut_obj_size(b))
            return false;
        yyjson_mut_obj_iter other = yyjson_mut_obj_iter_with(b);
        size_t i, n;
        yyjson_mut_val *k, *v;
        yyjson_mut_obj_foreach(a, i, n, k, v) {
            yyjson_mut_val *q = yyjson_mut_obj_iter_next(&other);
            if (!yyjson_mut_equals(k, q) || !merge_corpus_equal(v, yyjson_mut_obj_iter_get_val(q)))
                return false;
        }
        return true;
    }
    if (yyjson_mut_is_arr(a)) {
        if (yyjson_mut_arr_size(a) != yyjson_mut_arr_size(b))
            return false;
        yyjson_mut_arr_iter other = yyjson_mut_arr_iter_with(b);
        size_t i, n;
        yyjson_mut_val *v;
        yyjson_mut_arr_foreach(a, i, n, v) if (!merge_corpus_equal(
                                                   v,
                                                   yyjson_mut_arr_iter_next(&other))) return false;
        return true;
    }
    return yyjson_mut_equals(a, b);
}

static bool reference(yyjson_mut_val *v, yyjson_mut_val *table) {
    return yyjson_mut_is_uint(v) && yyjson_mut_get_uint(v) < yyjson_mut_arr_size(table);
}

/* Count owning DOM nodes and copied string/raw bytes before allocating output.
 * Subtract only after checking: reference repetition cannot overflow the budget
 * or turn a tiny dictionary into an unbounded expansion. Parser nesting limits
 * apply to literals; snapshot references themselves never recurse. */
static bool consume(size_t *remaining, size_t size) {
    if (size > *remaining)
        return false;
    *remaining -= size;
    return true;
}

static bool literal_budget(yyjson_mut_val *v, size_t *remaining) {
    if (!consume(remaining, sizeof(yyjson_mut_val)))
        return false;
    if (yyjson_mut_is_str(v) || yyjson_mut_is_raw(v))
        return consume(remaining, yyjson_mut_get_len(v)) && consume(remaining, 1);
    size_t i, n;
    yyjson_mut_val *key, *value;
    if (yyjson_mut_is_obj(v)) {
        yyjson_mut_obj_foreach(v, i, n, key, value) if (!literal_budget(key, remaining)
                                                        || !literal_budget(value,
                                                                           remaining)) return false;
    } else if (yyjson_mut_is_arr(v)) {
        yyjson_mut_arr_foreach(v, i, n, value) if (!literal_budget(value, remaining)) return false;
    }
    return true;
}

static bool tree_budget(yyjson_mut_val *entries, yyjson_mut_val *snapshot, size_t *remaining) {
    if (!consume(remaining, sizeof(yyjson_mut_val)))
        return false;
    size_t i, n;
    yyjson_mut_val *ref;
    yyjson_mut_arr_foreach(snapshot, i, n, ref) {
        yyjson_mut_val *entry = yyjson_mut_arr_get(entries, (size_t)yyjson_mut_get_uint(ref));
        if (!literal_budget(yyjson_mut_arr_get(entry, 0), remaining)
            || !literal_budget(yyjson_mut_arr_get(entry, 1), remaining))
            return false;
    }
    return true;
}

static yyjson_mut_val *copy(yyjson_mut_doc *doc, yyjson_mut_val *value) {
    return yyjson_mut_val_mut_copy(doc, value);
}

static bool
add(yyjson_mut_doc *doc, yyjson_mut_val *object, yyjson_mut_val *key, yyjson_mut_val *value) {
    return yyjson_mut_obj_add(object, copy(doc, key), value);
}

static yyjson_mut_val *
tree(yyjson_mut_doc *doc, yyjson_mut_val *entries, yyjson_mut_val *snapshot) {
    yyjson_mut_val *out = yyjson_mut_obj(doc);
    if (!out)
        return NULL;
    size_t i, n;
    yyjson_mut_val *ref;
    yyjson_mut_arr_foreach(snapshot, i, n, ref) {
        yyjson_mut_val *entry = yyjson_mut_arr_get(entries, (size_t)yyjson_mut_get_uint(ref));
        if (!add(doc, out, yyjson_mut_arr_get(entry, 0), copy(doc, yyjson_mut_arr_get(entry, 1))))
            return NULL;
    }
    return out;
}

yyjson_mut_doc *merge_corpus_expand(const char *data, size_t size, char *error, size_t error_size) {
    if (size > CORPUS_LIMIT) {
        if (error && error_size)
            snprintf(error, error_size, "merge corpus input exceeds 64 MiB");
        return NULL;
    }
    yyjson_mut_doc *input = inspect_report_parse(data, size, error, error_size), *doc = NULL;
    if (!input)
        return NULL;
    yyjson_mut_val *root = yyjson_mut_doc_get_root(input);
    yyjson_mut_val *entries = GET(root, "entries"), *snapshots = GET(root, "snapshots"),
                   *cases = GET(root, "cases");
    CHECK(yyjson_mut_is_obj(root) && yyjson_mut_obj_size(root) == 4
              && named(GET(root, "format"), "module-merge-corpus-v1") && yyjson_mut_is_arr(entries)
              && yyjson_mut_is_arr(snapshots) && yyjson_mut_is_arr(cases),
          "invalid merge corpus envelope");
    size_t i, n;
    yyjson_mut_val *entry, *snapshot, *record;
    yyjson_mut_arr_foreach(entries, i, n, entry) {
        CHECK(yyjson_mut_is_arr(entry) && yyjson_mut_arr_size(entry) == 2
                  && text(yyjson_mut_arr_get(entry, 0))
                  && yyjson_mut_is_obj(yyjson_mut_arr_get(entry, 1)),
              "invalid merge corpus entry");
    }
    yyjson_mut_arr_foreach(snapshots, i, n, snapshot) {
        CHECK(yyjson_mut_is_arr(snapshot), "invalid merge corpus snapshot");
        size_t j, m;
        yyjson_mut_val *ref;
        yyjson_mut_arr_foreach(snapshot, j, m, ref) {
            CHECK(reference(ref, entries), "invalid merge corpus entry reference");
            entry = yyjson_mut_arr_get(entries, (size_t)yyjson_mut_get_uint(ref));
            yyjson_mut_val *path = yyjson_mut_arr_get(entry, 0), *prior;
            size_t k, count;
            yyjson_mut_arr_foreach(snapshot, k, count, prior) {
                if (k == j)
                    break;
                yyjson_mut_val *previous =
                    yyjson_mut_arr_get(entries, (size_t)yyjson_mut_get_uint(prior));
                CHECK(!yyjson_mut_equals(path, yyjson_mut_arr_get(previous, 0)),
                      "duplicate merge corpus snapshot path");
            }
        }
    }
    size_t remaining = CORPUS_LIMIT;
    CHECK(consume(&remaining, 3 * sizeof(yyjson_mut_val) + sizeof("cases")),
          "merge corpus expansion budget exceeded");
    yyjson_mut_arr_foreach(cases, i, n, record) {
        CHECK(yyjson_mut_is_obj(record) && text(GET(record, "id")) && GET(record, "files")
                  && GET(record, "after"),
              "invalid merge corpus case");
        size_t j, count;
        yyjson_mut_val *prior;
        yyjson_mut_arr_foreach(cases, j, count, prior) {
            if (j == i)
                break;
            CHECK(!yyjson_mut_equals(GET(record, "id"), GET(prior, "id")),
                  "duplicate merge corpus case ID");
        }
        CHECK(consume(&remaining, sizeof(yyjson_mut_val)),
              "merge corpus expansion budget exceeded");
        yyjson_mut_val *key, *value;
        yyjson_mut_obj_foreach(record, j, count, key, value) {
            CHECK(literal_budget(key, &remaining), "merge corpus expansion budget exceeded");
            if (snapshot_key(key)
                && !(named(key, "during")
                     && (yyjson_mut_is_null(value) || yyjson_mut_is_str(value)))) {
                CHECK(reference(value, snapshots), "invalid merge corpus snapshot reference");
                snapshot = yyjson_mut_arr_get(snapshots, (size_t)yyjson_mut_get_uint(value));
                CHECK(tree_budget(entries, snapshot, &remaining),
                      "merge corpus expansion budget exceeded");
            } else
                CHECK(literal_budget(value, &remaining), "merge corpus expansion budget exceeded");
        }
    }
    doc = yyjson_mut_doc_new(NULL);
    CHECK(doc, "merge corpus allocation failed");
    yyjson_mut_val *out = yyjson_mut_obj(doc), *expanded = yyjson_mut_arr(doc);
    CHECK(out && expanded && yyjson_mut_obj_add_val(doc, out, "cases", expanded),
          "merge corpus allocation failed");
    yyjson_mut_doc_set_root(doc, out);
    yyjson_mut_arr_foreach(cases, i, n, record) {
        yyjson_mut_val *row = yyjson_mut_obj(doc), *key, *value;
        CHECK(row && yyjson_mut_arr_append(expanded, row), "merge corpus allocation failed");
        size_t j, count;
        yyjson_mut_obj_foreach(record, j, count, key, value) {
            yyjson_mut_val *v;
            if (snapshot_key(key)
                && !(named(key, "during")
                     && (yyjson_mut_is_null(value) || yyjson_mut_is_str(value)))) {
                snapshot = yyjson_mut_arr_get(snapshots, (size_t)yyjson_mut_get_uint(value));
                v = tree(doc, entries, snapshot);
            } else
                v = copy(doc, value);
            CHECK(v && add(doc, row, key, v), "merge corpus allocation failed");
        }
    }
    yyjson_mut_doc_free(input);
    return doc;
fail:
    yyjson_mut_doc_free(input);
    yyjson_mut_doc_free(doc);
    return NULL;
}

/* The first equal item wins: no hash-table iteration or sorting affects order. */
static yyjson_mut_val *intern(yyjson_mut_doc *doc, yyjson_mut_val *table, yyjson_mut_val *value) {
    size_t i, n;
    yyjson_mut_val *existing;
    yyjson_mut_arr_foreach(table, i, n, existing) if (merge_corpus_equal(
                                                          existing,
                                                          value)) return yyjson_mut_uint(doc, i);
    size_t index = yyjson_mut_arr_size(table);
    return yyjson_mut_arr_append(table, value) ? yyjson_mut_uint(doc, index) : NULL;
}

yyjson_mut_doc *merge_corpus_pack(const char *data, size_t size, char *error, size_t error_size) {
    if (size > CORPUS_LIMIT) {
        if (error && error_size)
            snprintf(error, error_size, "merge corpus input exceeds 64 MiB");
        return NULL;
    }
    yyjson_mut_doc *input = inspect_report_parse(data, size, error, error_size), *doc = NULL;
    if (!input)
        return NULL;
    yyjson_mut_val *root = yyjson_mut_doc_get_root(input), *cases = GET(root, "cases");
    CHECK(yyjson_mut_is_obj(root) && yyjson_mut_obj_size(root) == 1 && yyjson_mut_is_arr(cases),
          "frozen merge corpus must contain only cases");
    doc = yyjson_mut_doc_new(NULL);
    CHECK(doc, "merge corpus allocation failed");
    yyjson_mut_val *out = yyjson_mut_obj(doc), *entries = yyjson_mut_arr(doc),
                   *snapshots = yyjson_mut_arr(doc), *packed = yyjson_mut_arr(doc);
    CHECK(out && entries && snapshots && packed
              && yyjson_mut_obj_add_str(doc, out, "format", "module-merge-corpus-v1")
              && yyjson_mut_obj_add_val(doc, out, "entries", entries)
              && yyjson_mut_obj_add_val(doc, out, "snapshots", snapshots)
              && yyjson_mut_obj_add_val(doc, out, "cases", packed),
          "merge corpus allocation failed");
    yyjson_mut_doc_set_root(doc, out);
    size_t i, n;
    yyjson_mut_val *record;
    yyjson_mut_arr_foreach(cases, i, n, record) {
        CHECK(yyjson_mut_is_obj(record), "invalid frozen merge case");
        yyjson_mut_val *row = yyjson_mut_obj(doc), *key, *value;
        CHECK(row && yyjson_mut_arr_append(packed, row), "merge corpus allocation failed");
        size_t j, count;
        yyjson_mut_obj_foreach(record, j, count, key, value) {
            yyjson_mut_val *v;
            if (snapshot_key(key) && yyjson_mut_is_obj(value)) {
                yyjson_mut_val *refs = yyjson_mut_arr(doc), *path, *item;
                CHECK(refs, "merge corpus allocation failed");
                size_t k, length;
                yyjson_mut_obj_foreach(value, k, length, path, item) {
                    yyjson_mut_val *entry = yyjson_mut_arr(doc);
                    CHECK(entry && yyjson_mut_arr_append(entry, copy(doc, path))
                              && yyjson_mut_arr_append(entry, copy(doc, item))
                              && yyjson_mut_arr_append(refs, intern(doc, entries, entry)),
                          "merge corpus allocation failed");
                }
                v = intern(doc, snapshots, refs);
            } else
                v = copy(doc, value);
            CHECK(v && add(doc, row, key, v), "merge corpus allocation failed");
        }
    }
    /* Conversion must round-trip before a caller can publish any output. */
    size_t length;
    char *serialized = yyjson_mut_write(doc, 0, &length);
    yyjson_mut_doc *check =
        serialized ? merge_corpus_expand(serialized, length, error, error_size) : NULL;
    bool exact = check && merge_corpus_equal(root, yyjson_mut_doc_get_root(check));
    free(serialized);
    yyjson_mut_doc_free(check);
    CHECK(exact, "merge corpus conversion did not preserve the frozen document");
    yyjson_mut_doc_free(input);
    return doc;
fail:
    yyjson_mut_doc_free(input);
    yyjson_mut_doc_free(doc);
    return NULL;
}
