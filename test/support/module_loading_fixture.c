/** @file module_loading_fixture.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Real compiler fixtures and independent archive mutation for loading (#369).
 */
#include "test/support/module_loading_fixture.h"

#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/format_binary.h"
#include "test/support/format_zip.h"
#include "test/support/language_execution.h"
#include "test/support/module_execution.h"
#include "yyjson.h"

#include <stdlib.h>
#include <string.h>
#define REQUIRE(x) scenario_require(r, (x), #x)

typedef struct {
    const char *name;
    const void *bytes;
    size_t size;
} part_t;

typedef struct {
    yyjson_mut_doc *doc;
    part_t *parts;
    size_t count, capacity;
} archive_t;

struct loading_fixture_t {
    scenario_run_t *run;
    const char *builder, *compiler, *include, *root, *base;
    yyjson_mut_doc *schema;
    module_execution_suite_t *suite;
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

static yyjson_mut_val *root(archive_t *a) {
    return yyjson_mut_doc_get_root(a->doc);
}

static yyjson_mut_val *variant(scenario_run_t *r, archive_t *a) {
    yyjson_mut_val *v = yyjson_mut_arr_get(yyjson_mut_obj_get(root(a), "native"), 0);
    REQUIRE(v);
    return v;
}

static part_t *part(scenario_run_t *r, archive_t *a, const char *name) {
    for (size_t i = 0; i < a->count; i++)
        if (!strcmp(a->parts[i].name, name))
            return a->parts + i;
    REQUIRE(false);
    return NULL;
}

static void put(scenario_run_t *r, archive_t *a, const char *name, const void *bytes, size_t size) {
    part_t *p = NULL;
    for (size_t i = 0; i < a->count; i++)
        if (!strcmp(a->parts[i].name, name))
            p = a->parts + i;
    if (!p) {
        REQUIRE(a->count < a->capacity);
        p = a->parts + a->count++;
        p->name = copy(r, name, strlen(name));
    }
    p->bytes = bytes;
    p->size = size;
}

static archive_t *read_archive(loading_fixture_t *f, const char *path) {
    scenario_run_t *r = f->run;
    size_t size;
    const unsigned char *bytes = (const unsigned char *)language_read(r, path, &size);
    char error[256];
    format_zip_info_t info;
    REQUIRE(format_zip_inspect(bytes, size, SIZE_MAX, &info, error, sizeof(error)));
    size_t count = info.count;
    archive_t *a = owned(r, calloc(1, sizeof(*a)));
    a->capacity = count + 4;
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
            put(r, a, name, data, n);
    }
    REQUIRE(a->doc);
    return a;
}

