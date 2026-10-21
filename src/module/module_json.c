#include "module_json.h"

#include <goat/module_identity.h>
#include <stdint.h>
#include <string.h>

static int allowed(const char *key, size_t len, const char *const *names) {
    for (; *names; names++) {
        if (strlen(*names) == len && memcmp(key, *names, len) == 0)
            return 1;
    }
    return 0;
}

static const char *const gmod_fields[] =
    {"formatVersion", "name", "version", "main", "dependencies", "native", NULL};
static const char *const metadata_fields[] = {"formatVersion",
                                              "name",
                                              "version",
                                              "mainHash",
                                              "dependencies",
                                              "native",
                                              "nativeVariants",
                                              "bytecodeVersion",
                                              "abiVersion",
                                              "description",
                                              "author",
                                              "authors",
                                              "license",
                                              "homepage",
                                              "repository",
                                              "documentation",
                                              "keywords",
                                              NULL};
static const char *const native_fields[] = {"mode", "target", NULL};

static int integer_one(const yyjson_val *val) {
    if (!yyjson_is_raw(val))
        return 0;
    const char *s = yyjson_get_raw(val);
    return yyjson_get_len(val) == 1 && s[0] == '1';
}

static int strict_integer(const yyjson_val *val) {
    if (!yyjson_is_raw(val))
        return 0;
    const char *s = yyjson_get_raw(val);
    size_t n = yyjson_get_len(val), i = 0;
    if (n && s[0] == '-')
        i++;
    if (i == n || (s[i] == '0' && i + 1 != n))
        return 0;
    uint64_t value = 0;
    uint64_t limit = i ? UINT64_C(9223372036854775808) : UINT64_MAX;
    for (; i < n; i++) {
        unsigned digit = (unsigned)(s[i] - '0');
        if (digit > 9 || value > (limit - digit) / 10)
            return 0;
        value = value * 10 + digit;
    }
    return 1;
}

static goat_module_json_error validate_duplicates(yyjson_val *v) {
    if (yyjson_is_obj(v)) {
        yyjson_obj_iter it = yyjson_obj_iter_with(v);
        yyjson_val *key;
        while ((key = yyjson_obj_iter_next(&it))) {
            yyjson_obj_iter prev = yyjson_obj_iter_with(v);
            yyjson_val *p;
            while ((p = yyjson_obj_iter_next(&prev)) != key) {
                if (yyjson_get_len(p) == yyjson_get_len(key)
                    && memcmp(yyjson_get_str(p), yyjson_get_str(key), yyjson_get_len(key)) == 0)
                    return GOAT_MODULE_JSON_DUPLICATE_KEY;
            }
            goat_module_json_error err = validate_duplicates(yyjson_obj_iter_get_val(key));
            if (err != GOAT_MODULE_JSON_OK)
                return err;
        }
    } else if (yyjson_is_arr(v)) {
        yyjson_arr_iter it = yyjson_arr_iter_with(v);
        yyjson_val *child;
        while ((child = yyjson_arr_iter_next(&it))) {
            goat_module_json_error err = validate_duplicates(child);
            if (err != GOAT_MODULE_JSON_OK)
                return err;
        }
    }
    return GOAT_MODULE_JSON_OK;
}

static goat_module_json_error validate_fields(yyjson_val *obj, const char *const *fields) {
    if (!yyjson_is_obj(obj))
        return GOAT_MODULE_JSON_INVALID_FIELD;
    yyjson_obj_iter it = yyjson_obj_iter_with(obj);
    yyjson_val *key;
    while ((key = yyjson_obj_iter_next(&it))) {
        if (!allowed(yyjson_get_str(key), yyjson_get_len(key), fields))
            return GOAT_MODULE_JSON_UNKNOWN_FIELD;
    }
    return GOAT_MODULE_JSON_OK;
}

