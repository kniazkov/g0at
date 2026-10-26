/** @file native_requirements.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Bounded ELF/PE inspection; no OS loader and no host-version inference.
 */
#include "native_requirements.h"

#include "lib/allocate.h"
#include "lib/binary_file.h"

#include <stdio.h>
#include <string.h>

#define IMAGE_LIMIT (128u * 1024u * 1024u)
#define DEPENDENCY_LIMIT 64

typedef struct {
    const unsigned char *data;
    size_t size;
} image_t;

typedef struct {
    goat_native_environment value;
    native_dependency_resolver_t resolver;
    void *context;
    char *paths[DEPENDENCY_LIMIT];
    size_t count;
    const char *error;
    char detail[4096];
} inspection_t;

static bool range(image_t image, uint64_t offset, uint64_t length) {
    return offset <= image.size && length <= image.size - offset;
}

static uint64_t number(image_t image, size_t offset, unsigned width) {
    uint64_t value = 0;
    if (!range(image, offset, width))
        return 0;
    for (unsigned i = 0; i < width; i++)
        value |= (uint64_t)image.data[offset + i] << (i * 8);
    return value;
}

static const char *string_at(image_t image, uint64_t offset, uint64_t limit) {
    if (!range(image, offset, limit) || !limit)
        return NULL;
    const char *value = (const char *)image.data + offset;
    return memchr(value, 0, (size_t)limit) ? value : NULL;
}

static void maximum(goat_native_version *value, const goat_native_version *other) {
    if (!other->known)
        return;
    bool replace = !value->known;
    for (size_t i = 0; !replace && i < 4; i++) {
        if (other->components[i] != value->components[i]) {
            replace = other->components[i] > value->components[i];
            break;
        }
    }
    if (replace)
        *value = *other;
}

static bool version(const char *text, goat_native_version *out) {
    goat_native_version result = {.known = true};
    for (size_t i = 0; i < 4; i++) {
        if (*text < '0' || *text > '9')
            return false;
        uint32_t value = 0;
        do {
            unsigned digit = (unsigned)(*text++ - '0');
            if (value > (UINT32_MAX - digit) / 10)
                return false;
            value = value * 10 + digit;
        } while (*text >= '0' && *text <= '9');
        result.components[i] = value;
        if (!*text) {
            *out = result;
            return true;
        }
        if (*text++ != '.')
            return false;
    }
    return false;
}

static bool failure(inspection_t *state, const char *error) {
    state->error = error;
    return false;
}

static bool inspect_path(inspection_t *state, const char *path, bool private_abi);

static bool notes(inspection_t *state, image_t image, uint64_t offset, uint64_t size) {
    if (!range(image, offset, size))
        return failure(state, "Invalid ELF note range");
    uint64_t end = offset + size;
    while (offset < end) {
        if (end - offset < 12)
            return failure(state, "Truncated ELF note");
        uint64_t name = number(image, offset, 4), data = number(image, offset + 4, 4);
        uint64_t type = number(image, offset + 8, 4);
        uint64_t names = (name + 3) & ~UINT64_C(3), bytes = (data + 3) & ~UINT64_C(3);
        offset += 12;
        if (names > end - offset || bytes > end - offset - names)
            return failure(state, "Invalid ELF note length");
        if (type == 1 && name == 4 && data == 16 && memcmp(image.data + offset, "GNU", 4) == 0) {
            uint64_t desc = offset + names;
            if (number(image, desc, 4) != 0)
                return failure(state, "Non-Linux ELF ABI note");
            goat_native_version requirement = {.known = true};
            for (size_t i = 0; i < 3; i++)
                requirement.components[i] = (uint32_t)number(image, desc + 4 + i * 4, 4);
            maximum(&state->value.os_version, &requirement);
        }
        offset += names + bytes;
    }
    return true;
}

