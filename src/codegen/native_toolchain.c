/** @file native_toolchain.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Bounded reproducible compiler probes owned by the shared cleaner.
 */
#include "native_toolchain.h"

#include "lib/allocate.h"
#include "lib/file_system.h"
#include "native_requirements.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *targets[] = {"linux-x86_64-gnu",
                                "linux-aarch64-gnu",
                                "windows-x86_64-mingw64",
                                "windows-x86_64-ucrt64",
                                "windows-x86-mingw32"};
static const char *x64_flags[] = {"-std=c11",
                                  "-O2",
                                  "-march=x86-64",
                                  "-mtune=generic",
                                  "-m64",
                                  "-fPIC",
                                  "-shared",
                                  "-fno-fast-math",
                                  "-ffp-contract=off",
                                  "-Wl,-z,defs",
                                  NULL};
static const char *arm_flags[] = {"-std=c11",
                                  "-O2",
                                  "-march=armv8-a",
                                  "-fPIC",
                                  "-shared",
                                  "-fno-fast-math",
                                  "-ffp-contract=off",
                                  "-Wl,-z,defs",
                                  NULL};
static const char *win64_flags[] = {"-std=c11",
                                    "-O2",
                                    "-march=x86-64",
                                    "-mtune=generic",
                                    "-m64",
                                    "-shared",
                                    "-fno-fast-math",
                                    "-ffp-contract=off",
                                    "-D_WIN32_WINNT=0x0A00",
                                    "-DWINVER=0x0A00",
                                    "-DGOAT_NATIVE_BUILD",
                                    "-static-libgcc",
                                    "-Wl,--no-undefined",
                                    "-Wl,--exclude-all-symbols",
                                    NULL};
static const char *win32_flags[] = {"-std=c11",
                                    "-O2",
                                    "-march=i686",
                                    "-mtune=generic",
                                    "-m32",
                                    "-shared",
                                    "-fno-fast-math",
                                    "-ffp-contract=off",
                                    "-D_WIN32_WINNT=0x0A00",
                                    "-DWINVER=0x0A00",
                                    "-DGOAT_NATIVE_BUILD",
                                    "-static-libgcc",
                                    "-Wl,--no-undefined",
                                    "-Wl,--exclude-all-symbols",
                                    NULL};

typedef struct probe_t {
    native_toolchain_t result;
    uint64_t timeout;
    struct probe_t *next;
} probe_t;

struct native_toolchain_context_t {
    cleanup_service_t *cleanup;
    probe_t *probes;
};

static char *copy_text(const char *value) {
    size_t size = strlen(value) + 1;
    char *result = ALLOC(size);
    memcpy(result, value, size);
    return result;
}

static bool same_platform(const goat_native_environment *a, const goat_native_environment *b) {
    return a->os == b->os && a->architecture == b->architecture && a->runtime == b->runtime
           && a->bits == b->bits;
}

