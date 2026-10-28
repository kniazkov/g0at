/** @file unicode_cases.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent Unicode bytecode, locale and embedded-NUL scenarios.
 */
#include "test/support/language_execution.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void le32(unsigned char *p, uint32_t n) {
    for (unsigned i = 0; i < 4; i++)
        p[i] = (unsigned char)(n >> (8 * i));
}

static void le64(unsigned char *p, uint64_t n) {
    for (unsigned i = 0; i < 8; i++)
        p[i] = (unsigned char)(n >> (8 * i));
}

static uint64_t checksum(const unsigned char *p, size_t n) {
    uint64_t h = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < n; i++)
        h = (h ^ p[i]) * UINT64_C(1099511628211);
    return h;
}

/* Deliberately independent of production bytecode serializers and Unicode encoders. */
static size_t fixture(unsigned char *out) {
    const uint32_t scalars[][7] = {{0},
                                   {65, 83, 67, 73, 73},
                                   {0x41f, 0x440, 0x438, 0x432, 0x435, 0x442},
                                   {0x1f600},
                                   {101, 0x301},
                                   {0, 65, 0, 66, 0, 0}};
    const unsigned lengths[] = {0, 5, 6, 1, 2, 6};
    const unsigned ni = 13, od = 64 + 13 * 8, of = od + 6 * 16, ol = of + 16, start = ol + 24;
    memset(out, 0, 1024);
    unsigned char *inner = out + 96;
    size_t payload = 0;
    for (unsigned i = 0; i < 6; i++) {
        payload = (payload + 7) & ~(size_t)7;
        unsigned char *d = inner + od + i * 16;
        le32(d, (uint32_t)payload);
        le32(d + 4, (lengths[i] + 1) * 4);
        le32(d + 8, 1);
        le32(d + 12, lengths[i]);
        for (unsigned j = 0; j < lengths[i]; j++)
            le32(inner + start + payload + j * 4, scalars[i][j]);
        payload += (lengths[i] + 1) * 4;
        inner[64 + i * 16] = 13;
        le32(inner + 64 + i * 16 + 4, i);
        inner[64 + i * 16 + 8] = 5;
    }
    inner[64 + 12 * 8] = 2;
    const uint32_t header[] =
        {1, 64, (uint32_t)(start + payload), ni, 6, 1, 0, 1, 64, od, of, ol, ol, start};
    memcpy(inner, "GOATBC01", 8);
    for (unsigned i = 0; i < 14; i++)
        le32(inner + 8 + i * 4, header[i]);
    le32(inner + of + 4, ni);
    le32(inner + ol + 4, ni);
    le32(inner + ol + 12, 1);
    le32(inner + ol + 16, 1);
    memcpy(out, "GOATPG01", 8);
    le64(out + 8, 1);
    le64(out + 24, start + payload);
    size_t n = 96 + start + payload;
    le64(out + 56, checksum(out, n));
    return n;
}

#define CMD(...)                                                                                   \
    language_command(run,                                                                          \
                     exe,                                                                          \
                     (const char *const[]){exe, "--lang", "en", __VA_ARGS__, NULL},                \
                     environment)
#define CHECK(r, e, n) language_check(run, r, 0, e, n, "", 0)

static void accepted(scenario_run_t *run, scenario_command_result_t result) {
    scenario_require(run, result.exit_code == 0, "Unicode command failed");
}

static void rejected(scenario_run_t *run, scenario_command_result_t result) {
    scenario_require(run, result.exit_code != 0, "invalid Unicode input accepted");
}

