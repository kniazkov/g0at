/** @file test_module_identity.c
 * @brief Portable module identifier and import parser regressions.
 */
#include "test_module_identity.h"

#include "goat/unicode.h"
#include "test_macro.h"

#include <goat/module_identity.h>
#include <stdio.h>
#include <string.h>

/* Exercise both storage types and both import positions without terminators. */
static bool check_identifier(const char *text, size_t length, bool name_ok, bool version_ok) {
    gchar_t wide[257], wide_import[259];
    char import[259];
    ASSERT(length <= 257);
    for (size_t i = 0; i < length; i++)
        wide[i] = (unsigned char)text[i];
    ASSERT(goat_module_name_valid(text, length) == name_ok);
    ASSERT(goat_module_version_valid(text, length) == version_ok);
    ASSERT(goat_module_name_valid_wide(wide, length) == name_ok);
    ASSERT(goat_module_version_valid_wide(wide, length) == version_ok);
    goat_module_import_t parsed = {{text, 9}, {text, 8}, true};
    goat_module_import_wide_t parsed_wide = {{wide, 9}, {wide, 8}, true};
    if (!memchr(text, '@', length)) {
        ASSERT(goat_module_parse_import(text, length, &parsed) == name_ok);
        ASSERT(goat_module_parse_import_wide(wide, length, &parsed_wide) == name_ok);
        ASSERT(!parsed.has_version && !parsed.version.data && !parsed.version.length);
        ASSERT(!parsed_wide.has_version && !parsed_wide.version.data
               && !parsed_wide.version.length);
        ASSERT(parsed.name.data == (name_ok ? text : NULL));
        ASSERT(parsed.name.length == (name_ok ? length : 0));
        ASSERT(parsed_wide.name.data == (name_ok ? wide : NULL));
        ASSERT(parsed_wide.name.length == (name_ok ? length : 0));
    }
    import[0] = 'a';
    import[1] = '@';
    wide_import[0] = U'a';
    wide_import[1] = U'@';
    for (size_t i = 0; i < length; i++) {
        import[i + 2] = text[i];
        wide_import[i + 2] = wide[i];
    }
    ASSERT(goat_module_parse_import(import, length + 2, &parsed) == version_ok);
    ASSERT(goat_module_parse_import_wide(wide_import, length + 2, &parsed_wide) == version_ok);
    ASSERT(parsed.has_version == version_ok && parsed_wide.has_version == version_ok);
    ASSERT(parsed.name.data == (version_ok ? import : NULL));
    ASSERT(parsed.name.length == (version_ok ? 1 : 0));
    ASSERT(parsed.version.data == (version_ok ? import + 2 : NULL));
    ASSERT(parsed.version.length == (version_ok ? length : 0));
    ASSERT(parsed_wide.name.data == (version_ok ? wide_import : NULL));
    ASSERT(parsed_wide.name.length == (version_ok ? 1 : 0));
    ASSERT(parsed_wide.version.data == (version_ok ? wide_import + 2 : NULL));
    ASSERT(parsed_wide.version.length == (version_ok ? length : 0));
    return true;
}

static bool test_identifier_alphabet(void) {
    for (unsigned ch = 0; ch <= 255; ch++) {
        bool alnum = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
        for (size_t position = 0; position < 3; position++) {
            char text[] = {'a', 'a', 'a'};
            text[position] = (char)ch;
            bool ok = alnum || (position != 0 && (ch == '-' || ch == '_'))
                      || (position == 1 && ch == '.');
            ASSERT(check_identifier(text, 3, ok, ok));
        }
    }
    const uint32_t invalid[] = {0x100, 0x430, 0xd800, 0xdc00, 0x1f600, 0x110000, UINT32_MAX};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
        gchar_t text[] = {U'a', (gchar_t)invalid[i], U'a'};
        ASSERT(!goat_module_name_valid_wide(text, 3));
        ASSERT(!goat_module_version_valid_wide(text, 3));
        goat_module_import_wide_t parsed;
        ASSERT(!goat_module_parse_import_wide(text, 3, &parsed));
    }
    return true;
}

