/** @file module_library_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Real loaded descriptor, adapter identity and image ownership fixtures (#368).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "lib/strings/string_ext.h"
#include "test/support/format_json.h"
#include "test/support/format_zip.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"
#include "yyjson.h"

#include <stdlib.h>
#include <string.h>
#define REQUIRE(x) scenario_require(r, (x), #x)

typedef struct {
    const void *data;
    size_t size;
} bytes_t;

typedef struct {
    const char *builder, *loader, *compiler, *data, *name;
    bool success, elf;
} configuration_t;

static bool free_doc(void *p) {
    yyjson_doc_free(p);
    return true;
}

static bool free_mut(void *p) {
    yyjson_mut_doc_free(p);
    return true;
}

static bool free_malloc(void *p) {
    free(p);
    return true;
}

static void *owned(scenario_run_t *r, void *p) {
    REQUIRE(p);
    scenario_defer(r, free_malloc, p);
    return p;
}

static bytes_t read_bytes(scenario_run_t *r, const char *path) {
    size_t size;
    const void *p = language_read(r, path, &size);
    return (bytes_t){p, size};
}

static yyjson_val *parse(scenario_run_t *r, bytes_t bytes) {
    yyjson_doc *d = yyjson_read(bytes.data, bytes.size, 0);
    REQUIRE(d);
    scenario_defer(r, free_doc, d);
    return yyjson_doc_get_root(d);
}

static const char *field(scenario_run_t *r, yyjson_val *v, const char *key) {
    yyjson_val *x = yyjson_obj_get(v, key);
    REQUIRE(yyjson_is_str(x) && strlen(yyjson_get_str(x)) == yyjson_get_len(x));
    return yyjson_get_str(x);
}

static bytes_t part(scenario_run_t *r, bytes_t archive, const char *name) {
    char error[256];
    format_zip_info_t info;
    REQUIRE(format_zip_inspect(archive.data, archive.size, SIZE_MAX, &info, error, sizeof(error)));
    size_t count = info.count;
    for (size_t i = 0; i < count; i++) {
        REQUIRE(format_zip_inspect(archive.data, archive.size, i, &info, error, sizeof(error)));
        if (info.name_size == strlen(name) && !memcmp(info.name, name, info.name_size)) {
            size_t size;
            void *p = format_zip_extract(archive.data,
                                         archive.size,
                                         i,
                                         32 * 1024 * 1024,
                                         &size,
                                         error,
                                         sizeof(error));
            return (bytes_t){owned(r, p), size};
        }
    }
    REQUIRE(false);
    return (bytes_t){0};
}

static void command(scenario_run_t *r, const char *exe, const char *const *args) {
    scenario_command_result_t result = language_command(r, exe, args, NULL);
    REQUIRE(result.exit_code == 0);
}

static uint16_t u16(const unsigned char *p) {
    return (uint16_t)((unsigned)p[0] | (unsigned)p[1] << 8);
}

static uint32_t u32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t u64(const unsigned char *p) {
    return (uint64_t)u32(p) | (uint64_t)u32(p + 4) << 32;
}

static void put64(unsigned char *p, uint64_t value) {
    for (size_t i = 0; i < 8; i++)
        p[i] = (unsigned char)(value >> (8 * i));
}

static void utf8(scenario_run_t *r, const void *raw, size_t size) {
    string_value_t v = decode_utf8_n(raw, size);
    REQUIRE(v.data || !size);
    if (v.should_free)
        language_owned(r, (gchar_t *)v.data);
    size_t n;
    char *text = language_owned(r, encode_utf8_view((string_view_t){v.data, v.length}, &n));
    REQUIRE(n == size && !memcmp(text, raw, size));
}

static const unsigned char *section(scenario_run_t *r,
                                    const unsigned char *image,
                                    size_t size,
                                    uint64_t sections,
                                    uint16_t stride,
                                    uint16_t amount,
                                    uint32_t index) {
    REQUIRE(index < amount && sections <= size && stride >= 64
            && (uint64_t)stride * amount <= size - sections);
    return image + (size_t)sections + (size_t)stride * index;
}

/* Prove a genuine function alias in bounded ELF64 dynamic symbols before changing
 * precisely its STT nibble. Never manufacture an unrelated data object as an alias. */