bool native_toolchain_target(const char *target, goat_native_environment *out) {
    if (!out)
        return false;
    *out = (goat_native_environment){0};
    const char *suffix = target ? strchr(target, '+') : NULL;
    if (suffix) {
        if (strcmp(suffix, "+avx2"))
            return false;
        size_t length = (size_t)(suffix - target);
        char *base = ALLOC(length + 1);
        memcpy(base, target, length);
        base[length] = 0;
        bool supported = native_toolchain_target(base, out);
        FREE(base);
        if (!supported || out->architecture != GOAT_ARCH_X86_64)
            return false;
        out->cpu_features |= GOAT_CPU_SSE3 | GOAT_CPU_SSSE3 | GOAT_CPU_SSE4_1 | GOAT_CPU_SSE4_2
                             | GOAT_CPU_AVX | GOAT_CPU_AVX2;
        return true;
    }
    if (!target || !strcmp(target, "host")) {
        goat_native_environment host;
        if (!goat_native_environment_detect(&host))
            return false;
        for (size_t i = 0; i < sizeof(targets) / sizeof(*targets); i++) {
            goat_native_environment candidate;
            native_toolchain_target(targets[i], &candidate);
            if (same_platform(&host, &candidate)) {
                *out = candidate;
                return true;
            }
        }
        return false;
    }
    out->little_endian = true;
    if (!strcmp(target, targets[0]) || !strcmp(target, targets[1])) {
        out->os = GOAT_OS_LINUX;
        out->architecture = !strcmp(target, targets[0]) ? GOAT_ARCH_X86_64 : GOAT_ARCH_AARCH64;
        out->bits = 64;
        out->runtime = GOAT_RUNTIME_GLIBC;
        out->cpu_features =
            out->architecture == GOAT_ARCH_X86_64 ? GOAT_CPU_SSE2 : GOAT_CPU_FP | GOAT_CPU_NEON;
        return true;
    }
    if (!strcmp(target, targets[2]) || !strcmp(target, targets[3]) || !strcmp(target, targets[4])) {
        out->os = GOAT_OS_WINDOWS;
        out->architecture = !strcmp(target, targets[4]) ? GOAT_ARCH_X86 : GOAT_ARCH_X86_64;
        out->bits = out->architecture == GOAT_ARCH_X86 ? 32 : 64;
        out->runtime = !strcmp(target, targets[3]) ? GOAT_RUNTIME_UCRT : GOAT_RUNTIME_MSVCRT;
        out->cpu_features = out->bits == 64 ? GOAT_CPU_SSE2 : GOAT_CPU_I686;
        out->os_version = (goat_native_version){.components = {10, 0}, .known = true};
        return true;
    }
    *out = (goat_native_environment){0};
    return false;
}

static const char *target_name(const goat_native_environment *env) {
    for (size_t i = 0; i < sizeof(targets) / sizeof(*targets); i++) {
        goat_native_environment candidate;
        native_toolchain_target(targets[i], &candidate);
        if (same_platform(env, &candidate))
            return (env->cpu_features & GOAT_CPU_AVX2) ? (i == 0   ? "linux-x86_64-gnu+avx2"
                                                          : i == 2 ? "windows-x86_64-mingw64+avx2"
                                                                   : "windows-x86_64-ucrt64+avx2")
                                                       : targets[i];
    }
    return "unsupported";
}

static const char *const *target_flags(const goat_native_environment *env) {
    const char *const *base =
        env->os == GOAT_OS_WINDOWS
            ? (env->bits == 32 ? win32_flags : win64_flags)
            : (env->architecture == GOAT_ARCH_AARCH64 ? arm_flags : x64_flags);
    size_t count = 0;
    while (base[count])
        count++;
    const char **flags = ALLOC((count + 3) * sizeof(*flags));
    memcpy(flags, base, count * sizeof(*flags));
    if (env->cpu_features & GOAT_CPU_AVX2) {
        flags[count++] = "-mavx2";
        /* GCC enables POPCNT with AVX2; v1 has no independent POPCNT requirement. */
        flags[count++] = "-mno-popcnt";
    }
    flags[count] = NULL;
    return flags;
}

static char *resolve_compiler(const char *name) {
    if (strchr(name, '/') || strchr(name, '\\'))
        return canonical_fs_path(name, NULL);
    char *path = fs_environment("PATH", NULL);
    if (!path)
        return NULL;
    char *found = NULL;
    char *part = path;
    for (char *p = path;; p++) {
#ifdef _WIN32
        bool separator = *p == ';';
#else
        bool separator = *p == ':';
#endif
        if (!*p || separator) {
            bool last = !*p;
            *p = 0;
            char *joined = join_fs_path(*part ? part : ".", name, NULL);
            fs_info_t info;
            if (joined && inspect_fs_path(joined, true, &info, NULL) && info.kind == FS_FILE)
                found = canonical_fs_path(joined, NULL);
#ifdef _WIN32
            if (!found && joined) {
                char *exe = ALLOC(strlen(joined) + 5);
                sprintf(exe, "%s.exe", joined);
                if (inspect_fs_path(exe, true, &info, NULL) && info.kind == FS_FILE)
                    found = canonical_fs_path(exe, NULL);
                FREE(exe);
            }
#endif
            FREE(joined);
            if (found || last)
                break;
            part = p + 1;
        }
    }
    FREE(path);
    return found;
}