void unicode_cases(scenario_run_t *run, const char *exe) {
    const char *work = scenario_work_directory(scenario_run_context(run));
    const char *const *environment = language_environment_override(run, "LC_ALL", "C");
    const char *directory = language_join(run, work, "каталог-😀");
    language_directory(run, directory);
    const char *path_source = language_join(run, directory, "программа-𐐨.goat");
    const char *path_binary = language_join(run, directory, "программа-𐐨.gbin");
    const char path_code[] = "println(\"path-ok\");\n";
    language_write(run, path_source, path_code, sizeof(path_code) - 1);
    CHECK(CMD(path_source), "path-ok\n", 8);
    accepted(run, CMD("--compile", path_source));
    CHECK(CMD("--run", path_binary), "path-ok\n", 8);
    language_remove(run, path_binary);
    language_remove(run, path_source);
    language_remove(run, directory);
    unsigned char image[1024], decoded[1024];
    size_t n = fixture(image), size;
    const char *hex =
        language_read(run,
                      language_join(run,
                                    scenario_data_directory(scenario_run_context(run)),
                                    "unicode/scalars.gbin.hex"),
                      &size);
    size_t count = 0;
    int high = -1;
    for (size_t i = 0; i < size; i++) {
        int digit = hex[i] >= '0' && hex[i] <= '9'   ? hex[i] - '0'
                    : hex[i] >= 'a' && hex[i] <= 'f' ? hex[i] - 'a' + 10
                                                     : -1;
        if (digit < 0) {
            scenario_require(run,
                             hex[i] == '\n' || hex[i] == '\r' || hex[i] == ' ' || hex[i] == '\t',
                             "invalid fixture hex");
            continue;
        }
        if (high < 0)
            high = digit;
        else {
            scenario_require(run, count < sizeof(decoded), "fixture too large");
            decoded[count++] = (unsigned char)(high * 16 + digit);
            high = -1;
        }
    }
    scenario_require(run, high < 0, "odd fixture hex");
    scenario_require_bytes(run, "independent Unicode fixture", image, n, decoded, count, false);
    const char *binary = language_join(run, work, "scalars.gbin");
    language_write(run, binary, image, n);
    accepted(run, CMD("--run", binary));
    const char *magics[] = {"GOATBI11", "GOATBIN1", "goat v.1", "GOATST01", "GOATSC01"};
    for (unsigned i = 0; i < 5; i++) {
        memcpy(decoded, image, n);
        memcpy(decoded, magics[i], 8);
        memset(decoded + 56, 0, 8);
        le64(decoded + 56, checksum(decoded, n));
        language_write(run, binary, decoded, n);
        rejected(run, CMD("--run", binary));
    }
    const char *source = language_join(run, work, "unicode.goat");
    const char code[] =
        "const f=func(s){return s.toUpperCase();};\nconst g=func(s){return s.toLowerCase();};\n"
        "println(\"😀\".length);\nprintln(\"A\\0B\".length);\nprintln(\"A\" == "
        "\"A\\0B\");\nprintln(\"A\\0B\".indexOf(\"B\"));\n"
        "println(f(\"ß\\0я𐐨\"));\nprintln(g(\"ΟΣ ΟΣΑ ΟΣ́\\0Σ "
        "İ\"));\nprintln(\"A\\0B\".replace(\"B\", \"Z\"));\nprintln(\"%0\\0%1\".format(\"A\\0B\", "
        "\"C\"));\n";
    const char expected[] = "1\n3\nfalse\n2\nSS\0Я𐐀\nος οσα ος́\0σ i̇\nA\0Z\nA\0B\0C\n";
    language_write(run, source, code, sizeof(code) - 1);
    const char *locales[] = {"C", "C.UTF-8", "tr_TR.UTF-8"};
    for (unsigned l = 0; l < 3; l++) {
        environment = language_environment_override(run, "LC_ALL", locales[l]);
        for (unsigned o = 0; o < 2; o++) {
            const char *opt = o ? "all" : "none";
            CHECK(CMD("--optimize", opt, source), expected, sizeof(expected) - 1);
            accepted(run, CMD("--compile", "--optimize", opt, source));
            CHECK(CMD("--run", language_join(run, work, "unicode.gbin")),
                  expected,
                  sizeof(expected) - 1);
            scenario_command_result_t printed = CMD("--compile", "--print-source-code", source);
            accepted(run, printed);
            const char *restored = language_join(run, work, "regenerated.goat");
            language_write(run, restored, printed.out, printed.out_size);
            CHECK(CMD(restored), expected, sizeof(expected) - 1);
        }
        const char *report = language_join(run, work, "native.report");
        CHECK(CMD("--native", "required", "--save-native", report, source),
              expected,
              sizeof(expected) - 1);
        const char *fields = language_read(run, report, &size);
        /* File reader guarantees a trailing NUL, but do not depend on it here. */
        bool succeeded = false, retries = false;
        for (size_t i = 0; i < size; i++)
            if (i == 0 || fields[i - 1] == '\n') {
                if (size - i >= 10 && !memcmp(fields + i, "succeeded=", 10)) {
                    unsigned value = 0;
                    size_t j = i + 10;
                    for (; j < size && fields[j] >= '0' && fields[j] <= '9'; j++)
                        value = value * 10 + (unsigned)(fields[j] - '0');
                    succeeded = value >= 2;
                }
                if (size - i >= 9 && !memcmp(fields + i, "retries=0", 9)
                    && (size - i == 9 || fields[i + 9] == '\n' || fields[i + 9] == '\r'))
                    retries = true;
            }
        scenario_require(run,
                         succeeded && retries,
                         "native Unicode execution fell back or was absent");
    }
    environment = language_environment_override(run, "LC_ALL", "C");
    const char *raw = language_join(run, work, "raw-nul.goat");
    const char raw_code[] = "println(\"A\0B\".length); println(\"http://a/*b*/\");";
    language_write(run, raw, raw_code, sizeof(raw_code) - 1);
    CHECK(CMD(raw), "3\nhttp://a/*b*/\n", 16);
    const char invalid[] = "println(1);\0println(2);";
    language_write(run, raw, invalid, sizeof(invalid) - 1);
    rejected(run, CMD(raw));
}