static bool elf(inspection_t *state, image_t image, bool private_abi) {
    if (image.size < 64 || image.data[5] != 1 || image.data[6] != 1
        || (image.data[4] != 1 && image.data[4] != 2))
        return failure(state, "Unsupported ELF encoding");
    bool wide = image.data[4] == 2;
    unsigned word = wide ? 8 : 4;
    uint64_t machine = number(image, 18, 2);
    goat_native_arch arch = machine == 62    ? GOAT_ARCH_X86_64
                            : machine == 183 ? GOAT_ARCH_AARCH64
                            : machine == 3   ? GOAT_ARCH_X86
                                             : GOAT_ARCH_UNKNOWN;
    if (state->value.os != GOAT_OS_LINUX || state->value.architecture != arch
        || state->value.bits != (wide ? 64u : 32u) || state->value.runtime != GOAT_RUNTIME_GLIBC
        || number(image, 16, 2) != 3)
        return failure(state, "ELF target does not match selected compiler target");
    uint64_t sections = number(image, wide ? 40 : 32, word);
    uint64_t stride = number(image, wide ? 58 : 46, 2);
    uint64_t count = number(image, wide ? 60 : 48, 2);
    if (stride < (wide ? 64u : 40u) || !count || !range(image, sections, stride * count))
        return failure(state, "ELF section metadata is unavailable");
    bool has_dynamic = false;
    for (uint64_t i = 0; i < count; i++) {
        uint64_t section = sections + i * stride;
        uint64_t type = number(image, section + 4, 4);
        uint64_t offset = number(image, section + (wide ? 24 : 16), word);
        uint64_t size = number(image, section + (wide ? 32 : 20), word);
        if (type == 7 && !notes(state, image, offset, size))
            return false;
        if (type != 6 && type != 0x6ffffffe)
            continue;
        uint64_t link = number(image, section + (wide ? 40 : 24), 4);
        if (link >= count || !range(image, offset, size))
            return failure(state, "Invalid ELF dynamic metadata");
        uint64_t strings = sections + link * stride;
        uint64_t base = number(image, strings + (wide ? 24 : 16), word);
        uint64_t length = number(image, strings + (wide ? 32 : 20), word);
        if (!range(image, base, length))
            return failure(state, "Invalid ELF string table");
        if (type == 6) {
            has_dynamic = true;
            if (size % (word * 2))
                return failure(state, "Invalid ELF dynamic table size");
            for (uint64_t p = offset; p < offset + size; p += word * 2) {
                uint64_t tag = number(image, p, word), index = number(image, p + word, word);
                if (tag == 0)
                    break;
                if (tag == 15 || tag == 29 || tag == 0x7ffffffd || tag == 0x7fffffff)
                    return failure(state, "ELF dependency search overrides are unsupported");
                if (tag != 1)
                    continue;
                const char *name =
                    index < length ? string_at(image, base + index, length - index) : NULL;
                if (!name || !*name || strchr(name, '/') || !state->resolver)
                    return failure(state, "ELF dependency cannot be resolved");
                char *path = state->resolver(name, state->context);
                if (!path)
                    return failure(state,
                                   "ELF dependency is unavailable in the selected toolchain");
                bool private_dependency = !strcmp(name, "libc.so.6") || !strcmp(name, "libm.so.6")
                                          || !strcmp(name, "ld-linux-x86-64.so.2")
                                          || !strcmp(name, "ld-linux-aarch64.so.1")
                                          || !strcmp(name, "ld-linux.so.2");
                bool ok = inspect_path(state, path, private_dependency);
                FREE(path);
                if (!ok)
                    return false;
            }
        } else {
            uint64_t p = offset, end = offset + size;
            while (p < end) {
                if (end - p < 16 || number(image, p, 2) != 1)
                    return failure(state, "Invalid ELF version requirements");
                uint64_t aux = number(image, p + 8, 4), n = number(image, p + 2, 2);
                if (aux > end - p)
                    return failure(state, "Invalid ELF version auxiliary offset");
                uint64_t a = p + aux;
                for (uint64_t j = 0; j < n; j++) {
                    if (end - a < 16)
                        return failure(state, "Truncated ELF version auxiliary record");
                    uint64_t index = number(image, a + 8, 4);
                    const char *name =
                        index < length ? string_at(image, base + index, length - index) : NULL;
                    if (!name)
                        return failure(state, "Invalid ELF version name");
                    if (strncmp(name, "GLIBC_", 6) == 0) {
                        goat_native_version requirement;
                        if (!strcmp(name, "GLIBC_PRIVATE") && private_abi) {
                            /* Private glibc/loader coupling stays inside the resolved closure. */
                        } else if (!strcmp(name, "GLIBC_ABI_DT_RELR")) {
                            /* glibc 2.36 introduced DT_RELR; this is a versioned ABI
                             * dependency, not a numeric symbol version. */
                            requirement =
                                (goat_native_version){.components = {2, 36, 0, 0}, .known = true};
                            maximum(&state->value.runtime_version, &requirement);
                        } else if (!version(name + 6, &requirement)) {
                            return failure(state, "Unknown ELF glibc requirement");
                        } else {
                            maximum(&state->value.runtime_version, &requirement);
                        }
                    } else {
                        return failure(state, "Unsupported versioned native dependency");
                    }
                    uint64_t next = number(image, a + 12, 4);
                    if (j + 1 < n && (!next || next > end - a))
                        return failure(state, "Invalid ELF version auxiliary chain");
                    a += next;
                }
                uint64_t next = number(image, p + 12, 4);
                if (!next)
                    break;
                if (next > end - p)
                    return failure(state, "Invalid ELF version chain");
                p += next;
            }
        }
    }
    return has_dynamic || failure(state, "ELF image has no dynamic table");
}

