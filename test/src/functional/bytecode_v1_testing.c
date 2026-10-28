/** @file bytecode_v1_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent standalone bytecode producer, mutations and real VM/writer checks.
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/format_binary.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void put(unsigned char *p, uint32_t n) {
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (unsigned char)(n >> (i * 8));
}

static uint32_t get(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t get64(const unsigned char *p) {
    return (uint64_t)get(p) | (uint64_t)get(p + 4) << 32;
}

static void words(unsigned char *p, const uint32_t *values, size_t count) {
    for (size_t i = 0; i < count; ++i)
        put(p + i * 4, values[i]);
}

static void emit(unsigned char *p, size_t *size, unsigned op, uint32_t b, unsigned a) {
    p[*size] = (unsigned char)op;
    p[*size + 2] = (unsigned char)a;
    p[*size + 3] = (unsigned char)(a >> 8);
    put(p + *size + 4, b);
    *size += 8;
}

static void print_top(unsigned char *p, size_t *size) {
    emit(p, size, 15, 0, 0);
    emit(p, size, 48, 0, 1);
    emit(p, size, 5, 0, 0);
}

/* Numeric opcodes and explicit little-endian fields intentionally do not use
 * production encoding functions, enum values, or native Unicode conversions. */
static size_t fixture(unsigned char out[1024]) {
    unsigned char payload[128] = {0}, descriptors[64] = {0}, instructions[512] = {0};
    const uint32_t strings[][7] = {{'p', 'r', 'i', 'n', 't', 'l', 'n'},
                                   {'A', 0, 0x416, 0x1f600},
                                   {'x'}};
    const unsigned lengths[] = {7, 4, 1};
    size_t np = 0, ni_bytes = 0;
    memset(out, 0, 1024);
    for (unsigned i = 0; i < 3; ++i) {
        np = (np + 7) & ~(size_t)7;
        const uint32_t d[] = {(uint32_t)np, (lengths[i] + 1) * 4, 1, lengths[i]};
        words(descriptors + i * 16, d, 4);
        words(payload + np, strings[i], lengths[i]);
        np += (lengths[i] + 1) * 4;
    }
    np = (np + 7) & ~(size_t)7;
    const uint32_t parameter[] = {(uint32_t)np, 4, 2, 1};
    words(descriptors + 48, parameter, 4);
    put(payload + np, 2);
    np += 4;
    const uint64_t bits[] = {UINT64_C(0x8000000000000000),
                             UINT64_C(0x7fffffffffffffff),
                             UINT64_C(0x8000000000000000),
                             UINT64_C(0x7ff0000000000000),
                             UINT64_C(0x7ff8000000000042)};
    for (unsigned i = 0; i < 5; ++i) {
        emit(instructions, &ni_bytes, 1, (uint32_t)bits[i], 0);
        emit(instructions, &ni_bytes, i < 2 ? 11 : 12, (uint32_t)(bits[i] >> 32), 0);
        print_top(instructions, &ni_bytes);
    }
    emit(instructions, &ni_bytes, 13, 1, 0);
    print_top(instructions, &ni_bytes);
    uint32_t creation = (uint32_t)(ni_bytes / 8) + 1, body = creation + 3;
    emit(instructions, &ni_bytes, 1, body, 0);
    emit(instructions, &ni_bytes, 47, 3, 1);
    emit(instructions, &ni_bytes, 5, 0, 0);
    emit(instructions, &ni_bytes, 2, 0, 0);
    emit(instructions, &ni_bytes, 7, 0, 0);
    emit(instructions, &ni_bytes, 50, 0, 0);
    uint32_t ni = (uint32_t)(ni_bytes / 8), od = 64 + (uint32_t)ni_bytes;
    uint32_t of = od + 64, on = of + 16, ol = on + 32, start = ol + 48;
    const uint32_t h[] = {1, 64, start + (uint32_t)np, ni, 4, 1, 1, 2, 64, od, of, on, ol, start};
    const uint32_t f[] = {0, ni, 0, 0}, n[] = {7, 0, creation, body, 3, 1, UINT32_MAX, 0};
    const uint32_t l[] = {0, body, 0, 1, 1, 0, body, 2, 0, 2, 1, 7};
    memcpy(out, "GOATBC01", 8);
    words(out + 8, h, 14);
    memcpy(out + 64, instructions, ni_bytes);
    memcpy(out + od, descriptors, sizeof(descriptors));
    words(out + of, f, 4);
    words(out + on, n, 8);
    words(out + ol, l, 12);
    memcpy(out + start, payload, np);
    return start + np;
}

