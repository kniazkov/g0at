/** @file module_build.c
 * @copyright 2026 Ivan Kniazkov
 * @brief One compiler, one checked native variant, no runtime loading during a build.
 */
#include "module_build.h"

#include "codegen/c_module_v1.h"
#include "codegen/native_artifact_validation.h"
#include "lib/allocate.h"
#include "lib/binary_file.h"
#include "lib/sha256.h"
#include "module/module_hash.h"
#include "vm/binary.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *copy_text(const char *text) {
    size_t size = strlen(text) + 1;
    char *copy = ALLOC(size);
    memcpy(copy, text, size);
    return copy;
}

static bool fail(module_compilation_error_t *error, const char *message) {
    if (error) {
        FREE(error->message);
        error->message = copy_text(message);
    }
    return false;
}

void free_module_build(module_build_t *build) {
    if (!build)
        return;
    free_module_compilation(build->compilation);
    FREE(build->native_bytes);
    FREE(build->native_name);
    FREE(build->warning);
    FREE(build);
}

module_writer_input_t module_build_writer_input(const module_build_t *build,
                                                module_writer_native_t *native) {
    module_writer_input_t input = {0};
    if (!build || !build->compilation)
        return input;
    input.metadata = build->compilation->metadata;
    input.bytecode =
        (zip_view_t){build->compilation->code->buffer, build->compilation->code->buffer_size};
    if (build->native_size && native) {
        *native = (module_writer_native_t){build->native_name,
                                           strlen(build->native_name),
                                           {build->native_bytes, build->native_size}};
        input.native = native;
        input.native_count = 1;
    }
    return input;
}

static yyjson_mut_val *version(yyjson_mut_doc *doc, goat_native_version value) {
    yyjson_mut_val *array = yyjson_mut_arr(doc);
    size_t count = 4;
    while (count > 1 && !value.components[count - 1])
        --count;
    for (size_t i = 0; i < count; ++i)
        yyjson_mut_arr_add_uint(doc, array, value.components[i]);
    return array;
}

static yyjson_doc *target_document(const goat_native_environment *environment) {
    static const struct {
        uint64_t bit;
        const char *name;
    } features[] = {
        {GOAT_CPU_I686, "i686"},   {GOAT_CPU_SSE2, "sse2"},     {GOAT_CPU_SSE3, "sse3"},
        {GOAT_CPU_SSSE3, "ssse3"}, {GOAT_CPU_SSE4_1, "sse4_1"}, {GOAT_CPU_SSE4_2, "sse4_2"},
        {GOAT_CPU_AVX, "avx"},     {GOAT_CPU_AVX2, "avx2"},     {GOAT_CPU_FMA, "fma"},
        {GOAT_CPU_BMI1, "bmi1"},   {GOAT_CPU_BMI2, "bmi2"},     {GOAT_CPU_AVX512F, "avx512f"},
        {GOAT_CPU_FP, "fp"},       {GOAT_CPU_NEON, "neon"},     {GOAT_CPU_AES, "aes"},
        {GOAT_CPU_SHA1, "sha1"},   {GOAT_CPU_SHA2, "sha2"},     {GOAT_CPU_CRC32, "crc32"},
        {GOAT_CPU_LSE, "lse"},     {GOAT_CPU_SVE, "sve"},       {GOAT_CPU_SVE2, "sve2"}};

    if (!environment->os_version.known || !environment->runtime_version.known)
        return NULL;
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc,
                           root,
                           "os",
                           environment->os == GOAT_OS_WINDOWS ? "windows" : "linux");
    yyjson_mut_obj_add_val(doc, root, "minimumOs", version(doc, environment->os_version));
    yyjson_mut_obj_add_str(doc,
                           root,
                           "architecture",
                           environment->architecture == GOAT_ARCH_X86      ? "x86"
                           : environment->architecture == GOAT_ARCH_X86_64 ? "x86_64"
                                                                           : "aarch64");
    yyjson_mut_obj_add_uint(doc, root, "bits", environment->bits);
    yyjson_mut_obj_add_str(doc, root, "endian", "little");
    yyjson_mut_val *cpu = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, root, "cpuFeatures", cpu);
    for (size_t i = 0; i < sizeof(features) / sizeof(*features); ++i)
        if (environment->cpu_features & features[i].bit)
            yyjson_mut_arr_add_str(doc, cpu, features[i].name);
    yyjson_mut_val *runtime = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, root, "runtime", runtime);
    yyjson_mut_obj_add_str(doc,
                           runtime,
                           "family",
                           environment->runtime == GOAT_RUNTIME_GLIBC    ? "glibc"
                           : environment->runtime == GOAT_RUNTIME_MSVCRT ? "msvcrt"
                           : environment->runtime == GOAT_RUNTIME_UCRT   ? "ucrt"
                                                                         : "musl");
    yyjson_mut_obj_add_val(doc,
                           runtime,
                           "minimumVersion",
                           version(doc, environment->runtime_version));
    size_t size;
    char *json = yyjson_mut_write(doc, 0, &size);
    yyjson_doc *result = NULL;
    if (json)
        goat_module_json_read(json, size, GOAT_MODULE_TARGET, &result);
    free(json);
    yyjson_mut_doc_free(doc);
    return result;
}