static uint64_t
pe_offset(image_t image, uint64_t table, uint64_t count, uint64_t rva, uint64_t length) {
    for (uint64_t i = 0; i < count; i++) {
        uint64_t section = table + i * 40;
        uint64_t address = number(image, section + 12, 4);
        uint64_t size = number(image, section + 16, 4);
        uint64_t offset = number(image, section + 20, 4);
        if (rva >= address && rva - address <= size && length <= size - (rva - address)
            && range(image, offset + rva - address, length))
            return offset + rva - address;
    }
    return UINT64_MAX;
}

static bool equal_ascii(const char *a, const char *b) {
    while (*a && *b) {
        char x = *a++, y = *b++;
        if (x >= 'A' && x <= 'Z')
            x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z')
            y += 'a' - 'A';
        if (x != y)
            return false;
    }
    return !*a && !*b;
}

/** @brief Imports known to exist in the Windows 10 system CRT profile.
 * Unknown or ordinal imports require a new audited profile, not a guessed floor.
 */
static bool known_pe_symbol(const char *name, bool kernel) {
    static const char *const kernel_symbols[] = {"CloseHandle",
                                                 "DeleteCriticalSection",
                                                 "EnterCriticalSection",
                                                 "ExitProcess",
                                                 "FreeLibrary",
                                                 "GetCPInfo",
                                                 "GetCommandLineA",
                                                 "GetCommandLineW",
                                                 "GetCurrentProcess",
                                                 "GetCurrentProcessId",
                                                 "GetCurrentThreadId",
                                                 "GetLastError",
                                                 "GetModuleHandleA",
                                                 "GetModuleHandleW",
                                                 "GetProcAddress",
                                                 "GetProcessHeap",
                                                 "GetStartupInfoA",
                                                 "GetStartupInfoW",
                                                 "GetStringTypeW",
                                                 "GetSystemTimeAsFileTime",
                                                 "GetSystemTimePreciseAsFileTime",
                                                 "GetTickCount",
                                                 "HeapAlloc",
                                                 "HeapFree",
                                                 "InitializeCriticalSection",
                                                 "InitializeCriticalSectionAndSpinCount",
                                                 "IsDebuggerPresent",
                                                 "IsProcessorFeaturePresent",
                                                 "LeaveCriticalSection",
                                                 "LoadLibraryA",
                                                 "LoadLibraryW",
                                                 "MultiByteToWideChar",
                                                 "QueryPerformanceCounter",
                                                 "RtlAddFunctionTable",
                                                 "RtlDeleteFunctionTable",
                                                 "RtlLookupFunctionEntry",
                                                 "RtlVirtualUnwind",
                                                 "SetLastError",
                                                 "SetUnhandledExceptionFilter",
                                                 "Sleep",
                                                 "TerminateProcess",
                                                 "TlsAlloc",
                                                 "TlsFree",
                                                 "TlsGetValue",
                                                 "TlsSetValue",
                                                 "UnhandledExceptionFilter",
                                                 "VirtualProtect",
                                                 "VirtualQuery",
                                                 "WideCharToMultiByte",
                                                 NULL};
    static const char *const crt_symbols[] = {"__acrt_iob_func",
                                              "__dllonexit",
                                              "__iob_func",
                                              "__p___argc",
                                              "__p___argv",
                                              "__p__commode",
                                              "__p__fmode",
                                              "__p__iob",
                                              "__p__environ",
                                              "__p__wcmdln",
                                              "__setusermatherr",
                                              "__stdio_common_vfprintf",
                                              "__stdio_common_vsprintf",
                                              "__stdio_common_vsscanf",
                                              "_amsg_exit",
                                              "_beginthreadex",
                                              "_cexit",
                                              "_commode",
                                              "_configure_narrow_argv",
                                              "_create_locale",
                                              "_crt_atexit",
                                              "_endthreadex",
                                              "_errno",
                                              "_execute_onexit_table",
                                              "_exit",
                                              "_fmode",
                                              "_fpreset",
                                              "_free_locale",
                                              "_get_initial_narrow_environment",
                                              "_initialize_narrow_environment",
                                              "_initialize_onexit_table",
                                              "_initterm",
                                              "_initterm_e",
                                              "_iob",
                                              "_lock",
                                              "_onexit",
                                              "_register_onexit_function",
                                              "_set_app_type",
                                              "_strtod_l",
                                              "_unlock",
                                              "_vsnprintf_l",
                                              "abort",
                                              "acos",
                                              "acosh",
                                              "asin",
                                              "asinh",
                                              "atan",
                                              "atan2",
                                              "atanh",
                                              "atexit",
                                              "calloc",
                                              "cbrt",
                                              "ceil",
                                              "cos",
                                              "cosh",
                                              "erf",
                                              "erfc",
                                              "exit",
                                              "exp",
                                              "exp2",
                                              "expm1",
                                              "fabs",
                                              "fclose",
                                              "fflush",
                                              "floor",
                                              "fma",
                                              "fmax",
                                              "fmin",
                                              "fmod",
                                              "fopen",
                                              "fprintf",
                                              "fputc",
                                              "fputs",
                                              "fread",
                                              "free",
                                              "frexp",
                                              "fwrite",
                                              "hypot",
                                              "ldexp",
                                              "lgamma",
                                              "log",
                                              "log10",
                                              "log1p",
                                              "log2",
                                              "malloc",
                                              "memcmp",
                                              "memcpy",
                                              "memmove",
                                              "memset",
                                              "modf",
                                              "nearbyint",
                                              "nextafter",
                                              "pow",
                                              "printf",
                                              "puts",
                                              "qsort",
                                              "realloc",
                                              "remainder",
                                              "rint",
                                              "round",
                                              "setlocale",
                                              "sin",
                                              "sinh",
                                              "snprintf",
                                              "sprintf",
                                              "sqrt",
                                              "strcmp",
                                              "strcpy",
                                              "strerror",
                                              "strlen",
                                              "strncmp",
                                              "strncpy",
                                              "strtod",
                                              "strtol",
                                              "strtoll",
                                              "strtoul",
                                              "strtoull",
                                              "tan",
                                              "tanh",
                                              "tgamma",
                                              "trunc",
                                              "vfprintf",
                                              "vsnprintf",
                                              "wcslen",
                                              NULL};
    const char *const *symbols = kernel ? kernel_symbols : crt_symbols;
    for (size_t i = 0; symbols[i]; i++)
        if (!strcmp(name, symbols[i]))
            return true;
    return false;
}

