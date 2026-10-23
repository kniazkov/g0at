/** @file test_module_identity.c
 * @brief Portable module identifier and import parser regressions.
 */
#include "test_module_identity.h"

#include "goat/unicode.h"
#include "test_macro.h"

#include <goat/module_identity.h>
#include <stdio.h>
#include <string.h>

bool test_module_identity(void) {
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