static unsigned digit(scenario_run_t *run, unsigned char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    scenario_require(run, false, "invalid hex fixture digit");
    return 0;
}

static unsigned char *read_hex(scenario_run_t *run, const char *name, size_t *size) {
    const char *path = language_join(run, scenario_data_directory(scenario_run_context(run)), name);
    size_t n;
    const char *text = language_read(run, path, &n);
    unsigned char *bytes = language_owned(run, ALLOC(n / 2 + 1));
    bool high = true;
    unsigned value = 0;
    *size = 0;
    for (size_t i = 0; i < n; ++i) {
        if (isspace((unsigned char)text[i]))
            continue;
        unsigned d = digit(run, (unsigned char)text[i]);
        if (high)
            value = d << 4;
        else
            bytes[(*size)++] = (unsigned char)(value | d);
        high = !high;
    }
    scenario_require(run, high, "odd hex fixture length");
    return bytes;
}

static bool release_malloc(void *p) {
    free(p);
    return true;
}

static void oracle(scenario_run_t *run, const unsigned char *data, size_t size) {
    char error[256] = {0};
    bool ok = format_binary_check(data, size, error, sizeof(error));
    scenario_require(run, ok, error);
}

static void execute(scenario_run_t *run,
                    const char *exe,
                    const unsigned char *data,
                    size_t size,
                    bool valid,
                    const char *expected,
                    size_t expected_size) {
    const char *path =
        language_join(run, scenario_work_directory(scenario_run_context(run)), "image.gbin");
    language_write(run, path, data, size);
    scenario_command_result_t result = language_command(
        run,
        exe,
        (const char *const[]){exe, "--lang", "en", "--run", "--native", "off", path, NULL},
        NULL);
    scenario_require(run, (result.exit_code == 0) == valid, "bytecode acceptance mismatch");
    if (!valid) {
        const char diagnostic[] = "Invalid, incompatible or unreadable Goat binary";
        bool found = false;
        for (size_t i = 0; i + sizeof(diagnostic) - 1 <= result.err_size; ++i)
            if (!memcmp((const char *)result.err + i, diagnostic, sizeof(diagnostic) - 1))
                found = true;
        scenario_require(run, found, "missing invalid bytecode diagnostic");
    }
    scenario_require_bytes(run,
                           "bytecode stdout",
                           expected,
                           expected_size,
                           result.out,
                           result.out_size,
                           true);
    size_t after_size;
    const char *after = language_read(run, path, &after_size);
    scenario_require_bytes(run, "unchanged bytecode input", data, size, after, after_size, false);
}

static void bad(scenario_run_t *run,
                const char *exe,
                const unsigned char *image,
                size_t size,
                size_t offset,
                uint32_t value,
                unsigned width,
                unsigned *count) {
    unsigned char copy[1024];
    scenario_require(run,
                     size <= sizeof(copy) && offset <= size && width <= size - offset,
                     "invalid mutation bounds");
    memcpy(copy, image, size);
    for (unsigned i = 0; i < width; ++i)
        copy[offset + i] = (unsigned char)(value >> (i * 8));
    execute(run, exe, copy, size, false, "", 0);
    ++*count;
}