static bool pe(inspection_t *state, image_t image) {
    uint64_t header = number(image, 60, 4);
    if (!range(image, header, 24) || memcmp(image.data + header, "PE\0\0", 4))
        return failure(state, "Invalid PE signature");
    uint64_t machine = number(image, header + 4, 2);
    goat_native_arch arch = machine == 0x8664  ? GOAT_ARCH_X86_64
                            : machine == 0x14c ? GOAT_ARCH_X86
                                               : GOAT_ARCH_UNKNOWN;
    uint64_t count = number(image, header + 6, 2);
    uint64_t optional_size = number(image, header + 20, 2), optional = header + 24;
    bool wide = number(image, optional, 2) == 0x20b;
    if (state->value.os != GOAT_OS_WINDOWS || state->value.architecture != arch
        || state->value.bits != (wide ? 64u : 32u) || (number(image, optional, 2) != 0x10b && !wide)
        || !(number(image, header + 22, 2) & 0x2000))
        return failure(state, "PE target does not match selected compiler target");
    uint64_t dirs = optional + (wide ? 112 : 96), table = optional + optional_size;
    if (optional_size < (wide ? 112u : 96u) + 14 * 8 || !range(image, optional, optional_size)
        || !range(image, table, count * 40) || number(image, optional + (wide ? 108 : 92), 4) < 14)
        return failure(state, "Incomplete PE metadata");
    if (number(image, dirs + 13 * 8, 4))
        return failure(state, "Delay-loaded dependencies have unknown native requirements");
    goat_native_version os = {.components = {10, 0, 0, 0}, .known = true};
    goat_native_version header_os = {.components = {(uint32_t)number(image, optional + 40, 2),
                                                    (uint32_t)number(image, optional + 42, 2)},
                                     .known = true};
    goat_native_version subsystem = {.components = {(uint32_t)number(image, optional + 48, 2),
                                                    (uint32_t)number(image, optional + 50, 2)},
                                     .known = true};
    maximum(&os, &header_os);
    maximum(&os, &subsystem);
    maximum(&state->value.os_version, &os);
    uint64_t imports = number(image, dirs + 8, 4), size = number(image, dirs + 12, 4);
    uint64_t offset = pe_offset(image, table, count, imports, size);
    if (!imports || size < 20 || offset == UINT64_MAX)
        return failure(state, "PE runtime import metadata is unavailable");
    bool crt = false, terminated = false, unsupported = false, truncated = false;
    size_t detail_size = 0;
    for (uint64_t i = 0; i + 20 <= size; i += 20) {
        uint64_t entry = offset + i;
        uint64_t name_rva = number(image, entry + 12, 4);
        if (!name_rva) {
            terminated = true;
            break;
        }
        uint64_t name_offset = pe_offset(image, table, count, name_rva, 1);
        const char *name = name_offset == UINT64_MAX
                               ? NULL
                               : string_at(image, name_offset, image.size - name_offset);
        if (!name)
            return failure(state, "Invalid PE import name");
        bool msvcrt = equal_ascii(name, "msvcrt.dll");
        bool ucrt = equal_ascii(name, "ucrtbase.dll")
                    || (strlen(name) > 15 && strncmp(name, "api-ms-win-crt-", 14) == 0);
        if (msvcrt || ucrt) {
            goat_native_runtime runtime = msvcrt ? GOAT_RUNTIME_MSVCRT : GOAT_RUNTIME_UCRT;
            if (state->value.runtime != runtime)
                return failure(state, "PE CRT imports disagree with selected target");
            crt = true;
        } else if (!equal_ascii(name, "kernel32.dll")) {
            return failure(state, "PE dependency is outside the supported Windows 10 CRT profile");
        }
        uint64_t thunk = number(image, entry, 4);
        if (!thunk)
            thunk = number(image, entry + 16, 4);
        if (!thunk)
            return failure(state, "PE import symbol table is unavailable");
        bool symbols_end = false;
        unsigned width = wide ? 8 : 4;
        for (size_t n = 0; n < 65536; n++) {
            uint64_t location = pe_offset(image, table, count, thunk + n * width, width);
            if (location == UINT64_MAX)
                return failure(state, "Invalid PE import symbol table");
            uint64_t symbol = number(image, location, width);
            if (!symbol) {
                symbols_end = true;
                break;
            }
            if (symbol & (UINT64_C(1) << (width * 8 - 1)))
                return failure(state, "Ordinal PE imports have unknown requirements");
            location = pe_offset(image, table, count, symbol, 3);
            const char *function = location == UINT64_MAX
                                       ? NULL
                                       : string_at(image, location + 2, image.size - location - 2);
            if (!function)
                return failure(state, "Invalid PE import symbol name");
            if (!known_pe_symbol(function, !msvcrt && !ucrt)) {
                if (!unsupported) {
                    detail_size = (size_t)snprintf(
                        state->detail,
                        sizeof(state->detail),
                        "PE import symbols are outside the Windows 10 CRT profile: ");
                    unsupported = true;
                }
                /* Retain a bounded inventory, not just the first missing profile entry. */
                if (!truncated) {
                    size_t remaining = sizeof(state->detail) - detail_size;
                    int written = snprintf(state->detail + detail_size,
                                           remaining - 32,
                                           "%s!%s; ",
                                           name,
                                           function);
                    if (written < 0 || (size_t)written >= remaining - 32) {
                        detail_size += strlen(state->detail + detail_size);
                        snprintf(state->detail + detail_size,
                                 sizeof(state->detail) - detail_size,
                                 "... [inventory truncated]");
                        truncated = true;
                    } else {
                        detail_size += (size_t)written;
                    }
                }
            }
        }
        if (!symbols_end)
            return failure(state, "Unterminated PE import symbol table");
    }
    if (!terminated || !crt)
        return failure(state, "PE CRT identity cannot be established");
    if (unsupported)
        return failure(state, state->detail);
    /* The audited import profile is supplied by the Windows 10 OS/family contract.
     * No additional CRT file-version floor is required: MSVCRT file versions do
     * not track Windows major versions. Zero is the schema's explicit no-extra-floor. */
    state->value.runtime_version = (goat_native_version){.known = true};
    return true;
}