static char *run(native_toolchain_context_t *ctx,
                 const char *const *argv,
                 child_deadline_t deadline,
                 bool *timeout,
                 char **diagnostic) {
    child_options_t options = {.executable = argv[0], .arguments = argv, .deadline = deadline};
    child_result_t *result = ALLOC(sizeof(*result));
    cleanup_status_t status = run_cleanup_process(ctx->cleanup, &options, result);
    char *text = NULL;
    if (status == CLEANUP_OK && result->status == CHILD_EXITED && !result->exit_code
        && !result->system_error && !result->cleanup_error && !result->out.truncated) {
        text = ALLOC(result->out.size + 1);
        memcpy(text, result->out.data, result->out.size);
        text[result->out.size] = 0;
    } else {
        *timeout = status == CLEANUP_TIMEOUT
                   || (status == CLEANUP_OK && result->status == CHILD_TIMED_OUT);
        const char *reason = *timeout               ? "compiler probe timed out"
                             : status != CLEANUP_OK ? "cleanup service cannot run compiler"
                             : result->status == CHILD_START_ERROR ? "compiler could not start"
                                                                   : "compiler command failed";
        size_t size = status == CLEANUP_OK ? result->err.size : 0;
        *diagnostic = ALLOC(strlen(reason) + size + 3);
        sprintf(*diagnostic, "%s%s", reason, size ? ": " : "");
        if (size) {
            size_t offset = strlen(*diagnostic);
            memcpy(*diagnostic + offset, result->err.data, size);
            (*diagnostic)[offset + size] = 0;
        }
    }
    FREE(result);
    return text;
}

typedef struct {
    native_toolchain_context_t *ctx;
    const char *compiler;
    child_deadline_t deadline;
    bool timed_out;
} resolver_t;

static char *resolve_dependency(const char *name, void *opaque) {
    resolver_t *resolver = opaque;
    if (!name || strchr(name, '/') || strchr(name, '\\'))
        return NULL;
    char *option = ALLOC(strlen(name) + 18);
    sprintf(option, "-print-file-name=%s", name);
    const char *argv[] = {resolver->compiler, option, NULL};
    char *error = NULL;
    char *answer = run(resolver->ctx, argv, resolver->deadline, &resolver->timed_out, &error);
    FREE(option);
    FREE(error);
    if (!answer)
        return NULL;
    size_t size = strlen(answer);
    while (size && (answer[size - 1] == '\n' || answer[size - 1] == '\r'))
        answer[--size] = 0;
    char *path = strcmp(answer, name) ? canonical_fs_path(answer, NULL) : NULL;
    FREE(answer);
    return path;
}

static char *macro_value(const char *macros, const char *name) {
    size_t n = strlen(name);
    for (const char *line = macros; line && *line; line = strchr(line, '\n')) {
        if (*line == '\n')
            line++;
        if (!strncmp(line, "#define ", 8) && !strncmp(line + 8, name, n) && line[8 + n] == ' ') {
            const char *value = line + 9 + n;
            const char *end = strchr(value, '\n');
            size_t size = end ? (size_t)(end - value) : strlen(value);
            if (size > 1 && value[0] == '"' && value[size - 1] == '"') {
                value++;
                size -= 2;
            }
            char *copy = ALLOC(size + 1);
            memcpy(copy, value, size);
            copy[size] = 0;
            return copy;
        }
    }
    return NULL;
}

