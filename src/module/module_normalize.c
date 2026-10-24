/** @file module_normalize.c
 * @brief Validated, idempotent module-v1 normalization before canonical encoding.
 */
#include "module_json_internal.h"

#include <goat/integer_range.h>
#include <stdlib.h>
#include <string.h>

typedef yyjson_mut_val value;

typedef struct {
    yyjson_mut_doc *doc;
    goat_module_json_error error;
} context;

static value *get(value *obj, const char *key) {
    return yyjson_mut_obj_get(obj, key);
}

static bool is(value *v, const char *s) {
    return yyjson_mut_is_str(v) && yyjson_mut_get_len(v) == strlen(s)
           && !memcmp(yyjson_mut_get_str(v), s, strlen(s));
}

static int text_compare(value *a, value *b) {
    size_t na = yyjson_mut_get_len(a), nb = yyjson_mut_get_len(b);
    int cmp = memcmp(yyjson_mut_get_str(a), yyjson_mut_get_str(b), na < nb ? na : nb);
    return cmp ? cmp : (na > nb) - (na < nb);
}

static int64_t number(value *v) {
    /* yyjson setters inspect the existing tag before replacing a scalar. */
    yyjson_val temp = {0};
    if (yyjson_mut_is_raw(v))
        yyjson_set_raw(&temp, yyjson_mut_get_raw(v), yyjson_mut_get_len(v));
    else if (yyjson_mut_is_sint(v))
        yyjson_set_sint(&temp, yyjson_mut_get_sint(v));
    else
        yyjson_set_uint(&temp, yyjson_mut_get_uint(v));
    int64_t n = 0;
    goat_json_integer(&temp, &n); /* The complete schema has already checked the token. */
    return n;
}

static bool require(context *ctx, bool condition) {
    if (!condition && !ctx->error)
        ctx->error = GOAT_MODULE_JSON_INVALID_FIELD;
    return condition;
}

static bool allocated(context *ctx, bool condition) {
    if (!condition && !ctx->error)
        ctx->error = GOAT_MODULE_JSON_MEMORY;
    return condition;
}

static bool scalar(context *ctx, value *type, value *v) {
    bool valid = false;
    if (is(type, "integer"))
        valid = yyjson_mut_is_raw(v) || yyjson_mut_is_int(v);
    else if (is(type, "boolean"))
        valid = yyjson_mut_is_bool(v);
    else if (is(type, "null"))
        valid = yyjson_mut_is_null(v);
    else if (is(type, "string"))
        valid = yyjson_mut_is_str(v);
    else if (is(type, "character"))
        valid = yyjson_mut_is_str(v)
                && goat_json_scalar_count(yyjson_mut_get_str(v), yyjson_mut_get_len(v)) == 1;
    else if (is(type, "real")) {
        valid = yyjson_mut_is_str(v) && yyjson_mut_get_len(v) == 16;
        for (size_t i = 0; valid && i < 16; i++) {
            char ch = yyjson_mut_get_str(v)[i];
            valid = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
        }
    }
    return require(ctx, valid);
}

static void facts(context *ctx, value *obj) {
    if (ctx->error)
        return;
    value *type = get(obj, "type"), *constant = get(obj, "value");
    value *range = get(obj, "range"), *elements = get(obj, "elements");
    if (!require(ctx, !(is(type, "null") && get(obj, "nonNull"))))
        return;
    if (constant && !scalar(ctx, type, constant))
        return;
    if (range) {
        if (!require(ctx, is(type, "integer")))
            return;
        int64_t min = number(get(range, "min")), max = number(get(range, "max"));
        goat_integer_range_kind kind = goat_classify_integer_range(min, max);
        if (!require(ctx, kind != GOAT_RANGE_INVALID))
            return;
        if (constant) {
            int64_t n = number(constant);
            if (!require(ctx, n >= min && n <= max))
                return;
        } else if (kind == GOAT_RANGE_CONSTANT) {
            /* Preserve the exact RAW integer token; no machine float conversion. */
            value *min_value = yyjson_mut_obj_remove_key(range, "min");
            if (!allocated(ctx, yyjson_mut_obj_add_val(ctx->doc, obj, "value", min_value)))
                return;
        }
        if (constant || kind == GOAT_RANGE_CONSTANT || kind == GOAT_RANGE_FULL)
            yyjson_mut_obj_remove_key(obj, "range");
    }
    if (elements) {
        if (!require(ctx, is(type, "array")))
            return;
        facts(ctx, elements); /* Even empty:true cannot hide invalid nested facts. */
        if (ctx->error)
            return;
        if (!yyjson_mut_obj_size(elements) || get(obj, "empty"))
            yyjson_mut_obj_remove_key(obj, "elements");
    }
    if (!require(ctx, !get(obj, "empty") || is(type, "array")))
        return;
    if (is(type, "null"))
        yyjson_mut_obj_remove_key(obj, "value");
    else if (type)
        yyjson_mut_obj_remove_key(obj, "nonNull");
}