typedef struct {
    module_build_t *result;
    native_execution_mode_t mode;
    const char *target, *compiler;
    uint32_t timeout;
    native_build_context_t *context;
    native_toolchain_context_t *toolchains;
} build_context_t;

static bool install_variant(module_compilation_t *compilation,
                            module_build_t *build,
                            const native_toolchain_t *toolchain,
                            const yyjson_doc *target,
                            const yyjson_doc *specializations) {
    uint8_t digest[SHA256_SIZE];
    char hash[65];
    sha256(build->native_bytes, build->native_size, digest);
    for (size_t i = 0; i < SHA256_SIZE; ++i)
        sprintf(hash + i * 2, "%02x", digest[i]);
    yyjson_mut_doc *doc = yyjson_doc_mut_copy(compilation->metadata, NULL);
    yyjson_mut_val *root = yyjson_mut_doc_get_root(doc);
    yyjson_mut_val *variants = yyjson_mut_arr(doc), *variant = yyjson_mut_obj(doc);
    yyjson_mut_arr_append(variants, variant);
    yyjson_mut_obj_put(root, yyjson_mut_str(doc, "native"), variants);
    yyjson_mut_obj_add_strcpy(doc, variant, "library", build->native_name);
    yyjson_mut_obj_add_strcpy(doc, variant, "hash", hash);
    yyjson_mut_obj_add_uint(doc, variant, "goatAbiVersion", 1);
    yyjson_mut_obj_add_val(doc,
                           variant,
                           "target",
                           yyjson_val_mut_copy(doc, yyjson_doc_get_root((yyjson_doc *)target)));
    yyjson_mut_obj_add_val(
        doc,
        variant,
        "specializations",
        yyjson_val_mut_copy(doc, yyjson_doc_get_root((yyjson_doc *)specializations)));
    yyjson_mut_val *compiler = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, variant, "cCompiler", compiler);
    yyjson_mut_obj_add_strcpy(doc, compiler, "name", toolchain->name);
    yyjson_mut_obj_add_strcpy(doc, compiler, "version", toolchain->version);
    size_t size = 0;
    char *json = yyjson_mut_write(doc, 0, &size);
    yyjson_doc *metadata = NULL;
    if (json)
        goat_module_json_read(json, size, GOAT_MODULE_METADATA, &metadata);
    free(json);
    yyjson_mut_doc_free(doc);
    if (!metadata)
        return false;
    bool matches = false;
    goat_module_json_error status = goat_module_main_hash_verify(metadata,
                                                                 compilation->code->buffer,
                                                                 compilation->code->buffer_size,
                                                                 &matches);
    if (status || !matches) {
        yyjson_doc_free(metadata);
        return false;
    }
    yyjson_doc_free(compilation->metadata);
    compilation->metadata = metadata;
    return true;
}