static void alias_type(scenario_run_t *r, const char *path, const char *name, bool object) {
    bytes_t original = read_bytes(r, path);
    REQUIRE(original.size >= 64 && !memcmp(original.data, "\177ELF\002\001", 6));
    unsigned char *image = owned(r, malloc(original.size));
    memcpy(image, original.data, original.size);
    uint64_t sections = u64(image + 40);
    uint16_t stride = u16(image + 58), amount = u16(image + 60);
    REQUIRE(amount && stride >= 64 && sections <= original.size
            && (uint64_t)stride * amount <= original.size - sections);
    size_t entries[2] = {0};
    uint64_t addresses[2] = {0};
    uint16_t defined[2] = {0};
    bool found[2] = {false, false};
    for (uint32_t index = 0; index < amount; index++) {
        const unsigned char *table =
            section(r, image, original.size, sections, stride, amount, index);
        if (u32(table + 4) != 11)
            continue;
        uint64_t offset = u64(table + 24), size = u64(table + 32), step = u64(table + 56);
        uint32_t link = u32(table + 40);
        REQUIRE(step == 24 && size % step == 0 && offset <= original.size
                && size <= original.size - offset);
        const unsigned char *strings =
            section(r, image, original.size, sections, stride, amount, link);
        uint64_t strings_offset = u64(strings + 24), strings_size = u64(strings + 32);
        REQUIRE(u32(strings + 4) == 3 && strings_offset <= original.size
                && strings_size <= original.size - strings_offset);
        for (uint64_t entry = offset; entry < offset + size; entry += step) {
            uint32_t name_offset = u32(image + (size_t)entry);
            REQUIRE(name_offset < strings_size);
            size_t first = (size_t)(strings_offset + name_offset),
                   available = (size_t)(strings_size - name_offset);
            const unsigned char *end = memchr(image + first, 0, available);
            REQUIRE(end);
            size_t length = (size_t)(end - image - first);
            utf8(r, image + first, length);
            const char *wanted[] = {name, "actual_function"};
            for (size_t j = 0; j < 2; j++)
                if (strlen(wanted[j]) == length && !memcmp(image + first, wanted[j], length)) {
                    REQUIRE(!found[j] && u16(image + (size_t)entry + 6)
                            && (image[(size_t)entry + 4] & 15) == 2);
                    found[j] = true;
                    entries[j] = (size_t)entry;
                    addresses[j] = u64(image + (size_t)entry + 8);
                    defined[j] = u16(image + (size_t)entry + 6);
                }
        }
    }
    REQUIRE(found[0] && found[1] && addresses[0] && addresses[0] == addresses[1]
            && defined[0] == defined[1]);
    unsigned char function[24];
    memcpy(function, image + entries[1], sizeof(function));
    if (object) {
        image[entries[0] + 4] = (unsigned char)((image[entries[0] + 4] & 0xf0) | 1);
        language_write(r, path, image, original.size);
        size_t changed = entries[0] + 4;
        REQUIRE(!memcmp(image, original.data, changed));
        REQUIRE(!memcmp(image + changed + 1,
                        (const unsigned char *)original.data + changed + 1,
                        original.size - changed - 1));
    }
    REQUIRE((image[entries[0] + 4] & 15) == (object ? 1 : 2) && (image[entries[1] + 4] & 15) == 2
            && !memcmp(image + entries[1], function, sizeof(function)));
    bytes_t reread = read_bytes(r, path);
    scenario_require_bytes(r,
                           "exact ELF alias mutation",
                           image,
                           original.size,
                           reread.data,
                           reread.size,
                           false);
}

static const char *symbol(scenario_run_t *r, yyjson_val *specs, size_t i) {
    const char *s = field(r, yyjson_arr_get(specs, i), "symbol");
    REQUIRE(*s);
    for (size_t j = 0; s[j]; j++)
        REQUIRE((s[j] >= 'a' && s[j] <= 'z') || (s[j] >= 'A' && s[j] <= 'Z') || s[j] == '_'
                || (j && s[j] >= '0' && s[j] <= '9'));
    return s;
}

