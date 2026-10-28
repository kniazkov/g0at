/** @file module_abi_real.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Compile and execute an independent public module ABI fixture.
 */
#include "lib/memory/allocate.h"
#include "test/support/format_metadata.h"
#include "test/support/language_execution.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static bool release_doc(void *p) {
    yyjson_mut_doc_free(p);
    return true;
}

static bool release_malloc(void *p) {
    free(p);
    return true;
}

static yyjson_mut_doc *read_json(scenario_run_t *r, const char *path) {
    size_t size;
    const char *data = language_read(r, path, &size);
    yyjson_mut_doc *d = format_json_parse(data, size, NULL, 0);
    scenario_require(r, d != NULL, "read ABI fixture JSON");
    scenario_defer(r, release_doc, d);
    return d;
}

static scenario_command_result_t command(scenario_run_t *r, const char *const *args) {
    scenario_command_result_t out = language_command(r, args[0], args, NULL);
    scenario_require(r, out.exit_code == 0, "ABI fixture command exit");
    return out;
}

void module_abi_real(scenario_run_t *r, const char *host, const char *vectors) {
    const char *cc = getenv("CC");
    if (!cc || !*cc)
        cc = MIGRATION_CC;
    const char *work = scenario_work_directory(scenario_run_context(r));
    const char *empty = language_join(r, work, "abi-empty.c");
    language_write(r, empty, "", 0);
    const char *macro_args[] = {cc, "-include", "stdlib.h", "-dM", "-E", "-x", "c", empty, NULL};
    scenario_command_result_t macros = command(r, macro_args);
    const char *text = macros.out;
    const char *arch = strstr(text, "__aarch64__")  ? "aarch64"
                       : strstr(text, "__x86_64__") ? "x86_64"
                                                    : "x86";
    const char *baseline = !strcmp(arch, "aarch64")  ? "-march=armv8-a"
                           : !strcmp(arch, "x86_64") ? "-march=x86-64"
                                                     : "-march=i686";
    const char *features = !strcmp(arch, "aarch64")  ? "[\"fp\",\"neon\"]"
                           : !strcmp(arch, "x86_64") ? "[\"sse2\"]"
                                                     : "[\"i686\"]";
#ifdef _WIN32
    const char *os = "windows", *minimum_os = "[10]";
    const char *runtime = strstr(text, "#define _UCRT ") ? "ucrt" : "msvcrt";
    const char *library = language_join(r, work, "abi-fixture.dll");
#else
    const char *os = "linux", *minimum_os = "[0]", *runtime = "glibc";
    const char *library = language_join(r, work, "abi-fixture.so");
#endif
    char profile[1024];
    int length = snprintf(profile,
                          sizeof(profile),
                          "{\"architecture\":\"%s\",\"bits\":%d,\"os\":\"%s\","
                          "\"endian\":\"little\",\"minimumOs\":%s,\"cpuFeatures\":%s,"
                          "\"runtime\":{\"family\":\"%s\",\"minimumVersion\":[0]}}",
                          arch,
                          !strcmp(arch, "x86") ? 32 : 64,
                          os,
                          minimum_os,
                          features,
                          runtime);
    scenario_require(r, length > 0 && (size_t)length < sizeof(profile), "ABI target size");
    yyjson_mut_doc *target = format_json_parse(profile, (size_t)length, NULL, 0);
    scenario_require(r, target != NULL, "ABI target JSON");
    scenario_defer(r, release_doc, target);
    yyjson_mut_doc *d = read_json(r, language_join(r, vectors, "native-description.json"));
    yyjson_mut_val *root = yyjson_mut_doc_get_root(d);
    scenario_require(
        r,
        yyjson_mut_obj_put(root,
                           yyjson_mut_str(d, "target"),
                           yyjson_mut_val_mut_copy(d, yyjson_mut_doc_get_root(target))),
        "ABI target update");
    yyjson_mut_doc *schema =
        read_json(r, MIGRATION_ROOT "/docs/module-format-v1/schema-source.json");
    char error[256];
    scenario_require(r,
                     format_metadata_normalize(d,
                                               root,
                                               "native",
                                               yyjson_mut_doc_get_root(schema),
                                               error,
                                               sizeof(error)),
                     "normalize ABI fixture");
    size_t raw_size;
    char *raw = format_json_canonical(root, &raw_size);
    scenario_require(r, raw != NULL, "canonical ABI descriptor");
    scenario_defer(r, release_malloc, raw);
    size_t size = raw_size + 17;
    unsigned char *native = language_owned(r, ALLOC(size));
    memcpy(native, "GOATND01", 8);
    for (unsigned i = 0; i < 8; ++i)
        native[8 + i] = (unsigned char)((uint64_t)raw_size >> (8 * i));
    memcpy(native + 16, raw, raw_size);
    native[size - 1] = 0;
    const char *prefix = "#include <goat/module_abi.h>\n#include <string.h>\n"
                         "GOAT_MODULE_EXPORT const unsigned char goat_module_descriptor_v1[] = {";
    const char *suffix =
        "};\nGOAT_MODULE_EXPORT uint32_t GOAT_MODULE_CALL goat_fn_1_v1(\n"
        "uint32_t version, const goat_module_context_v1 *ctx, uint32_t count,\n"
        "const goat_module_value_v1 *arguments, goat_module_value_v1 *result) {\n"
        "if (version != 1) return GOAT_MODULE_ABI_MISMATCH;\n"
        "if (!ctx || !ctx->services || ctx->services->struct_size < GOAT_MODULE_SERVICES_SIZE)\n"
        "return GOAT_MODULE_BAD_REQUEST;\n"
        "if (count != 2 || arguments[0].tag != GOAT_MODULE_INTEGER || arguments[1].tag != "
        "GOAT_MODULE_INTEGER)\n"
        "return GOAT_MODULE_TYPE_MISMATCH;\n"
        "if (result->tag || result->reserved || result->payload) return "
        "GOAT_MODULE_EXTERNAL_ERROR;\n"
        "const goat_module_services_v1 *s = ctx->services;\n"
        "if (!s->is_space(32) || s->is_space(65) || s->to_upper('a') != 'A' || s->to_lower('A') != "
        "'a')\n"
        "return GOAT_MODULE_EXTERNAL_ERROR;\n"
        "uint64_t *temporary = s->allocate(ctx->state, sizeof(uint64_t));\n"
        "*temporary = arguments[0].payload + arguments[1].payload;\n"
        "result->tag = GOAT_MODULE_INTEGER;\nresult->payload = *temporary;\n"
        "s->deallocate(ctx->state, temporary);\nreturn GOAT_MODULE_SUCCESS;\n}\n";
    size_t capacity = strlen(prefix) + size * 4 + strlen(suffix) + 1;
    char *source = language_owned(r, ALLOC(capacity));
    size_t at = strlen(prefix);
    memcpy(source, prefix, at);
    for (size_t i = 0; i < size; ++i)
        at += (size_t)snprintf(source + at, capacity - at, "%s%u", i ? "," : "", native[i]);
    memcpy(source + at, suffix, strlen(suffix) + 1);
    at += strlen(suffix);
    const char *path = language_join(r, work, "abi-fixture.c");
    language_write(r, path, source, at);
    const char *compile_args[] = {cc,
                                  baseline,
                                  "-std=c11",
                                  "-O2",
                                  "-Wall",
                                  "-Wextra",
                                  "-Werror",
                                  "-shared",
#ifndef _WIN32
                                  "-fPIC",
#endif
                                  "-I",
                                  MIGRATION_ROOT "/include",
                                  path,
                                  "-o",
                                  library,
                                  NULL};
    command(r, compile_args);
    char size_text[32];
    snprintf(size_text, sizeof(size_text), "%zu", size);
    const char *load_args[] = {host, "load", library, size_text, NULL};
    scenario_command_result_t loaded = command(r, load_args);
    scenario_require(r, strstr(loaded.out, "returned 42") != NULL, "real ABI adapter result");
}