static bool same_target(const yyjson_doc *left, const yyjson_doc *right) {
    size_t a, b;
    char *first = yyjson_write(left, 0, &a), *second = yyjson_write(right, 0, &b);
    bool same = first && second && a == b && !memcmp(first, second, a);
    free(first);
    free(second);
    return same;
}

static bool build_native(const node_t *const *roots,
                         size_t count,
                         module_compilation_t *compilation,
                         void *opaque,
                         module_compilation_error_t *error) {
    build_context_t *context = opaque;
    if (context->mode == NATIVE_OFF)
        return true;
    goat_native_environment requested;
    if (!native_toolchain_target(context->target, &requested))
        return fail(error, "unsupported native target; no host substitution is permitted");
    arena_t *arena = create_arena(64);
    c_module_v1_output_t output = generate_c_module_v1(arena, roots, count);
    bool ok = false;
    native_toolchain_search_t search = {0};
    native_workspace_t *workspace = NULL;
    yyjson_doc *target = NULL;
    if (!output.generated_count) {
        if (context->mode == NATIVE_REQUIRED)
            fail(error, "native required needs an own specialization, including own include files");
        else if (!strcmp(context->target, "host"))
            ok = true;
        if (context->mode == NATIVE_REQUIRED || !strcmp(context->target, "host"))
            goto done;
    }
    search = find_native_toolchains(context->toolchains,
                                    context->target,
                                    context->compiler,
                                    context->timeout,
                                    false);
    if (!search.selected) {
        const char *message = search.diagnostic ? search.diagnostic : "no usable native compiler";
        if (context->mode == NATIVE_REQUIRED || strcmp(context->target, "host"))
            fail(error, message);
        else {
            context->result->warning = copy_text(message);
            ok = true;
        }
        goto done;
    }
    if (!output.generated_count) {
        ok = true;
        goto done;
    }
    const native_toolchain_t *toolchain = search.selected;
    target = target_document(&toolchain->requirements);
    workspace = create_native_workspace_in_context(context->context);
    if (!target || !workspace) {
        fail(error, "cannot prepare checked native build workspace or target");
        goto done;
    }
    /* The descriptor contains actual requirements. Re-emit once after inspecting
     * the complete linked artifact; the second result must agree, never guess. */
    for (unsigned pass = 0; pass < 2; ++pass) {
        if (pass) {
            destroy_native_workspace(workspace);
            workspace = create_native_workspace_in_context(context->context);
            if (!workspace) {
                fail(error, "cannot prepare final native descriptor build workspace");
                goto done;
            }
        }
        string_value_t source = append_c_module_v1_descriptor(VALUE_TO_VIEW(output.source),
                                                              compilation->metadata,
                                                              yyjson_doc_get_root(target),
                                                              output.specializations);
        if (!source.data) {
            fail(error, "cannot emit native module descriptor");
            goto done;
        }
        native_compile_options_t options = {.arguments = toolchain->flags,
                                            .bundled_runtime = true,
                                            .runtime_sources = true};
        native_compile_result_t compiled = compile_native_library_in_context(context->context,
                                                                             source.data,
                                                                             toolchain->compiler,
                                                                             workspace->library,
                                                                             &options);
        FREE_STRING(source);
        if (compiled.status != NATIVE_COMPILE_OK) {
            char detail[256];
            snprintf(detail,
                     sizeof(detail),
                     "selected compiler failed to build generated module C (status %d, system %d, "
                     "Windows %u, cleanup %d)",
                     compiled.status,
                     compiled.system_error,
                     compiled.windows_error,
                     compiled.cleanup_status);
            fail(error,
                 compiled.diagnostics && *compiled.diagnostics ? compiled.diagnostics : detail);
            destroy_native_compile_result(&compiled);
            goto done;
        }
        destroy_native_compile_result(&compiled);
        goat_native_environment requirements;
        char *diagnostic = NULL;
        if (!native_toolchain_inspect(context->toolchains,
                                      toolchain,
                                      workspace->library,
                                      &requirements,
                                      &diagnostic)) {
            fail(error, diagnostic ? diagnostic : "cannot establish native artifact requirements");
            FREE(diagnostic);
            goto done;
        }
        FREE(diagnostic);
        yyjson_doc *actual = target_document(&requirements);
        if (!actual) {
            fail(error, "native artifact requirements are not representable");
            goto done;
        }
        bool same = same_target(target, actual);
        yyjson_doc_free(target);
        target = actual;
        if (!same) {
            if (pass == 1) {
                fail(error, "native requirements changed after descriptor emission");
                goto done;
            }
            continue;
        }
        context->result->native_bytes =
            read_binary_file(workspace->library, GOAT_BINARY_LIMIT, &context->result->native_size);
        context->result->native_name = copy_text(
            requirements.os == GOAT_OS_WINDOWS ? "native/variant-1.dll" : "native/variant-1.so");
        ok = context->result->native_bytes
             && install_variant(compilation,
                                context->result,
                                toolchain,
                                target,
                                output.specializations);
        if (!ok)
            fail(error, "native variant metadata does not match immutable module main content");
        else {
            char *artifact_error = NULL;
            ok = validate_native_artifact_bytes(context->result->native_bytes,
                                                context->result->native_size,
                                                compilation->metadata,
                                                0,
                                                &artifact_error);
            if (!ok)
                fail(error,
                     artifact_error ? artifact_error
                                    : "native descriptor or exports do not match module");
            FREE(artifact_error);
        }
        break;
    }
done:
    yyjson_doc_free(target);
    destroy_native_workspace(workspace);
    destroy_native_toolchain_search(&search);
    FREE_STRING(output.source);
    yyjson_doc_free(output.specializations);
    destroy_arena(arena);
    return ok;
}