static int names_order(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static void save_archive(loading_fixture_t *f, const char *path, archive_t *a, bool rehash) {
    scenario_run_t *r = f->run;
    yyjson_mut_val *native = yyjson_mut_obj_get(root(a), "native");
    size_t count = yyjson_mut_arr_size(native);
    char hash[65], error[256];
    if (rehash) {
        for (size_t i = 0; i < count; i++) {
            yyjson_mut_val *v = yyjson_mut_arr_get(native, i);
            part_t *p = part(r, a, str(r, v, "library"));
            format_binary_sha256(p->bytes, p->size, hash);
            REQUIRE(yyjson_mut_obj_put(v,
                                       yyjson_mut_str(a->doc, "hash"),
                                       yyjson_mut_strcpy(a->doc, hash)));
        }
        part_t *code = part(r, a, "bytecode.bin");
        REQUIRE(format_binary_main_hash(root(a),
                                        yyjson_mut_doc_get_root(f->schema),
                                        code->bytes,
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
        part_t *p = part(r, a, names[i]);
        format_zip_entry_init(entries + i + 1, p->name, p->bytes, p->size, false);
    }
    size_t size;
    unsigned char *data =
        owned(r, format_zip_build(entries, n + 1, NULL, 0, false, &size, error, sizeof(error)));
    language_write(r, path, data, size);
}

loading_fixture_t *loading_fixture_create(scenario_run_t *r,
                                          const char *builder,
                                          const char *compiler,
                                          const char *include,
                                          const char *schema) {
    loading_fixture_t *f = owned(r, calloc(1, sizeof(*f)));
    f->run = r;
    f->builder = builder;
    f->compiler = compiler;
    f->include = include;
    f->root = scenario_work_directory(scenario_run_context(r));
    size_t size;
    const char *bytes = language_read(r, schema, &size);
    f->schema = parse(r, bytes, size);
    f->suite = module_execution_suite(r, builder);
    return f;
}

const char *loading_fixture_root(loading_fixture_t *f) {
    return f->root;
}

const char *loading_build(loading_fixture_t *f,
                          const char *name,
                          const char *source,
                          const char *mode,
                          const char *dependency) {
    scenario_run_t *r = f->run;
    module_execution_project_t *p = module_execution_project(f->suite, name);
    module_execution_put(p, "main.goat", source);
    if (dependency) {
        archive_t *dep = read_archive(f, dependency);
        const char *depname = str(r, root(dep), "name"), *version = str(r, root(dep), "version");
        const char *directory = language_join(r, module_execution_modules(p), depname);
        language_directory(r, directory);
        size_t len = strlen(version) + 6;
        char *filename = owned(r, malloc(len));
        snprintf(filename, len, "%s.gbin", version);
        size_t size;
        const char *bytes = language_read(r, dependency, &size);
        language_write(r, language_join(r, directory, filename), bytes, size);
    }
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    REQUIRE(doc);
    scenario_defer(r, free_mut, doc);
    yyjson_mut_val *object = yyjson_mut_obj(doc), *native = yyjson_mut_obj(doc);
    REQUIRE(object && native);
    yyjson_mut_doc_set_root(doc, object);
    REQUIRE(yyjson_mut_obj_add_uint(doc, object, "formatVersion", 1)
            && yyjson_mut_obj_add_str(doc, object, "name", name)
            && yyjson_mut_obj_add_str(doc, object, "version", "1")
            && yyjson_mut_obj_add_str(doc, object, "main", "main.goat")
            && yyjson_mut_obj_add_str(doc, native, "mode", mode)
            && yyjson_mut_obj_add_val(doc, object, "native", native));
    size_t length;
    char *text = owned(r, yyjson_mut_write(doc, 0, &length));
    const char *descriptor = module_execution_put(p, "module.gmod", text),
               *archive = language_join(r, module_execution_root(p), "module.gbin");
    const char *args[] = {f->builder,
                          "native-build",
                          descriptor,
                          archive,
                          "descriptor",
                          "descriptor",
                          f->compiler,
                          NULL};
    scenario_command_result_t result = language_command(r, f->builder, args, NULL);
    REQUIRE(result.exit_code == 0 && !result.out_size);
    return archive;
}

const char *loading_base(loading_fixture_t *f) {
    if (!f->base)
        f->base = loading_build(f,
                                "base",
                                "external f=func(x){return x*x;}; println(f(7)); println(f(8));",
                                "required",
                                NULL);
    return f->base;
}

static const char *
named(scenario_run_t *r, const char *root, const char *name, const char *suffix) {
    size_t n = strlen(name) + strlen(suffix) + 1;
    char *path = owned(r, malloc(n));
    snprintf(path, n, "%s%s", name, suffix);
    return language_join(r, root, path);
}

const char *loading_custom(loading_fixture_t *f,
                           const char *name,
                           const char *original,
                           const char *kind,
                           bool nonmatching) {
    scenario_run_t *r = f->run;
    archive_t *a = read_archive(f, original);
    yyjson_mut_val *v = variant(r, a), *specs = yyjson_mut_obj_get(v, "specializations");
    REQUIRE(yyjson_mut_is_arr(specs));
    if (nonmatching)
        for (size_t i = 0; i < yyjson_mut_arr_size(specs); i++) {
            yyjson_mut_val *spec = yyjson_mut_arr_get(specs, i), *args = yyjson_mut_arr(a->doc),
                           *types = yyjson_mut_arr(a->doc), *when = yyjson_mut_obj(a->doc);
            REQUIRE(args && types && when && yyjson_mut_arr_add_str(a->doc, args, "real")
                    && yyjson_mut_arr_add_str(a->doc, types, "real")
                    && yyjson_mut_obj_add_val(a->doc, when, "argumentTypes", types));
            REQUIRE(yyjson_mut_obj_put(yyjson_mut_obj_get(spec, "signature"),
                                       yyjson_mut_str(a->doc, "arguments"),
                                       args)
                    && yyjson_mut_obj_put(spec, yyjson_mut_str(a->doc, "when"), when));
        }
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    REQUIRE(doc);
    scenario_defer(r, free_mut, doc);
    yyjson_mut_val *description = yyjson_mut_obj(doc);
    REQUIRE(description);
    yyjson_mut_doc_set_root(doc, description);
    REQUIRE(yyjson_mut_obj_add_uint(doc, description, "formatVersion", 1)
            && yyjson_mut_obj_add_uint(doc, description, "goatAbiVersion", 1));
    const char *keys[] = {"module", "version", "mainHash", "target", "specializations"};
    yyjson_mut_val *values[] = {yyjson_mut_obj_get(root(a), "name"),
                                yyjson_mut_obj_get(root(a), "version"),
                                yyjson_mut_obj_get(root(a), "mainHash"),
                                yyjson_mut_obj_get(v, "target"),
                                specs};
    for (size_t i = 0; i < 5; i++)
        REQUIRE(values[i]
                && yyjson_mut_obj_add(description,
                                      yyjson_mut_str(doc, keys[i]),
                                      yyjson_mut_val_mut_copy(doc, values[i])));
    if (!strcmp(kind, "descriptor-mismatch"))
        REQUIRE(yyjson_mut_obj_put(
            description,
            yyjson_mut_str(doc, "mainHash"),
            yyjson_mut_str(doc,
                           "0000000000000000000000000000000000000000000000000000000000000000")));
    size_t encoded_size;
    char *encoded = owned(r, format_json_canonical(description, &encoded_size));
    REQUIRE(encoded_size < SIZE_MAX - 17);
    size_t blob_size = encoded_size + 17;
    unsigned char *blob = owned(r, calloc(1, blob_size));
    memcpy(blob, "GOATND01", 8);
    for (size_t i = 0; i < 8; i++)
        blob[8 + i] = (unsigned char)((uint64_t)encoded_size >> (i * 8));
    memcpy(blob + 16, encoded, encoded_size);
    if (!strcmp(kind, "descriptor-overflow")) {
        blob_size = 17;
        memset(blob + 8, 255, 8);
        blob[16] = 0;
    } else if (!strcmp(kind, "descriptor-short")) {
        blob_size = 1;
        blob[0] = 'X';
    }
    const char *source = named(r, f->root, name, ".c");
#ifdef _WIN32
    const char *library = named(r, f->root, name, ".dll");
#else
    const char *library = named(r, f->root, name, ".so");
#endif
    FILE *file = FOPEN(source, "wb");
    REQUIRE(file);
    bool ok = true;
    const char *prefix =
        "#include \"goat/module_abi.h\"\n#include <stdio.h>\n#include <stdlib.h>\n#include "
        "<stdatomic.h>\nstatic atomic_flag marker_lock = ATOMIC_FLAG_INIT;\nstatic void mark(char "
        "c) { while(atomic_flag_test_and_set_explicit(&marker_lock,memory_order_acquire)) {} const "
        "char *p=getenv(\"GOAT_LOADING_NATIVE_MARKER\"); if(p){FILE "
        "*f=fopen(p,\"ab\");if(!f)abort(); if(fputc(c,f)==EOF || fclose(f))abort();} "
        "atomic_flag_clear_explicit(&marker_lock,memory_order_release);}\n__attribute__(("
        "constructor)) static void loaded(void) { mark('L'); }";
    ok = fputs(prefix, file) >= 0;
    bool nonreturning = !strcmp(kind, "nonreturning-loop") || !strcmp(kind, "nonreturning-block");
    if (nonreturning)
        ok = ok
             && fputs("\n__attribute__((destructor)) static void unloaded(void) { mark('U'); }",
                      file)
                    >= 0;
    if (!strcmp(kind, "nonreturning-block"))
        ok = ok
             && fputs("\n#ifdef _WIN32\n#include <windows.h>\n#else\n#include <unistd.h>\n#endif",
                      file)
                    >= 0;
    if (strcmp(kind, "descriptor-missing")) {
        ok = ok
             && fprintf(file,
                        "\nGOAT_MODULE_EXPORT %sunsigned char goat_module_descriptor_v1[] = {",
                        !strcmp(kind, "descriptor-writable") ? "" : "const ")
                    >= 0;
        for (size_t i = 0; i < blob_size; i++)
            ok = ok && fprintf(file, "%s%u", i ? "," : "", (unsigned)blob[i]) >= 0;
        ok = ok && fputs("};", file) >= 0;
    }
    for (size_t i = 0; i < yyjson_mut_arr_size(specs); i++) {
        if (!strcmp(kind, "symbol-missing") && i + 1 == yyjson_mut_arr_size(specs))
            continue;
        const char *symbol = str(r, yyjson_mut_arr_get(specs, i), "symbol");
        for (size_t j = 0; symbol[j]; j++)
            REQUIRE((symbol[j] >= 'a' && symbol[j] <= 'z') || (symbol[j] >= 'A' && symbol[j] <= 'Z')
                    || symbol[j] == '_' || (j && symbol[j] >= '0' && symbol[j] <= '9'));
        ok = ok
             && fprintf(file,
                        "\nGOAT_MODULE_EXPORT uint32_t GOAT_MODULE_CALL %s(uint32_t abi,const "
                        "goat_module_context_v1*c,uint32_t n,const "
                        "goat_module_value_v1*a,goat_module_value_v1*r){(void)abi;(void)c;(void)n;"
                        "mark('C');\n",
                        symbol)
                    >= 0;
        const char *body =
            !strcmp(kind, "nonreturning-loop")
                ? "for(;;){atomic_signal_fence(memory_order_seq_cst);}}"
            : !strcmp(kind, "nonreturning-block")
                ? "\n#ifdef _WIN32\nSleep(INFINITE);\n#else\nfor(;;)pause();\n#endif\nreturn 0;}"
            : !strcmp(kind, "bad-status") ? "return GOAT_MODULE_RESOURCE_LIMIT;}"
            : !strcmp(kind, "bad-result") ? "r->tag=GOAT_MODULE_STRING;r->payload=1;return 0;}"
                                          : "r->tag=GOAT_MODULE_INTEGER;r->reserved=0;r->payload=a["
                                            "0].payload*a[0].payload;return 0;}";
        ok = ok && fputs(body, file) >= 0;
    }
    if (FCLOSE(file))
        ok = false;
    REQUIRE(ok);
    const char *args[12];
    size_t n = 0;
    args[n++] = f->compiler;
#ifndef _WIN32
    args[n++] = "-fPIC";
#endif
    args[n++] = "-std=c11";
    args[n++] = "-shared";
    args[n++] = "-O0";
    args[n++] = "-I";
    args[n++] = f->include;
    args[n++] = source;
    args[n++] = "-o";
    args[n++] = library;
    args[n] = NULL;
    scenario_command_result_t result = language_command(r, f->compiler, args, NULL);
    REQUIRE(result.exit_code == 0);
    size_t size;
    const char *bytes = language_read(r, library, &size);
    put(r, a, str(r, v, "library"), bytes, size);
    const char *path = named(r, f->root, name, ".gbin");
    save_archive(f, path, a, true);
    return path;
}

const char *loading_graft(loading_fixture_t *f, const char *name, const char *source) {
    scenario_run_t *r = f->run;
    archive_t *template = read_archive(f, loading_base(f));
    const char *path = loading_build(f, name, source, "off", NULL);
    archive_t *a = read_archive(f, path);
    yyjson_mut_val *native =
        yyjson_mut_val_mut_copy(a->doc, yyjson_mut_obj_get(root(template), "native"));
    REQUIRE(native && yyjson_mut_obj_put(root(a), yyjson_mut_str(a->doc, "native"), native));
    part_t *p = part(r, template, str(r, variant(r, template), "library"));
    put(r, a, p->name, p->bytes, p->size);
    save_archive(f, path, a, true);
    size_t length = strlen(name) + 9;
    char *library_name = owned(r, malloc(length));
    snprintf(library_name, length, "%s-library", name);
    const char *generated = loading_custom(f, library_name, path, "valid", false);
    size_t size;
    const char *bytes = language_read(r, generated, &size);
    language_write(r, path, bytes, size);
    return path;
}

const char *
loading_transform(loading_fixture_t *f, const char *name, const char *original, const char *kind) {
    scenario_run_t *r = f->run;
    archive_t *a = read_archive(f, original);
    bool rehash = true;
    if (!strcmp(kind, "plain")) {
        REQUIRE(
            yyjson_mut_obj_put(root(a), yyjson_mut_str(a->doc, "native"), yyjson_mut_arr(a->doc)));
    } else if (!strcmp(kind, "foreign") || !strcmp(kind, "foreign-damaged")) {
        yyjson_mut_val *minimum = yyjson_mut_arr(a->doc);
        REQUIRE(minimum && yyjson_mut_arr_add_uint(a->doc, minimum, 999)
                && yyjson_mut_arr_add_uint(a->doc, minimum, 0));
        REQUIRE(yyjson_mut_obj_put(yyjson_mut_obj_get(variant(r, a), "target"),
                                   yyjson_mut_str(a->doc, "minimumOs"),
                                   minimum));
        if (!strcmp(kind, "foreign-damaged")) {
            const char *bad = "changed without updating hash";
            put(r, a, str(r, variant(r, a), "library"), bad, strlen(bad));
            rehash = false;
        }
    } else if (!strcmp(kind, "ambiguous")) {
        yyjson_mut_val *v = variant(r, a), *alternate = yyjson_mut_val_mut_copy(a->doc, v);
        const char *old = str(r, v, "library"), *dot = strrchr(old, '.');
        REQUIRE(dot);
        size_t n = strlen(dot) + 17;
        char *library = owned(r, malloc(n));
        snprintf(library, n, "native/alternate%s", dot);
        REQUIRE(alternate
                && yyjson_mut_obj_put(alternate,
                                      yyjson_mut_str(a->doc, "library"),
                                      yyjson_mut_strcpy(a->doc, library))
                && yyjson_mut_arr_append(yyjson_mut_obj_get(root(a), "native"), alternate));
        part_t *p = part(r, a, old);
        put(r, a, library, p->bytes, p->size);
    } else if (!strcmp(kind, "damaged")) {
        const char *name = str(r, variant(r, a), "library");
        part_t *p = part(r, a, name);
        REQUIRE(p->size >= 9);
        char *data = copy(r, p->bytes, p->size);
        memcpy(data, "corrupted", 9);
        put(r, a, name, data, p->size);
        rehash = false;
    } else {
        REQUIRE(!strcmp(kind, "invalid-os") || !strcmp(kind, "invalid-shared-library"));
        const char *bad =
            !strcmp(kind, "invalid-os") ? "not an OS library" : "not a shared library";
        put(r, a, str(r, variant(r, a), "library"), bad, strlen(bad));
    }
    const char *path = named(r, f->root, name, ".gbin");
    save_archive(f, path, a, rehash);
    return path;
}