typedef enum {
    SORT_TEXT,
    SORT_CANONICAL,
    SORT_FUNCTION,
    SORT_SPECIALIZATION,
    SORT_DEPENDENCY,
    SORT_PRODUCER,
    SORT_VARIANT
} sort_kind;

typedef struct {
    value *val;
    char *key;
    size_t length;
    sort_kind kind;
} sort_item;

static int bytes_compare(const char *a, size_t na, const char *b, size_t nb) {
    int cmp = memcmp(a, b, na < nb ? na : nb);
    return cmp ? cmp : (na > nb) - (na < nb);
}

static int compare(const void *left, const void *right) {
    const sort_item *a = left, *b = right;
    if (a->kind == SORT_TEXT)
        return text_compare(a->val, b->val);
    if (a->kind == SORT_FUNCTION || a->kind == SORT_SPECIALIZATION) {
        const char *key = a->kind == SORT_FUNCTION ? "functionId" : "specializationId";
        int64_t x = number(get(a->val, key)), y = number(get(b->val, key));
        return (x > y) - (x < y);
    }
    if (a->kind == SORT_DEPENDENCY || a->kind == SORT_PRODUCER) {
        const char *fields[] = {"name", "version", "buildId"};
        size_t n = a->kind == SORT_PRODUCER ? 3 : 2;
        for (size_t i = 0; i < n; i++) {
            int cmp = text_compare(get(a->val, fields[i]), get(b->val, fields[i]));
            if (cmp)
                return cmp;
        }
        return 0;
    }
    int cmp = bytes_compare(a->key, a->length, b->key, b->length);
    if (cmp || a->kind != SORT_VARIANT)
        return cmp;
    int64_t x = number(get(a->val, "goatAbiVersion")), y = number(get(b->val, "goatAbiVersion"));
    if (x != y)
        return (x > y) - (x < y);
    cmp = text_compare(get(a->val, "hash"), get(b->val, "hash"));
    /* Equal semantic sort keys still need deterministic serialization of aliases. */
    return cmp ? cmp : text_compare(get(a->val, "library"), get(b->val, "library"));
}

static void sort(context *ctx, value *array, sort_kind kind, bool unique) {
    size_t n = yyjson_mut_arr_size(array);
    if (ctx->error || !n)
        return;
    if (n > SIZE_MAX / sizeof(sort_item)) {
        ctx->error = GOAT_MODULE_JSON_LIMIT;
        return;
    }
    sort_item *items = calloc(n, sizeof(*items));
    if (!allocated(ctx, items != NULL))
        return;
    size_t i, max;
    value *v;
    yyjson_mut_arr_foreach(array, i, max, v) {
        items[i].val = v;
        items[i].kind = kind;
        if (kind == SORT_CANONICAL || kind == SORT_VARIANT) {
            yyjson_doc *doc =
                yyjson_mut_val_imut_copy(kind == SORT_VARIANT ? get(v, "target") : v, NULL);
            if (!allocated(ctx, doc != NULL))
                break;
            ctx->error =
                goat_json_canonical(yyjson_doc_get_root(doc), &items[i].key, &items[i].length);
            yyjson_doc_free(doc);
            if (ctx->error)
                break;
        }
    }
    if (!ctx->error) {
        qsort(items, n, sizeof(*items), compare);
        if (unique)
            for (i = 1; i < n; i++)
                require(ctx, compare(items + i - 1, items + i) != 0);
        if (!ctx->error) {
            yyjson_mut_arr_clear(array);
            for (i = 0; i < n; i++)
                yyjson_mut_arr_append(array, items[i].val);
        }
    }
    for (i = 0; i < n; i++)
        free(items[i].key);
    free(items);
}