bool build_module(const module_sources_t *sources,
                  const module_dependencies_t *dependencies,
                  const module_graph_t *graph,
                  const module_build_options_t *options,
                  module_build_t **out,
                  module_compilation_error_t *error) {
    if (out)
        *out = NULL;
    if (error)
        *error = (module_compilation_error_t){0};
    if (!sources || !out)
        return fail(error, "invalid module build arguments");
    module_build_options_t defaults = {0};
    if (!options)
        options = &defaults;
    yyjson_val *native = sources->descriptor
                             ? yyjson_obj_get(yyjson_doc_get_root(sources->descriptor), "native")
                             : NULL;
    const char *mode = yyjson_get_str(yyjson_obj_get(native, "mode"));
    build_context_t context = {.mode = mode && !strcmp(mode, "off")        ? NATIVE_OFF
                                       : mode && !strcmp(mode, "required") ? NATIVE_REQUIRED
                                                                           : NATIVE_AUTO,
                               .target = yyjson_get_str(yyjson_obj_get(native, "target")),
                               .compiler = options->compiler,
                               .timeout = options->probe_timeout_seconds
                                              ? options->probe_timeout_seconds
                                              : NATIVE_PROBE_DEFAULT_SECONDS};
    if (options->override_mode)
        context.mode = options->mode;
    if (context.mode != NATIVE_OFF && context.mode != NATIVE_AUTO
        && context.mode != NATIVE_REQUIRED)
        return fail(error, "invalid native mode");
    if (options->target)
        context.target = options->target;
    if (!context.target)
        context.target = "host";
    context.context = options->context ? options->context : create_native_build_context(NULL);
    context.toolchains =
        options->toolchains
            ? options->toolchains
            : create_native_toolchain_context(native_build_cleanup(context.context));
    context.result = CALLOC(sizeof(*context.result));
    bool ok = context.context && context.toolchains
              && compile_module_sources_with_backend(sources,
                                                     dependencies,
                                                     graph,
                                                     build_native,
                                                     &context,
                                                     &context.result->compilation,
                                                     error);
    if (!options->toolchains)
        destroy_native_toolchain_context(context.toolchains);
    if (!options->context)
        release_native_build_context(context.context);
    if (ok)
        *out = context.result;
    else
        free_module_build(context.result);
    return ok;
}