static bool has_macro(const char *macros, const char *name) {
    char *value = macro_value(macros, name);
    bool found = value != NULL;
    FREE(value);
    return found;
}

static bool macros_match(const char *text, const goat_native_environment *env) {
    if (has_macro(text, "_WIN32") != (env->os == GOAT_OS_WINDOWS)
        || has_macro(text, "__aarch64__") != (env->architecture == GOAT_ARCH_AARCH64)
        || has_macro(text, "__x86_64__") != (env->architecture == GOAT_ARCH_X86_64)
        || has_macro(text, "__i386__") != (env->architecture == GOAT_ARCH_X86))
        return false;
    /* Every supported baseline deliberately excludes these elevated settings. */
    const char *extra[] = {"__AVX__",
                           "__AVX2__",
                           "__AVX512F__",
                           "__FMA__",
                           "__BMI__",
                           "__BMI2__",
                           "__SSE3__",
                           "__SSSE3__",
                           "__SSE4_1__",
                           "__SSE4_2__",
                           "__ARM_FEATURE_SVE",
                           "__ARM_FEATURE_ATOMICS",
                           "__ARM_FEATURE_CRYPTO"};
    const uint64_t bits[] = {GOAT_CPU_AVX,
                             GOAT_CPU_AVX2,
                             GOAT_CPU_AVX512F,
                             GOAT_CPU_FMA,
                             GOAT_CPU_BMI1,
                             GOAT_CPU_BMI2,
                             GOAT_CPU_SSE3,
                             GOAT_CPU_SSSE3,
                             GOAT_CPU_SSE4_1,
                             GOAT_CPU_SSE4_2,
                             GOAT_CPU_SVE,
                             GOAT_CPU_LSE,
                             GOAT_CPU_AES};
    for (size_t i = 0; i < sizeof(extra) / sizeof(*extra); i++)
        if (has_macro(text, extra[i]) != ((env->cpu_features & bits[i]) != 0))
            return false;
    const char *unmodeled[] =
        {"__POPCNT__", "__LZCNT__", "__F16C__", "__PCLMUL__", "__RDRND__", "__RDSEED__", "__SHA__"};
    for (size_t i = 0; i < sizeof(unmodeled) / sizeof(*unmodeled); i++)
        if (has_macro(text, unmodeled[i]))
            return false;
    return true;
}

static bool inspect(native_toolchain_context_t *ctx,
                    const native_toolchain_t *candidate,
                    const char *library,
                    child_deadline_t deadline,
                    goat_native_environment *environment,
                    char **diagnostic,
                    bool *timed_out) {
    resolver_t resolver = {.ctx = ctx, .compiler = candidate->compiler, .deadline = deadline};
    bool result = inspect_native_requirements(library,
                                              &candidate->requirements,
                                              resolve_dependency,
                                              &resolver,
                                              environment,
                                              diagnostic);
    *timed_out = resolver.timed_out;
    return result;
}