static void analysis(context *ctx, value *function) {
    value *a = get(function, "analysis");
    if (!a)
        return;
    value *summaries = get(a, "summaries");
    size_t i, max;
    value *s;
    yyjson_mut_arr_foreach(summaries, i, max, s) {
        value *when = get(s, "when"), *args = get(when, "arguments");
        value *result = get(s, "result"), *effects = get(s, "effects");
        if (!require(ctx, !result || !get(s, "normalReturn")))
            return;
        if (args) {
            if (!require(ctx,
                         yyjson_mut_arr_size(args)
                             == yyjson_mut_arr_size(get(function, "parameters"))))
                return;
            bool all_empty = true;
            size_t j, count;
            value *arg;
            yyjson_mut_arr_foreach(args, j, count, arg) {
                facts(ctx, arg);
                all_empty &= yyjson_mut_obj_size(arg) == 0;
            }
            if (ctx->error)
                return;
            if (all_empty)
                yyjson_mut_obj_remove_key(when, "arguments");
        }
        if (result) {
            facts(ctx, result);
            if (ctx->error)
                return;
            if (!yyjson_mut_obj_size(result))
                yyjson_mut_obj_remove_key(s, "result");
        }
        if (effects) {
            value *may = get(effects, "may");
            sort(ctx, may, SORT_TEXT, true);
            if (!yyjson_mut_arr_size(may) && yyjson_mut_get_bool(get(effects, "unknown")))
                yyjson_mut_obj_remove_key(s, "effects");
        }
    }
    sort(ctx, summaries, SORT_CANONICAL, true); /* Duplicates BEFORE removing empty claims. */
    if (ctx->error)
        return;
    for (i = yyjson_mut_arr_size(summaries); i > 0; i--)
        if (yyjson_mut_obj_size(yyjson_mut_arr_get(summaries, i - 1)) == 1)
            yyjson_mut_arr_remove(summaries, i - 1);
    if (!yyjson_mut_arr_size(summaries))
        yyjson_mut_obj_remove_key(function, "analysis");
}

static value *find_function(value *functions, int64_t id) {
    size_t i, max;
    value *f;
    yyjson_mut_arr_foreach(functions, i, max, f) if (number(get(f, "functionId")) == id) return f;
    return NULL;
}

static void externals(context *ctx, value *root, value *functions) {
    yyjson_mut_obj_iter it = yyjson_mut_obj_iter_with(get(root, "externals"));
    value *key;
    while ((key = yyjson_mut_obj_iter_next(&it))) {
        value *e = yyjson_mut_obj_iter_get_val(key), *type = get(e, "type"), *v = get(e, "value");
        value *id = get(e, "functionId"), *ref = get(e, "functionRef");
        if (!require(ctx, !yyjson_mut_get_bool(get(e, "mutable")) || !(type || v || id || ref))
            || !require(ctx, !(id && ref))
            || !require(ctx, !(id || ref) || !type || is(type, "function")))
            return;
        if (id && !require(ctx, find_function(functions, number(id)) != NULL))
            return;
        if (v && !scalar(ctx, type, v))
            return;
        if (is(type, "null"))
            yyjson_mut_obj_remove_key(e, "value");
    }
}

