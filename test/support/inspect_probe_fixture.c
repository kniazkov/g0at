/** @file inspect_probe_fixture.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent Python-equivalent fixtures; no production hash/ZIP oracle.
 */
#include "test/support/inspect_probe_fixture.h"

#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/format_binary.h"
#include "test/support/format_zip.h"
#include "test/support/language_execution.h"
#include "yyjson.h"

#include <stdlib.h>
#include <string.h>
#define REQUIRE(x) scenario_require(r, (x), #x)

struct ip_fixture_t {
    scenario_run_t *run;
    const char *builder, *compiler, *source, *root;
    yyjson_mut_doc *schema;
};

static bool free_malloc(void *p) {
    free(p);
    return true;
}

static bool free_mut(void *p) {
    yyjson_mut_doc_free(p);
    return true;
}

static void *owned(scenario_run_t *r, void *p) {
    REQUIRE(p);
    scenario_defer(r, free_malloc, p);
    return p;
}

static char *copy(scenario_run_t *r, const void *p, size_t size) {
    char *s = owned(r, malloc(size + 1));
    memcpy(s, p, size);
    s[size] = 0;
    return s;
}

static yyjson_mut_doc *parse(scenario_run_t *r, const void *p, size_t size) {
    char error[256];
    yyjson_mut_doc *d = format_json_parse(p, size, error, sizeof(error));
    REQUIRE(d);
    scenario_defer(r, free_mut, d);
    return d;
}

static const char *str(scenario_run_t *r, yyjson_mut_val *v, const char *key) {
    yyjson_mut_val *x = yyjson_mut_obj_get(v, key);
    REQUIRE(yyjson_mut_is_str(x) && strlen(yyjson_mut_get_str(x)) == yyjson_mut_get_len(x));
    return yyjson_mut_get_str(x);
}

static yyjson_mut_val *root(ip_archive_t *a) {
    return yyjson_mut_doc_get_root(a->doc);
}

static yyjson_mut_val *variant(scenario_run_t *r, ip_archive_t *a) {
    yyjson_mut_val *v = yyjson_mut_arr_get(yyjson_mut_obj_get(root(a), "native"), 0);
    REQUIRE(v);
    return v;
}

ip_part_t *ip_part(ip_fixture_t *f, ip_archive_t *a, const char *name) {
    scenario_run_t *r = f->run;
    for (size_t i = 0; i < a->count; i++)
        if (!strcmp(a->parts[i].name, name))
            return a->parts + i;
    REQUIRE(false);
    return NULL;
}

void ip_put(ip_fixture_t *f, ip_archive_t *a, const char *name, const void *bytes, size_t size) {
    scenario_run_t *r = f->run;
    ip_part_t *p = NULL;
    for (size_t i = 0; i < a->count; i++)
        if (!strcmp(a->parts[i].name, name))
            p = a->parts + i;
    if (!p) {
        REQUIRE(a->count < a->capacity);
        p = a->parts + a->count++;
        p->name = copy(r, name, strlen(name));
    }
    p->data = bytes;
    p->size = size;
}

ip_archive_t *ip_read(ip_fixture_t *f, const char *path) {
    scenario_run_t *r = f->run;
    size_t size;
    const unsigned char *bytes = (const unsigned char *)language_read(r, path, &size);
    char error[256];
    format_zip_info_t info;
    REQUIRE(format_zip_inspect(bytes, size, SIZE_MAX, &info, error, sizeof(error)));
    size_t count = info.count;
    ip_archive_t *a = owned(r, calloc(1, sizeof(*a)));
    a->capacity = count + 16;
    a->parts = owned(r, calloc(a->capacity, sizeof(*a->parts)));
    for (size_t i = 0; i < count; i++) {
        REQUIRE(format_zip_inspect(bytes, size, i, &info, error, sizeof(error)));
        REQUIRE(!memchr(info.name, 0, info.name_size));
        char *name = copy(r, info.name, info.name_size);
        size_t n;
        unsigned char *data =
            owned(r,
                  format_zip_extract(bytes, size, i, 32 * 1024 * 1024, &n, error, sizeof(error)));
        if (!strcmp(name, "module.json")) {
            REQUIRE(!a->doc);
            a->doc = parse(r, data, n);
        } else
            ip_put(f, a, name, data, n);
    }
    REQUIRE(a->doc);
    return a;
}