static const char *
path_for(scenario_run_t *r, const char *work, const char *name, const char *extension) {
    size_t n = strlen(name) + strlen(extension) + 1;
    char *file = owned(r, malloc(n));
    snprintf(file, n, "%s%s", name, extension);
    return language_join(r, work, file);
}
#ifdef _WIN32
#    define EXPORT "__declspec(dllexport)"
#    define LIBRARY_EXTENSION ".dll"
#else
#    define EXPORT "__attribute__((visibility(\"default\")))"
#    define LIBRARY_EXTENSION ".so"
#endif
/* Emit exactly the original recipe's C declarations. The descriptor and symbol
 * names come from this scenario's freshly built module, never frozen host output. */
static const char *compile_fixture(scenario_run_t *r,
                                   configuration_t *c,
                                   const char *work,
                                   const char *name,
                                   bytes_t envelope,
                                   yyjson_val *specs,
                                   const char *hash_style) {
    bool missing = !strcmp(name, "missing-descriptor"),
         writable = !strcmp(name, "writable-descriptor"),
         hidden = !strcmp(name, "unreadable-descriptor"), dependency = !strcmp(name, "dependency"),
         data = strstr(name, "data-") == name, alias = strstr(name, "alias-") != NULL;
    const char *source = path_for(r, work, name, ".c"),
               *path = path_for(r, work, name, LIBRARY_EXTENSION);
    FILE *file = FOPEN(source, "wb");
    REQUIRE(file);
    bool ok = true;
    if (!missing) {
        const char *qualifier = writable ? ""
                                : hidden ? "const __attribute__((aligned(4096)))"
                                         : "const";
        ok = ok
             && fprintf(file,
                        "%s %s unsigned char goat_module_descriptor_v1[] = {",
                        EXPORT,
                        qualifier)
                    >= 0;
        const unsigned char *bytes = envelope.data;
        for (size_t i = 0; i < envelope.size; i++)
            ok = ok && fprintf(file, "%s%u", i ? "," : "", (unsigned)bytes[i]) >= 0;
        ok = ok && fputs("};\n", file) >= 0;
    }
    size_t count = dependency ? 0 : yyjson_arr_size(specs);
    if (!strcmp(name, "missing-last-symbol"))
        count--;
    for (size_t i = 0; i < count; i++) {
        const char *s = symbol(r, specs, i);
        if (!i && alias) {
            ok = ok
                 && fprintf(file,
                            "%s unsigned actual_function(void) { return 0; }\n%s unsigned %s(void) "
                            "__attribute__((alias(\"actual_function\")));\n",
                            EXPORT,
                            EXPORT,
                            s)
                        >= 0;
            continue;
        }
        if (!i && data)
            ok = ok
                 && fprintf(file,
                            "%s %s const int %s=0;\n",
                            EXPORT,
                            strstr(name, "data-in-text") == name
                                ? "__attribute__((section(\".text\")))"
                                : "",
                            s)
                        >= 0;
        else
            ok = ok && fprintf(file, "%s unsigned %s(void) { return 0; }\n", EXPORT, s) >= 0;
    }
    if (hidden)
        ok = ok
             && fputs("#include <sys/mman.h>\n__attribute__((constructor)) static void "
                      "protect_descriptor(void) { mprotect((void *)goat_module_descriptor_v1, "
                      "4096, PROT_NONE); }\n",
                      file)
                    >= 0;
    if (dependency)
        ok = ok && fputs("int dependency_marker(void) { return 1; }\n", file) >= 0;
    if (FCLOSE(file))
        ok = false;
    REQUIRE(ok);
    const char *args[10];
    size_t n = 0;
    args[n++] = c->compiler;
#ifndef _WIN32
    args[n++] = "-fPIC";
#endif
    args[n++] = "-shared";
    args[n++] = "-o";
    args[n++] = path;
    args[n++] = source;
#ifndef _WIN32
    char option[64];
    if (hash_style) {
        snprintf(option, sizeof(option), "-Wl,--hash-style=%s", hash_style);
        args[n++] = option;
    }
#else
    (void)hash_style;
#endif
    args[n] = NULL;
    command(r, c->compiler, args);
    if (alias)
        alias_type(r, path, symbol(r, specs, 0), strstr(name, "object-alias") == name);
    return path;
}