static void bytecode_body(scenario_run_t *run, void *user) {
    const char *exe = user;
    unsigned char image[1024], copy[1024];
    size_t size = fixture(image), frozen_size, minimal_size;
    unsigned char *frozen = read_hex(run, "binary-v1/scalars.hex", &frozen_size);
    scenario_require_bytes(run,
                           "independent scalar fixture",
                           image,
                           size,
                           frozen,
                           frozen_size,
                           false);
    static const char expected[] =
        "-9223372036854775808\n9223372036854775807\n-0\ninf\nnan\nA\0\xd0\x96\xf0\x9f\x98\x80\n";
    execute(run, exe, image, size, true, expected, sizeof(expected) - 1);
    unsigned char *minimal = format_binary_minimal(&minimal_size);
    scenario_defer(run, release_malloc, minimal);
    scenario_require(run, minimal != NULL, "cannot allocate minimal oracle fixture");
    frozen = read_hex(run, "binary-v1/empty.bytecode.hex", &frozen_size);
    scenario_require_bytes(run,
                           "independent empty fixture",
                           minimal,
                           minimal_size,
                           frozen,
                           frozen_size,
                           false);
    execute(run, exe, minimal, minimal_size, true, "", 0);
    uint32_t od = get(image + 44), of = get(image + 48), on = get(image + 52);
    uint32_t ol = get(image + 56), payload = get(image + 60);
    unsigned corruptions = 0;
    const char *magics[] = {"GOATBIN1", "GOATBI10", "GOATBI11", "goat v.1", "GOATSC01", "GOATST01"};
    for (unsigned i = 0; i < 6; ++i) {
        memcpy(copy, image, size);
        memcpy(copy, magics[i], 8);
        execute(run, exe, copy, size, false, "", 0);
        ++corruptions;
    }
#define BAD(o, v) bad(run, exe, image, size, o, v, 4, &corruptions)
    for (size_t offset = 8; offset < 64; offset += 4)
        BAD(offset, UINT32_MAX);
    const size_t truncations[] = {0, 7, 31, 63, 64, size - 1};
    for (unsigned i = 0; i < 6; ++i) {
        execute(run, exe, image, truncations[i], false, "", 0);
        ++corruptions;
    }
    memcpy(copy, image, size);
    copy[size] = 0;
    execute(run, exe, copy, size + 1, false, "", 0);
    ++corruptions;
    bad(run, exe, image, size, 64, 255, 1, &corruptions);
    bad(run, exe, image, size, 65, 1, 1, &corruptions);
    bad(run, exe, image, size, 66, 1, 2, &corruptions);
    bad(run, exe, image, size, 64, 3, 1, &corruptions);
    BAD(of + 8, 1); /* Jump to an ARG consumer. */
    BAD(od, 1);
    BAD(od + 4, UINT32_MAX);
    BAD(od + 8, 0);
    BAD(od + 12, UINT32_MAX);
    BAD(od + 16, 0); /* Aliased descriptors. */
    BAD(payload, 0xd800);
    BAD(payload, 0x110000);
    BAD(payload + 7 * 4, 1);
    BAD(payload + get(image + od + 3 * 16), 3); /* Self-referencing parameter vector. */
    const size_t offsets[] = {of,
                              of + 4,
                              of + 8,
                              of + 12,
                              on,
                              on + 4,
                              on + 8,
                              on + 12,
                              on + 16,
                              on + 20,
                              on + 28,
                              ol,
                              ol + 4,
                              ol + 8,
                              ol + 20};
    for (unsigned i = 0; i < sizeof(offsets) / sizeof(*offsets); ++i)
        BAD(offsets[i], UINT32_MAX);
    BAD(ol + 12, 0); /* Mixed synthetic and real location. */
    BAD(on + 24, 3); /* Name must be a string. */
    BAD(on + 8, 1);
    BAD(on + 12, 1);                   /* Constant creation / ARG consumer body. */
    BAD(ol + 24 + 20, 8);              /* Missing function ID. */
    BAD(of + 8, get(image + on + 12)); /* File entry inside a function. */
    BAD(ol + 24 + 20, 0);              /* Lost function identity. */
#undef BAD
    scenario_require(run, corruptions == 63, "corruption coverage count changed");
    memcpy(copy, image, size);
    put(copy + on, UINT32_MAX);
    put(copy + ol + 24 + 20, UINT32_MAX);
    execute(run, exe, copy, size, true, expected, sizeof(expected) - 1);
    oracle(run, image, size);
    const char *work = scenario_work_directory(scenario_run_context(run));
    const char *source = language_join(run, work, "empty.goat");
    language_write(run, source, "", 0);
    scenario_command_result_t result =
        language_command(run, exe, (const char *const[]){exe, "--compile", source, NULL}, NULL);
    scenario_require(run, result.exit_code == 0, "empty compilation failed");
    size_t artifact_size;
    const unsigned char *artifact =
        (const unsigned char *)language_read(run,
                                             language_join(run, work, "empty.gbin"),
                                             &artifact_size);
    scenario_require(run, artifact_size >= 96, "truncated empty envelope");
    put(minimal + 100, 0);
    put(minimal + 104, 0);
    scenario_require_bytes(run,
                           "empty writer",
                           minimal,
                           minimal_size,
                           artifact + 96,
                           artifact_size - 96,
                           false);
    source = language_join(run, work, "roundtrip.goat");
    const char program[] = "const outer=func(n){const inner=func(x){return x+1;};return inner(n);};"
                           "const unused=func(){return 99;};println(outer(41));";
    language_write(run, source, program, sizeof(program) - 1);
    const char *optimizations[] = {"none", "all"};
    for (unsigned i = 0; i < 2; ++i) {
        result = language_command(
            run,
            exe,
            (const char *const[]){exe, "--compile", "--optimize", optimizations[i], source, NULL},
            NULL);
        scenario_require(run, result.exit_code == 0, "nested function compilation failed");
        artifact = (const unsigned char *)language_read(run,
                                                        language_join(run, work, "roundtrip.gbin"),
                                                        &artifact_size);
        scenario_require(run, artifact_size >= 96, "truncated standalone envelope");
        scenario_require(run,
                         !memcmp(artifact, "GOATPG01", 8) && get64(artifact + 16) == 0,
                         "incorrect standalone envelope or flags");
        uint64_t code_size = get64(artifact + 24);
        scenario_require(run, code_size <= artifact_size - 96, "bytecode extends beyond envelope");
        oracle(run, artifact + 96, (size_t)code_size);
        execute(run, exe, artifact + 96, (size_t)code_size, true, "42\n", 3);
    }
    printf("[ ok ] Bytecode v1: independent numeric/string/function image, %u corruptions, shared "
           "codec\n",
           corruptions);
}