goat_module_json_error goat_module_json_read(const char *data,
                                             size_t length,
                                             goat_module_json_kind kind,
                                             yyjson_doc **out) {
    if (!out)
        return GOAT_MODULE_JSON_INVALID_FIELD;
    *out = NULL;
    if (!data
        || length >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB
               && (unsigned char)data[2] == 0xBF)
        return GOAT_MODULE_JSON_SYNTAX;

    yyjson_doc *doc = yyjson_read_opts(data, length, YYJSON_READ_NUMBER_AS_RAW, NULL, NULL);
    if (!doc)
        return GOAT_MODULE_JSON_SYNTAX;
    yyjson_val *root = yyjson_doc_get_root(doc);
    goat_module_json_error err = validate_duplicates(root);
    if (err == GOAT_MODULE_JSON_OK) {
        err = validate_fields(root, kind == GOAT_MODULE_GMOD ? gmod_fields : metadata_fields);
    }
    if (err == GOAT_MODULE_JSON_OK) {
        yyjson_val *version = yyjson_obj_get(root, "formatVersion");
        if (!version || !strict_integer(version))
            err = GOAT_MODULE_JSON_INVALID_FIELD;
        else if (!integer_one(version))
            err = GOAT_MODULE_JSON_UNSUPPORTED_VERSION;
    }
    if (err == GOAT_MODULE_JSON_OK) {
        yyjson_val *name = yyjson_obj_get(root, "name");
        yyjson_val *module_version = yyjson_obj_get(root, "version");
        yyjson_val *deps = yyjson_obj_get(root, "dependencies");
        if ((name
             && (!yyjson_is_str(name)
                 || !goat_module_name_valid(yyjson_get_str(name), yyjson_get_len(name))))
            || (module_version
                && (!yyjson_is_str(module_version)
                    || !goat_module_version_valid(yyjson_get_str(module_version),
                                                  yyjson_get_len(module_version)))))
            err = GOAT_MODULE_JSON_INVALID_FIELD;
        if (err == GOAT_MODULE_JSON_OK && yyjson_is_obj(deps)) {
            yyjson_obj_iter it = yyjson_obj_iter_with(deps);
            yyjson_val *key;
            while ((key = yyjson_obj_iter_next(&it))) {
                yyjson_val *value = yyjson_obj_iter_get_val(key);
                if (!goat_module_name_valid(yyjson_get_str(key), yyjson_get_len(key))
                    || !yyjson_is_str(value)
                    || !goat_module_version_valid(yyjson_get_str(value), yyjson_get_len(value))) {
                    err = GOAT_MODULE_JSON_INVALID_FIELD;
                    break;
                }
            }
        }
    }
    if (err == GOAT_MODULE_JSON_OK && kind == GOAT_MODULE_GMOD) {
        yyjson_val *name = yyjson_obj_get(root, "name");
        yyjson_val *version = yyjson_obj_get(root, "version");
        yyjson_val *main = yyjson_obj_get(root, "main");
        yyjson_val *deps = yyjson_obj_get(root, "dependencies");
        yyjson_val *native = yyjson_obj_get(root, "native");
        if (!yyjson_is_str(name) || !yyjson_is_str(version) || !yyjson_is_str(main))
            err = GOAT_MODULE_JSON_INVALID_FIELD;
        if (err == GOAT_MODULE_JSON_OK && deps) {
            if (!yyjson_is_obj(deps))
                err = GOAT_MODULE_JSON_INVALID_FIELD;
            else {
                yyjson_obj_iter it = yyjson_obj_iter_with(deps);
                yyjson_val *key;
                while ((key = yyjson_obj_iter_next(&it))) {
                    if (!yyjson_is_str(yyjson_obj_iter_get_val(key))) {
                        err = GOAT_MODULE_JSON_INVALID_FIELD;
                        break;
                    }
                }
            }
        }
        if (err == GOAT_MODULE_JSON_OK && native) {
            err = validate_fields(native, native_fields);
            if (err == GOAT_MODULE_JSON_OK) {
                yyjson_val *mode = yyjson_obj_get(native, "mode");
                yyjson_val *target = yyjson_obj_get(native, "target");
                if ((mode
                     && (!yyjson_is_str(mode)
                         || (strcmp(yyjson_get_str(mode), "off")
                             && strcmp(yyjson_get_str(mode), "auto")
                             && strcmp(yyjson_get_str(mode), "required"))))
                    || (target && !yyjson_is_str(target)))
                    err = GOAT_MODULE_JSON_INVALID_FIELD;
            }
        }
    }
    if (err == GOAT_MODULE_JSON_OK)
        *out = doc;
    else
        yyjson_doc_free(doc);
    return err;
}

const char *goat_module_json_error_string(goat_module_json_error error) {
    switch (error) {
        case GOAT_MODULE_JSON_OK:
            return "ok";
        case GOAT_MODULE_JSON_SYNTAX:
            return "invalid JSON or UTF-8";
        case GOAT_MODULE_JSON_DUPLICATE_KEY:
            return "duplicate JSON key";
        case GOAT_MODULE_JSON_UNKNOWN_FIELD:
            return "unknown JSON field";
        case GOAT_MODULE_JSON_INVALID_FIELD:
            return "invalid JSON field";
        case GOAT_MODULE_JSON_UNSUPPORTED_VERSION:
            return "unsupported format version";
    }
    return "unknown error";
}