static bool test_device_names(void) {
    const char *stems[] = {"con",  "prn",  "aux",  "nul",  "com0", "com1", "com2", "com3",
                           "com4", "com5", "com6", "com7", "com8", "com9", "lpt0", "lpt1",
                           "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    const char *suffixes[] = {"", ".txt", "..txt", ".a.b", "."};
    for (size_t i = 0; i < sizeof(stems) / sizeof(*stems); i++) {
        for (size_t j = 0; j < sizeof(suffixes) / sizeof(*suffixes); j++) {
            char text[32];
            size_t n = strlen(stems[i]), m = strlen(suffixes[j]);
            memcpy(text, stems[i], n);
            memcpy(text + n, suffixes[j], m);
            ASSERT(check_identifier(text, n + m, false, false));
        }
    }
    const char *valid[] = {"conx",
                           "con-",
                           "con_",
                           "com10",
                           "lpt10",
                           "x.con",
                           "x.com1.txt",
                           "a..b",
                           "a-",
                           "a_",
                           "0",
                           "1.02",
                           "1.2",
                           "beta",
                           "latest"};
    for (size_t i = 0; i < sizeof(valid) / sizeof(*valid); i++)
        ASSERT(check_identifier(valid[i], strlen(valid[i]), true, true));
    return true;
}

static bool test_identity_limits(void) {
    char text[257];
    memset(text, 'a', sizeof(text));
    const size_t lengths[] = {0, 1, 249, 250, 251, 254, 255, 256, 257};
    for (size_t i = 0; i < sizeof(lengths) / sizeof(*lengths); i++) {
        size_t n = lengths[i];
        ASSERT(check_identifier(text, n, n > 0 && n <= 255, n > 0 && n <= 250));
    }
    char import[507];
    gchar_t wide[507];
    memset(import, 'a', sizeof(import));
    import[255] = '@';
    for (size_t i = 0; i < sizeof(import); i++)
        wide[i] = (unsigned char)import[i];
    goat_module_import_t parsed;
    goat_module_import_wide_t parsed_wide;
    ASSERT(goat_module_parse_import(import, 506, &parsed));
    ASSERT(parsed.name.data == import && parsed.name.length == 255);
    ASSERT(parsed.version.data == import + 256 && parsed.version.length == 250
           && parsed.has_version);
    ASSERT(goat_module_parse_import_wide(wide, 506, &parsed_wide));
    ASSERT(parsed_wide.name.data == wide && parsed_wide.name.length == 255);
    ASSERT(parsed_wide.version.data == wide + 256 && parsed_wide.version.length == 250
           && parsed_wide.has_version);
    ASSERT(!goat_module_parse_import(import, 507, &parsed));
    ASSERT(!goat_module_parse_import_wide(wide, 507, &parsed_wide));
    /* Oversized lengths must be rejected before reading even the first element. */
    ASSERT(!goat_module_name_valid("", SIZE_MAX));
    ASSERT(!goat_module_version_valid("", SIZE_MAX));
    ASSERT(!goat_module_name_valid_wide(U"", SIZE_MAX));
    ASSERT(!goat_module_version_valid_wide(U"", SIZE_MAX));
    ASSERT(!goat_module_parse_import("", SIZE_MAX, &parsed));
    ASSERT(!goat_module_parse_import_wide(U"", SIZE_MAX, &parsed_wide));
    ASSERT(!parsed.name.data && !parsed.name.length && !parsed.version.data
           && !parsed.version.length && !parsed.has_version);
    ASSERT(!parsed_wide.name.data && !parsed_wide.name.length && !parsed_wide.version.data
           && !parsed_wide.version.length && !parsed_wide.has_version);
    ASSERT(!goat_module_name_valid_wide(NULL, 1));
    ASSERT(!goat_module_version_valid_wide(NULL, 1));
    return true;
}

bool test_module_identity(void) {
    ASSERT(test_identifier_alphabet());
    ASSERT(test_device_names());
    ASSERT(test_identity_limits());
    ASSERT(goat_module_name_valid("geometry", 8));
    ASSERT(goat_module_name_valid("123module", 9));
    ASSERT(goat_module_name_valid("a..b-_", 6));
    ASSERT(goat_module_version_valid("1.02.0-beta.1", 13));
    ASSERT(!goat_module_name_valid(NULL, 0));
    ASSERT(!goat_module_version_valid(NULL, 1));
    ASSERT(!goat_module_name_valid("", 0));
    ASSERT(!goat_module_version_valid("", 0));
    ASSERT(!goat_module_name_valid("Geometry", 8));
    ASSERT(!goat_module_name_valid("a.", 2));
    ASSERT(!goat_module_version_valid("1.", 2));
    ASSERT(!goat_module_name_valid("_a", 2));
    ASSERT(!goat_module_name_valid("-a", 2));
    ASSERT(!goat_module_name_valid("a/b", 3));
    ASSERT(!goat_module_name_valid("a@b", 3));
    ASSERT(!goat_module_name_valid("a b", 3));
    ASSERT(!goat_module_name_valid("a\0b", 3));
    ASSERT(!goat_module_version_valid("1\0x", 3));
    ASSERT(!goat_module_name_valid("a\xc3\xa9", 3));
    const char *reserved[] = {"con",
                              "prn",
                              "aux",
                              "nul",
                              "com0",
                              "com1",
                              "com9",
                              "lpt0",
                              "lpt1",
                              "lpt9",
                              "con.txt",
                              "nul.backup",
                              "com0.x",
                              "lpt9.1"};
    for (size_t i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i++) {
        ASSERT(!goat_module_name_valid(reserved[i], strlen(reserved[i])));
        ASSERT(!goat_module_version_valid(reserved[i], strlen(reserved[i])));
    }
    ASSERT(goat_module_name_valid("com10", 5));
    ASSERT(goat_module_name_valid("lpt10", 5));
    char name[256], version[251];
    memset(name, 'a', sizeof(name));
    memset(version, '1', sizeof(version));
    ASSERT(goat_module_name_valid(name, 255));
    ASSERT(!goat_module_name_valid(name, 256));
    ASSERT(goat_module_version_valid(version, 250));
    ASSERT(!goat_module_version_valid(version, 251));

    goat_module_import_t parsed = {0};
    const char *text = "geometry@1.02.0";
    ASSERT(goat_module_parse_import(text, strlen(text), &parsed));
    ASSERT(parsed.name.data == text && parsed.name.length == 8);
    ASSERT(parsed.has_version);
    ASSERT(parsed.version.data == text + 9 && parsed.version.length == 6);
    ASSERT(goat_module_parse_import("geometry", 8, &parsed));
    ASSERT(!parsed.has_version && !parsed.version.data && !parsed.version.length);
    const char *bad[] =
        {"", "geometry@", "@1.0", "geometry@1@2", "Geometry@1", "con@1", "geometry@con", "a\0b"};
    const size_t bad_length[] = {0, 9, 4, 12, 10, 5, 12, 3};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        ASSERT(!goat_module_parse_import(bad[i], bad_length[i], &parsed));
        ASSERT(!parsed.name.data && !parsed.name.length && !parsed.has_version);
    }
    ASSERT(!goat_module_parse_import(NULL, 0, &parsed));
    ASSERT(!goat_module_parse_import("a", 1, NULL));

    goat_module_import_wide_t wide = {0};
    const gchar_t *wtext = U"geometry@1.02.0";
    ASSERT(goat_module_parse_import_wide(wtext, STRLEN(wtext), &wide));
    ASSERT(wide.name.data == wtext && wide.name.length == 8);
    ASSERT(wide.version.data == wtext + 9 && wide.version.length == 6);
    ASSERT(goat_module_parse_import_wide(U"geometry", 8, &wide));
    ASSERT(!wide.has_version && !wide.version.data);
    ASSERT(!goat_module_parse_import_wide(U"geometry@", 9, &wide));
    ASSERT(!goat_module_parse_import_wide(U"geometry@1@2", 12, &wide));
    ASSERT(!goat_module_parse_import_wide(U"con.txt", 7, &wide));
    ASSERT(!goat_module_parse_import_wide(U"a\0b", 3, &wide));
    ASSERT(!goat_module_parse_import_wide(U"g\u00e9o", 3, &wide));
    ASSERT(!goat_module_parse_import_wide(NULL, 0, &wide));
    ASSERT(!goat_module_parse_import_wide(U"a", 1, NULL));
    return true;
}