int main(int argc, char **argv) {
#ifdef _WIN32
    fs_error_t arguments_error = {0};
    if (!fs_arguments(&argc, &argv, &arguments_error)) {
        clear_fs_error(&arguments_error);
        return 1;
    }
#endif
    int result = 1;
    if (argc == 2 && !strcmp(argv[1], "--list")) {
        puts("native.bytecode_v1");
        result = 0;
    } else if (argc == 3 && !strcmp(argv[1], "--generate")) {
        unsigned char image[1024];
        size_t size = fixture(image);
        FILE *file = FOPEN(argv[2], "wb");
        if (file) {
            bool ok = true;
            for (size_t i = 0; i < size; ++i)
                if (fprintf(file, "%02x", image[i]) < 0)
                    ok = false;
            if (fputc('\n', file) == EOF)
                ok = false;
            if (fclose(file))
                ok = false;
            result = ok ? 0 : 1;
        }
    } else if (argc == 5 || argc == 6) {
        fs_error_t error = {0};
        char *exe = absolute_fs_path(argv[2], &error);
        clear_fs_error(&error);
        if (exe) {
            scenario_definition_t definition = {"native.bytecode_v1",
                                                180000,
                                                bytecode_body,
                                                exe,
                                                NULL};
            scenario_options_t options = {.build_directory = argv[3], .data_directory = argv[4]};
            const char *selection[] = {argv[1], argc == 6 ? argv[5] : NULL};
            result = scenario_registry_main(&definition, 1, argc == 6 ? 2 : 1, selection, &options);
            FREE(exe);
        }
    } else
        fputs("usage: bytecode_v1_testing native.bytecode_v1 GOAT BUILD DATA [--keep-work]\n"
              "       bytecode_v1_testing --generate OUTPUT.hex\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