static int names_order(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

const char *ip_save(ip_fixture_t *f, const char *path, ip_archive_t *a) {
    const bool rehash = true;
    scenario_run_t *r = f->run;
    yyjson_mut_val *native = yyjson_mut_obj_get(root(a), "native");
    size_t count = yyjson_mut_arr_size(native);
    char hash[65], error[256];
    if (rehash) {
        for (size_t i = 0; i < count; i++) {
            yyjson_mut_val *v = yyjson_mut_arr_get(native, i);
            ip_part_t *p = ip_part(f, a, str(r, v, "library"));
            format_binary_sha256(p->data, p->size, hash);
            REQUIRE(yyjson_mut_obj_put(v,
                                       yyjson_mut_str(a->doc, "hash"),
                                       yyjson_mut_strcpy(a->doc, hash)));
        }
        ip_part_t *code = ip_part(f, a, "bytecode.bin");
        REQUIRE(format_binary_main_hash(root(a),
                                        yyjson_mut_doc_get_root(f->schema),
                                        code->data,
                                        code->size,
                                        hash,
                                        error,
                                        sizeof(error)));
        REQUIRE(yyjson_mut_obj_put(root(a),
                                   yyjson_mut_str(a->doc, "mainHash"),
                                   yyjson_mut_strcpy(a->doc, hash)));
    }
    size_t json_size;
    char *json = owned(r, format_json_canonical(root(a), &json_size));
    size_t n = 0;
    const char **names = owned(r, calloc(a->count + count + 1, sizeof(*names)));
    if (rehash) {
        names[n++] = "bytecode.bin";
        for (size_t i = 0; i < count; i++) {
            const char *name = str(r, yyjson_mut_arr_get(native, i), "library");
            bool found = false;
            for (size_t j = 0; j < n; j++)
                found |= !strcmp(name, names[j]);
            if (!found)
                names[n++] = name;
        }
        qsort(names, n, sizeof(*names), names_order);
    } else
        for (size_t i = 0; i < a->count; i++)
            names[n++] = a->parts[i].name;
    format_zip_entry_t *entries = owned(r, calloc(n + 1, sizeof(*entries)));
    format_zip_entry_init(entries, "module.json", json, json_size, false);
    for (size_t i = 0; i < n; i++) {
        ip_part_t *p = ip_part(f, a, names[i]);
        format_zip_entry_init(entries + i + 1, p->name, p->data, p->size, false);
    }
    size_t size;
    unsigned char *data =
        owned(r, format_zip_build(entries, n + 1, NULL, 0, false, &size, error, sizeof(error)));
    language_write(r, path, data, size);
    return path;
}

ip_fixture_t *ip_create(scenario_run_t *r,
                        const char *builder,
                        const char *compiler,
                        const char *source,
                        const char *schema_path) {
    ip_fixture_t *f = owned(r, calloc(1, sizeof(*f)));
    f->run = r;
    f->builder = builder;
    f->compiler = compiler;
    f->source = source;
    f->root = scenario_work_directory(scenario_run_context(r));
    size_t size;
    const char *bytes = language_read(r, schema_path, &size);
    f->schema = parse(r, bytes, size);
    return f;
}

void ip_normalize(ip_fixture_t *f, ip_archive_t *a) {
    scenario_run_t *r = f->run;
    char error[256];
    REQUIRE(format_metadata_normalize(a->doc,
                                      root(a),
                                      "module",
                                      yyjson_mut_doc_get_root(f->schema),
                                      error,
                                      sizeof(error)));
}

static const char *
named(scenario_run_t *r, const char *directory, const char *name, const char *suffix) {
    size_t size = strlen(name) + strlen(suffix) + 1;
    char *filename = owned(r, malloc(size));
    snprintf(filename, size, "%s%s", name, suffix);
    return language_join(r, directory, filename);
}

const char *ip_build(ip_fixture_t *f, const char *name) {
    scenario_run_t *r = f->run;
    const char *directory = language_join(r, f->root, name);
    language_directory(r, directory);
    size_t source_size;
    const char *source =
        language_read(r,
                      language_join(r,
                                    scenario_data_directory(scenario_run_context(r)),
                                    "module-inspect-probe/main.goat"),
                      &source_size);
    language_write(r, language_join(r, directory, "main.goat"), source, source_size);
    REQUIRE(!strchr(name, '"') && !strchr(name, '\\'));
    size_t capacity = strlen(name) + 160;
    char *json = owned(r, malloc(capacity));
    int length = snprintf(json,
                          capacity,
                          "{\"formatVersion\": 1, \"name\": \"%s\", \"version\": \"1\", \"main\": "
                          "\"main.goat\", \"native\": {\"mode\": \"required\"}}",
                          name);
    REQUIRE(length >= 0 && (size_t)length < capacity);
    const char *descriptor = language_join(r, directory, "module.gmod"),
               *output = language_join(r, directory, "module.gbin");
    language_write(r, descriptor, json, (size_t)length);
    const char *args[] = {f->builder,
                          "native-build",
                          descriptor,
                          output,
                          "descriptor",
                          "descriptor",
                          f->compiler,
                          NULL};
    scenario_command_result_t result = language_command(r, f->builder, args, NULL);
    REQUIRE(result.exit_code == 0);
    return output;
}

void ip_compile(ip_fixture_t *f, const char *source, const char *output, bool shared) {
    scenario_run_t *r = f->run;
    const char *args[14];
    size_t n = 0;
    args[n++] = f->compiler;
#ifndef _WIN32
    if (shared)
        args[n++] = "-fPIC";
#endif
    if (shared)
        args[n++] = "-shared";
    args[n++] = "-std=c11";
    args[n++] = "-O0";
    args[n++] = "-I";
    args[n++] = language_join(r, f->source, "include");
    args[n++] = source;
    args[n++] = "-o";
    args[n++] = output;
    args[n] = NULL;
    scenario_command_result_t result = language_command(r, f->compiler, args, NULL);
    REQUIRE(result.exit_code == 0);
}

static void
append(scenario_run_t *r, char *buffer, size_t capacity, size_t *used, const char *text) {
    size_t size = strlen(text);
    REQUIRE(*used < capacity && size < capacity - *used);
    memcpy(buffer + *used, text, size + 1);
    *used += size;
}

const char *ip_custom(ip_fixture_t *f, const char *original, const char *kind) {
    scenario_run_t *r = f->run;
    ip_archive_t *a = ip_read(f, original);
    yyjson_mut_val *v = variant(r, a);
    yyjson_mut_doc *description = yyjson_mut_doc_new(NULL);
    REQUIRE(description);
    scenario_defer(r, free_mut, description);
    yyjson_mut_val *object = yyjson_mut_obj(description);
    REQUIRE(object);
    yyjson_mut_doc_set_root(description, object);
    REQUIRE(yyjson_mut_obj_add_uint(description, object, "formatVersion", 1)
            && yyjson_mut_obj_add_uint(description, object, "goatAbiVersion", 1)
            && yyjson_mut_obj_add_str(description, object, "module", str(r, root(a), "name"))
            && yyjson_mut_obj_add_str(description, object, "version", str(r, root(a), "version"))
            && yyjson_mut_obj_add_str(
                description,
                object,
                "mainHash",
                !strcmp(kind, "descriptor-mismatch")
                    ? "0000000000000000000000000000000000000000000000000000000000000000"
                    : str(r, root(a), "mainHash")));
    const char *keys[] = {"target", "specializations"};
    for (size_t i = 0; i < 2; i++) {
        yyjson_mut_val *value =
            yyjson_mut_val_mut_copy(description, yyjson_mut_obj_get(v, keys[i]));
        REQUIRE(value && yyjson_mut_obj_add_val(description, object, keys[i], value));
    }
    size_t json_size;
    char *json = owned(r, format_json_canonical(object, &json_size));
    REQUIRE(json_size <= SIZE_MAX - 17);
    size_t blob_size = !strcmp(kind, "descriptor-overflow") ? 17 : json_size + 17;
    unsigned char *blob = owned(r, calloc(blob_size, 1));
    memcpy(blob, "GOATND01", 8);
    uint64_t length = !strcmp(kind, "descriptor-overflow") ? UINT64_MAX : json_size;
    for (unsigned i = 0; i < 8; i++)
        blob[8 + i] = (unsigned char)(length >> (8 * i));
    if (blob_size > 17)
        memcpy(blob + 16, json, json_size);
    yyjson_mut_val *specializations = yyjson_mut_obj_get(v, "specializations");
    size_t count = yyjson_mut_arr_size(specializations);
    REQUIRE(blob_size < (SIZE_MAX - 4096) / 5);
    size_t capacity = blob_size * 5 + strlen(f->source) + 4096;
    for (size_t i = 0; i < count; i++) {
        size_t extra = strlen(str(r, yyjson_mut_arr_get(specializations, i), "symbol")) + 512;
        REQUIRE(extra <= SIZE_MAX - capacity);
        capacity += extra;
    }
    char *text = owned(r, calloc(capacity, 1));
    size_t used = 0;
    append(r, text, capacity, &used, "#include \"goat/module_abi.h\"\n#include \"");
    const char *library_source =
        language_join(r, f->source, "test/src/component/inspect_probe_library.c");
    char *portable = copy(r, library_source, strlen(library_source));
    for (char *p = portable; *p; p++)
        if (*p == '\\')
            *p = '/';
    append(r, text, capacity, &used, portable);
    append(r, text, capacity, &used, "\"");
    if (strcmp(kind, "descriptor-missing")) {
        append(r,
               text,
               capacity,
               &used,
               "\nGOAT_MODULE_EXPORT const unsigned char goat_module_descriptor_v1[]={");
        for (size_t i = 0; i < blob_size; i++) {
            char number[8];
            snprintf(number, sizeof(number), "%s%u", i ? "," : "", (unsigned)blob[i]);
            append(r, text, capacity, &used, number);
        }
        append(r, text, capacity, &used, "};");
    }
    for (size_t i = 0; i < count; i++) {
        if (!strcmp(kind, "symbol-missing") && i == count - 1)
            continue;
        append(r, text, capacity, &used, "\nGOAT_MODULE_EXPORT uint32_t GOAT_MODULE_CALL ");
        append(r, text, capacity, &used, str(r, yyjson_mut_arr_get(specializations, i), "symbol"));
        append(r,
               text,
               capacity,
               &used,
               "(uint32_t abi,const goat_module_context_v1*c,uint32_t n,"
               "const goat_module_value_v1*a,goat_module_value_v1*r){"
               "(void)abi;(void)c;(void)n;(void)a;(void)r;mark(\"called\");abort();return 0;}");
    }
    size_t name_size = strlen(kind) + strlen(str(r, root(a), "name")) + 2;
    char *name = owned(r, malloc(name_size));
    snprintf(name, name_size, "%s-%s", kind, str(r, root(a), "name"));
    const char *source = named(r, f->root, name, ".c");
#ifdef _WIN32
    const char *library = named(r, f->root, name, ".dll");
#else
    const char *library = named(r, f->root, name, ".so");
#endif
    language_write(r, source, text, used);
    ip_compile(f, source, library, true);
    size_t size;
    const char *payload = language_read(r, library, &size);
    ip_put(f, a, str(r, v, "library"), payload, size);
    return ip_save(f, named(r, f->root, name, ".gbin"), a);
}