static void library_case(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    const char *work = scenario_work_directory(scenario_run_context(r));
    bytes_t goat = read_bytes(r, language_join(r, c->data, "module-library/main.goat"));
    const char *source = language_join(r, work, "main.goat"),
               *descriptor = language_join(r, work, "module.gmod"),
               *archive_path = language_join(r, work, "module.gbin");
    language_write(r, source, goat.data, goat.size);
    const char gmod[] = "{\"formatVersion\":1,\"name\":\"mapped\",\"version\":\"1\",\"main\":"
                        "\"main.goat\",\"native\":{\"mode\":\"required\"}}";
    language_write(r, descriptor, gmod, sizeof(gmod) - 1);
    const char *args[] = {c->builder,
                          "native-build",
                          descriptor,
                          archive_path,
                          "descriptor",
                          "descriptor",
                          c->compiler,
                          NULL};
    command(r, c->builder, args);
    bytes_t archive = read_bytes(r, archive_path), raw = part(r, archive, "module.json");
    yyjson_val *metadata = parse(r, raw), *native = yyjson_obj_get(metadata, "native"),
               *variant = yyjson_arr_get(native, 0);
    REQUIRE(yyjson_is_obj(variant));
    const char *manifest = language_join(r, work, "module.json");
    language_write(r, manifest, raw.data, raw.size);
    bytes_t original = part(r, archive, field(r, variant, "library"));
    const char *original_path = path_for(r, work, "original", LIBRARY_EXTENSION);
    language_write(r, original_path, original.data, original.size);
    const char *path = original_path;
    if (strcmp(c->name, "original")) {
        yyjson_val *specs = yyjson_obj_get(variant, "specializations");
        REQUIRE(yyjson_is_arr(specs) && yyjson_arr_size(specs) >= 2);
        yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
        REQUIRE(doc);
        scenario_defer(r, free_mut, doc);
        yyjson_mut_val *root = yyjson_mut_obj(doc);
        REQUIRE(root);
        yyjson_mut_doc_set_root(doc, root);
        REQUIRE(yyjson_mut_obj_add_uint(doc, root, "formatVersion", 1));
        const char *keys[] =
            {"module", "version", "mainHash", "target", "goatAbiVersion", "specializations"};
        yyjson_val *values[] = {yyjson_obj_get(metadata, "name"),
                                yyjson_obj_get(metadata, "version"),
                                yyjson_obj_get(metadata, "mainHash"),
                                yyjson_obj_get(variant, "target"),
                                yyjson_obj_get(variant, "goatAbiVersion"),
                                specs};
        for (size_t i = 0; i < 6; i++) {
            REQUIRE(values[i]);
            REQUIRE(yyjson_mut_obj_add(root,
                                       yyjson_mut_str(doc, keys[i]),
                                       yyjson_val_mut_copy(doc, values[i])));
        }
        size_t json_size;
        char *json = owned(r, format_json_canonical(root, &json_size));
        REQUIRE(json_size <= SIZE_MAX - 8209);
        size_t size = 17 + json_size;
        unsigned char *envelope = owned(r, calloc(1, size + 8192));
        memcpy(envelope, "GOATND01", 8);
        put64(envelope + 8, json_size);
        memcpy(envelope + 16, json, json_size);
        if (!strcmp(c->name, "bad-magic"))
            memcpy(envelope, "INVALID!", 8);
        else if (!strcmp(c->name, "overflow")) {
            size = 16;
            memset(envelope + 8, 255, 8);
        } else if (!strcmp(c->name, "outside-image")) {
            size = 16;
            put64(envelope + 8, UINT64_C(1) << 30);
        } else if (!strcmp(c->name, "unterminated"))
            envelope[size - 1] = '!';
        else if (!strcmp(c->name, "different-name")) {
            bool changed = false;
            for (size_t i = 0; i + 6 <= size; i++)
                if (!memcmp(envelope + i, "mapped", 6)) {
                    memcpy(envelope + i, "otherx", 6);
                    changed = true;
                }
            REQUIRE(changed);
        } else if (!strcmp(c->name, "unreadable-descriptor"))
            size += 8192;
        const char *hash_style = NULL;
        if (strstr(c->name, "-sysv"))
            hash_style = "sysv";
        else if (strstr(c->name, "-gnu"))
            hash_style = "gnu";
        else if (strstr(c->name, "-both"))
            hash_style = "both";
        if (!strcmp(c->name, "foreign-owner")) {
            const char *dependency = compile_fixture(r,
                                                     c,
                                                     work,
                                                     "dependency",
                                                     (bytes_t){envelope, size},
                                                     specs,
                                                     NULL),
                       *code = language_join(r, work, "foreign.c");
            path = language_join(r, work, "foreign.so");
            FILE *file = FOPEN(code, "wb");
            REQUIRE(file);
            bool ok = fputs("extern int dependency_marker(void);\n", file) >= 0;
            for (size_t i = 0; i < yyjson_arr_size(specs); i++)
                ok = ok
                     && fprintf(file,
                                "%s unsigned %s(void) { return dependency_marker(); }%s",
                                EXPORT,
                                symbol(r, specs, i),
                                i + 1 < yyjson_arr_size(specs) ? "\n" : "")
                            >= 0;
            if (FCLOSE(file))
                ok = false;
            REQUIRE(ok);
            size_t n = strlen(work) + 12;
            char *rpath = owned(r, malloc(n));
            snprintf(rpath, n, "-Wl,-rpath,%s", work);
            const char *link[] =
                {c->compiler, "-shared", "-fPIC", "-o", path, code, dependency, rpath, NULL};
            command(r, c->compiler, link);
        } else
            path =
                compile_fixture(r, c, work, c->name, (bytes_t){envelope, size}, specs, hash_style);
    }
    const char *check[] = {c->loader, path, manifest, c->success ? "success" : "failure", NULL};
    command(r, c->loader, check);
}