static bool inspect_path(inspection_t *state, const char *path, bool private_abi) {
    for (size_t i = 0; i < state->count; i++)
        if (!strcmp(path, state->paths[i]))
            return true;
    if (state->count == DEPENDENCY_LIMIT)
        return failure(state, "Native dependency closure is too large");
    size_t length = strlen(path);
    char *copy = ALLOC(length + 1);
    memcpy(copy, path, length + 1);
    state->paths[state->count++] = copy;
    size_t size;
    void *bytes = read_binary_file(path, IMAGE_LIMIT, &size);
    if (!bytes)
        return failure(state, "Native image cannot be read");
    image_t image = {bytes, size};
    bool ok = false;
    if (size >= 4 && !memcmp(bytes, "\177ELF", 4))
        ok = elf(state, image, private_abi);
    else if (size >= 64 && !memcmp(bytes, "MZ", 2))
        ok = pe(state, image);
    else
        failure(state, "Unknown native image format");
    FREE(bytes);
    return ok;
}

bool inspect_native_requirements(const char *path,
                                 const goat_native_environment *expected,
                                 native_dependency_resolver_t resolver,
                                 void *context,
                                 goat_native_environment *out,
                                 char **diagnostic) {
    if (out)
        memset(out, 0, sizeof(*out));
    if (diagnostic)
        *diagnostic = NULL;
    if (!path || !expected || !out || !diagnostic)
        return false;
    inspection_t state = {.value = *expected, .resolver = resolver, .context = context};
    memset(&state.value.os_version, 0, sizeof(state.value.os_version));
    memset(&state.value.runtime_version, 0, sizeof(state.value.runtime_version));
    bool ok = expected->little_endian && inspect_path(&state, path, false);
    if (ok && (!state.value.os_version.known || !state.value.runtime_version.known))
        ok = failure(&state, "Native OS or runtime requirement cannot be established");
    if (ok)
        *out = state.value;
    else {
        const char *error = state.error ? state.error : "Unsupported native byte order";
        size_t length = strlen(error);
        *diagnostic = ALLOC(length + 1);
        memcpy(*diagnostic, error, length + 1);
    }
    for (size_t i = 0; i < state.count; i++)
        FREE(state.paths[i]);
    return ok;
}