static void normalize_target(context *ctx, value *target) {
    bool windows = is(get(target, "os"), "windows");
    value *runtime = get(target, "runtime"), *os_version = get(target, "minimumOs");
    value *runtime_version = get(runtime, "minimumVersion");
    value *family = get(runtime, "family");
    if (!require(ctx,
                 number(get(target, "bits")) == (is(get(target, "architecture"), "x86") ? 32 : 64))
        || !require(ctx,
                    windows ? is(family, "msvcrt") || is(family, "ucrt")
                            : is(family, "glibc") || is(family, "musl"))
        || !require(ctx, !windows || number(yyjson_mut_arr_get_first(os_version)) >= 10))
        return;
    value *versions[] = {os_version, runtime_version};
    for (size_t i = 0; i < 2; i++)
        while (yyjson_mut_arr_size(versions[i]) > 1
               && !number(yyjson_mut_arr_get_last(versions[i])))
            yyjson_mut_arr_remove_last(versions[i]);
    sort(ctx, get(target, "cpuFeatures"), SORT_TEXT, true);
}

static void native(context *ctx, value *variants, value *functions) {
    size_t i, max;
    value *variant;
    yyjson_mut_arr_foreach(variants, i, max, variant) {
        for (size_t j = 0; j < i; j++)
            if (!require(ctx,
                         text_compare(get(variant, "library"),
                                      get(yyjson_mut_arr_get(variants, j), "library"))
                             != 0))
                return;
        normalize_target(ctx, get(variant, "target"));
        value *specs = get(variant, "specializations");
        size_t j, count;
        value *spec;
        yyjson_mut_arr_foreach(specs, j, count, spec) {
            value *f = find_function(functions, number(get(spec, "functionId")));
            value *signature = get(spec, "signature"), *guard = get(spec, "when");
            value *args = get(signature, "arguments"), *types = get(guard, "argumentTypes");
            if (!require(ctx,
                         f && yyjson_mut_arr_size(args) == yyjson_mut_arr_size(get(f, "parameters"))
                             && yyjson_mut_arr_size(args) == yyjson_mut_arr_size(types)))
                return;
            for (size_t k = 0; k < yyjson_mut_arr_size(args); k++)
                if (!require(
                        ctx,
                        !text_compare(yyjson_mut_arr_get(args, k), yyjson_mut_arr_get(types, k))))
                    return;
            value *conditions = get(guard, "conditions");
            size_t k, length;
            value *predicate;
            yyjson_mut_arr_foreach(conditions, k, length, predicate) {
                int64_t index = number(get(predicate, "argument"));
                value *constant = get(predicate, "constant"), *type = get(constant, "type");
                if (!require(ctx, (uint64_t)index < yyjson_mut_arr_size(args)))
                    return;
                if (!require(ctx, !text_compare(type, yyjson_mut_arr_get(args, (size_t)index)))
                    || !scalar(ctx, type, get(constant, "value")))
                    return;
                value *op = get(predicate, "operation");
                if (!require(ctx,
                             is(op, "equal") || is(op, "notEqual") || is(type, "integer")
                                 || is(type, "real") || is(type, "character")))
                    return;
            }
            sort(ctx, conditions, SORT_CANONICAL, true);
            if (conditions && !yyjson_mut_arr_size(conditions))
                yyjson_mut_obj_remove_key(guard, "conditions");
        }
        sort(ctx, specs, SORT_SPECIALIZATION, true);
    }
    sort(ctx, variants, SORT_VARIANT, false);
}

goat_module_json_error goat_module_json_normalize(yyjson_mut_doc *doc, goat_module_json_kind kind) {
    context ctx = {.doc = doc};
    if (kind == GOAT_MODULE_GMOD)
        return GOAT_MODULE_JSON_OK;
    value *root = yyjson_mut_doc_get_root(doc), *functions = get(root, "functions");
    sort(&ctx, get(root, "dependencies"), SORT_DEPENDENCY, true);
    sort(&ctx, get(root, "producers"), SORT_PRODUCER, true);
    sort(&ctx, functions, SORT_FUNCTION, true);
    size_t i, max;
    value *f;
    yyjson_mut_arr_foreach(functions, i, max, f) analysis(&ctx, f);
    if (ctx.error)
        return ctx.error;
    externals(&ctx, root, functions);
    if (!ctx.error)
        native(&ctx, get(root, "native"), functions);
    return ctx.error;
}