static native_toolchain_t *
probe(native_toolchain_context_t *ctx, const char *compiler, const char *target, uint64_t timeout) {
    for (probe_t *p = ctx->probes; p; p = p->next)
        if (p->timeout == timeout && !strcmp(p->result.compiler, compiler)
            && !strcmp(p->result.target, target))
            return &p->result;
    probe_t *p = CALLOC(sizeof(*p));
    p->next = ctx->probes;
    ctx->probes = p;
    p->timeout = timeout;
    native_toolchain_t *candidate = &p->result;
    candidate->probe_timeout_seconds = timeout;
    candidate->compiler = copy_text(compiler);
    candidate->target = copy_text(target);
    native_toolchain_target(target, &candidate->requirements);
    candidate->flags = target_flags(&candidate->requirements);
    if (ensure_cleanup_service(ctx->cleanup) != CLEANUP_OK) {
        candidate->diagnostic = copy_text("cleanup service did not become ready");
        return candidate;
    }
    child_deadline_t deadline = child_deadline_after(timeout * 1000);
    char *prefix = join_fs_path(cleanup_service_directory(ctx->cleanup), "probe", NULL);
    char *directory = prefix ? create_fs_temporary(prefix, true, NULL, NULL) : NULL;
    FREE(prefix);
    char *source = directory ? join_fs_path(directory, "probe.c", NULL) : NULL;
    char *library =
        directory
            ? join_fs_path(directory,
                           candidate->requirements.os == GOAT_OS_WINDOWS ? "probe.dll" : "probe.so",
                           NULL)
            : NULL;
    FREE(directory);
    FILE *file = source ? FOPEN(source, "wb") : NULL;
    if (!file) {
        candidate->diagnostic = copy_text("cannot write compiler probe source");
        goto done;
    }
    const char *code =
        "#include <stdlib.h>\n#include <stdint.h>\n#include <math.h>\n"
        "#ifdef _WIN32\n__declspec(dllexport)\n#endif\n"
        "double goat_probe(double x) { void *p=malloc(16); free(p); return sqrt(x); }\n";
    bool written = FWRITE(code, 1, strlen(code), file) == strlen(code);
    if (FCLOSE(file) || !written) {
        candidate->diagnostic = copy_text("cannot write compiler probe source");
        goto done;
    }
    const char *argv[48];
    size_t count = 0;
    argv[count++] = compiler;
    for (size_t i = 0; candidate->flags[i]; i++)
        argv[count++] = candidate->flags[i];
    size_t common = count;
    argv[count++] = "-dM";
    argv[count++] = "-E";
    argv[count++] = source;
    argv[count] = NULL;
    char *macros = run(ctx, argv, deadline, &candidate->timed_out, &candidate->diagnostic);
    if (!macros)
        goto done;
    candidate->name = has_macro(macros, "__clang__")  ? copy_text("Clang")
                      : has_macro(macros, "__GNUC__") ? copy_text("GCC")
                                                      : NULL;
    candidate->version = macro_value(macros, "__VERSION__");
    bool match = macros_match(macros, &candidate->requirements);
    FREE(macros);
    if (!candidate->name || !candidate->version || !match) {
        candidate->diagnostic = copy_text("unsupported compiler or effective CPU/target settings");
        goto done;
    }
    count = common;
    argv[count++] = "-o";
    argv[count++] = library;
    argv[count++] = source;
    argv[count++] = "-lm";
    argv[count] = NULL;
    char *output = run(ctx, argv, deadline, &candidate->timed_out, &candidate->diagnostic);
    if (!output)
        goto done;
    FREE(output);
    goat_native_environment actual;
    candidate->available = inspect(ctx,
                                   candidate,
                                   library,
                                   deadline,
                                   &actual,
                                   &candidate->diagnostic,
                                   &candidate->timed_out);
    if (candidate->available)
        candidate->requirements = actual;
done:
    /* Only the shared cleaner removes these paths, after its owned process trees stop. */
    FREE(source);
    FREE(library);
    return candidate;
}

native_toolchain_context_t *create_native_toolchain_context(cleanup_service_t *cleanup) {
    if (!cleanup)
        return NULL;
    native_toolchain_context_t *ctx = CALLOC(sizeof(*ctx));
    ctx->cleanup = cleanup;
    return ctx;
}

void destroy_native_toolchain_context(native_toolchain_context_t *ctx) {
    if (!ctx)
        return;
    while (ctx->probes) {
        probe_t *p = ctx->probes;
        ctx->probes = p->next;
        FREE(p->result.compiler);
        FREE(p->result.name);
        FREE(p->result.version);
        FREE(p->result.target);
        FREE((void *)p->result.flags);
        FREE(p->result.diagnostic);
        FREE(p);
    }
    FREE(ctx);
}