static const struct {
    const char *name;
    bool success, elf;
} cases[] = {{"original", true, false},
             {"valid", true, false},
             {"missing-descriptor", false, false},
             {"writable-descriptor", false, false},
             {"bad-magic", false, false},
             {"overflow", false, false},
             {"outside-image", false, false},
             {"unterminated", false, false},
             {"different-name", false, false},
             {"missing-last-symbol", false, false},
             {"data-symbol", false, false},
             {"data-in-text", false, true},
             {"valid-sysv", true, true},
             {"data-in-text-sysv", false, true},
             {"object-alias-sysv", false, true},
             {"function-alias-sysv", true, true},
             {"valid-gnu", true, true},
             {"data-in-text-gnu", false, true},
             {"object-alias-gnu", false, true},
             {"function-alias-gnu", true, true},
             {"valid-both", true, true},
             {"data-in-text-both", false, true},
             {"object-alias-both", false, true},
             {"function-alias-both", true, true},
             {"unreadable-descriptor", false, true},
             {"foreign-owner", false, true}};

int main(int argc, char **argv) {
#ifdef _WIN32
    fs_error_t ae = {0};
    if (!fs_arguments(&argc, &argv, &ae)) {
        clear_fs_error(&ae);
        return 1;
    }
#endif
    int result = 1;
    if (argc == 7 || argc == 8) {
        fs_error_t error = {0};
        char *builder = absolute_fs_path(argv[2], &error);
        clear_fs_error(&error);
        char *loader = absolute_fs_path(argv[3], &error);
        clear_fs_error(&error);
        if (builder && loader) {
            size_t count = sizeof(cases) / sizeof(cases[0]);
            scenario_definition_t defs[sizeof(cases) / sizeof(cases[0])];
            configuration_t config[sizeof(cases) / sizeof(cases[0])];
            char ids[sizeof(cases) / sizeof(cases[0])][96];
            for (size_t i = 0; i < count; i++) {
                snprintf(ids[i], sizeof(ids[i]), "integration.module_library.%s", cases[i].name);
                config[i] = (configuration_t){builder,
                                              loader,
                                              argv[4],
                                              argv[6],
                                              cases[i].name,
                                              cases[i].success,
                                              cases[i].elf};
                defs[i] = (scenario_definition_t){ids[i], 300000, library_case, config + i, NULL};
#ifdef _WIN32
                if (cases[i].elf)
                    defs[i].skip_reason =
                        "ELF symbol/hash-table/mprotect ownership fixture is Unix-only";
#endif
            }
            scenario_options_t options = {.build_directory = argv[5], .data_directory = argv[6]};
            const char *selection[] = {argv[1], argc == 8 ? argv[7] : NULL};
            result = scenario_registry_main(defs, count, argc == 8 ? 2 : 1, selection, &options);
        }
        FREE(builder);
        FREE(loader);
    } else
        fputs("usage: module_library_scenarios ID BUILDER LOADER CC BUILD DATA [--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