native_toolchain_search_t find_native_toolchains(native_toolchain_context_t *ctx,
                                                 const char *target,
                                                 const char *compiler,
                                                 uint64_t timeout,
                                                 bool enumerate) {
    native_toolchain_search_t search = {0};
    if (!ctx || !timeout || timeout > UINT64_MAX / 1000) {
        search.diagnostic =
            copy_text("native probe timeout must be a positive representable integer");
        return search;
    }
    goat_native_environment expected;
    if ((target || !enumerate) && !native_toolchain_target(target, &expected)) {
        search.diagnostic = copy_text("unsupported native target; no host substitution");
        return search;
    }
    const char *names[] = {
#ifdef _WIN32
        "gcc",
        "clang",
        "cc",
#else
        "cc",
        "gcc",
        "clang",
#endif
        "x86_64-linux-gnu-gcc",
        "aarch64-linux-gnu-gcc",
        "x86_64-w64-mingw32-gcc",
        "i686-w64-mingw32-gcc"};
    size_t n = compiler ? 1 : sizeof(names) / sizeof(*names);
    search.items = ALLOC(n * (sizeof(targets) / sizeof(*targets)) * sizeof(*search.items));
    for (size_t i = 0; i < n; i++) {
        const char *name = compiler ? compiler : names[i];
        char *resolved = resolve_compiler(name);
        if (!resolved) {
            if (compiler)
                search.diagnostic = copy_text("explicit C compiler was not found");
            continue;
        }
        size_t target_count = !target && enumerate ? sizeof(targets) / sizeof(*targets) : 1;
        for (size_t j = 0; j < target_count; j++) {
            const char *id = target_count == 1 ? target_name(&expected) : targets[j];
            native_toolchain_t *candidate = probe(ctx, resolved, id, timeout);
            bool duplicate = false;
            for (size_t k = 0; k < search.count; k++)
                duplicate |= search.items[k] == candidate;
            if (!duplicate)
                search.items[search.count++] = candidate;
            if (candidate->available && !search.selected)
                search.selected = candidate;
            if (search.selected && !enumerate)
                break;
        }
        FREE(resolved);
        if (search.selected && !enumerate)
            break;
    }
    if (!search.count && !search.diagnostic)
        search.diagnostic = copy_text("no supported C compiler was found on PATH");
    if (search.count && !search.selected && !search.diagnostic) {
        size_t size = 1;
        for (size_t i = 0; i < search.count; i++) {
            const native_toolchain_t *item = search.items[i];
            size = allocation_size_add(size, strlen(item->compiler));
            size = allocation_size_add(size, strlen(item->target));
            size = allocation_size_add(size,
                                       strlen(item->diagnostic ? item->diagnostic : "probe failed")
                                           + 8);
        }
        search.diagnostic = ALLOC(size);
        size_t offset = 0;
        for (size_t i = 0; i < search.count; i++) {
            const native_toolchain_t *item = search.items[i];
            offset += (size_t)snprintf(search.diagnostic + offset,
                                       size - offset,
                                       "%s [%s]: %s\n",
                                       item->compiler,
                                       item->target,
                                       item->diagnostic ? item->diagnostic : "probe failed");
        }
    }
    return search;
}

void destroy_native_toolchain_search(native_toolchain_search_t *search) {
    FREE(search->items);
    FREE(search->diagnostic);
    *search = (native_toolchain_search_t){0};
}

bool native_toolchain_inspect(native_toolchain_context_t *ctx,
                              const native_toolchain_t *candidate,
                              const char *library,
                              goat_native_environment *requirements,
                              char **diagnostic) {
    if (!ctx || !candidate || !candidate->available || !library || !requirements || !diagnostic)
        return false;
    bool timed_out = false;
    /* External dependency queries remain bounded; this does not time-limit user compilation. */
    return inspect(ctx,
                   candidate,
                   library,
                   child_deadline_after(candidate->probe_timeout_seconds * 1000),
                   requirements,
                   diagnostic,
                   &timed_out);
}
