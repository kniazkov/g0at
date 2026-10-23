/** @file test_unicode.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Portable scalar strings, bounded operations, casing and binary validation.
 */
#include "codegen/linker.h"
#include "lib/allocate.h"
#include "lib/comparison.h"
#include "lib/io.h"
#include "lib/string_ext.h"
#include "test_lib.h"
#include "test_macro.h"
#include "vm/binary.h"

#include <locale.h>
#include <math.h>

bool test_unicode_strings(void) {
    string_value_t value = STATIC_STRING(U"\0A\0B\0\0\U0001f600");
    ASSERT(value.length == 7 && sizeof(*value.data) == 4);
    string_builder_t builder;
    init_string_builder(&builder, 0);
    string_value_t copy = append_string_value(&builder, value);
    ASSERT(string_view_equals(VALUE_TO_VIEW(copy), VALUE_TO_VIEW(value)));
    /* Nonterminated slices and self-append across a reallocation. */
    const gchar_t slice[] = {U'x', 0, U'y'};
    copy = append_substring(&builder, slice, 3);
    copy = append_substring(&builder, builder.data, builder.length);
    ASSERT(copy.length == 20 && !MEMCMP(copy.data, copy.data + 10, 10));
    FREE_STRING(copy);
    size_t bytes;
    char *utf8 = encode_utf8_view(VALUE_TO_VIEW(value), &bytes);
    ASSERT(bytes == 10 && !memcmp(utf8, "\0A\0B\0\0\xf0\x9f\x98\x80", 10));
    copy = decode_utf8_n(utf8, bytes);
    ASSERT(string_view_equals(VALUE_TO_VIEW(value), VALUE_TO_VIEW(copy)));
    FREE(utf8);
    FREE_STRING(copy);
    size_t units;
    uint16_t *utf16 = encode_utf16(VALUE_TO_VIEW(value), &units);
    ASSERT(units == 8 && utf16[6] == 0xd83d && utf16[7] == 0xde00);
    copy = decode_utf16(utf16, units);
    ASSERT(string_view_equals(VALUE_TO_VIEW(value), VALUE_TO_VIEW(copy)));
    FREE(utf16);
    FREE_STRING(copy);
    const uint16_t bad[] = {0xd800, U'A'};
    ASSERT(!decode_utf16(bad, 2).data);
    ASSERT(!decode_utf16(bad, 1).data);
    ASSERT(!decode_utf8_n("\xf0\x9f\x98", 3).data);
    copy = format_string_view(VALUE_TO_VIEW(STATIC_STRING(U"[%v]\0%u")), VALUE_TO_VIEW(value), 42u);
    ASSERT(copy.length == 12 && copy.data[10] == U'4' && copy.data[11] == U'2');
    FREE_STRING(copy);
    copy = string_to_string_notation(U"", STATIC_STRING(U"A\0B"));
    ASSERT(!STRCMP(copy.data, U"\"A\\0B\""));
    FREE_STRING(copy);
    string_view_t a = VALUE_TO_VIEW(STATIC_STRING(U"A"));
    string_view_t b = VALUE_TO_VIEW(STATIC_STRING(U"A\0B"));
    ASSERT(compare_strings(a, b) == ORDER_LESS && string_view_hash(a) != string_view_hash(b));
    ASSERT(!encode_utf8_path(b));
    ASSERT(!read_utf8_file_view(VALUE_TO_VIEW(STATIC_STRING(U"README.md\0missing"))).data);
    ASSERT(EMPTY_STRING_VALUE.data && !NULL_STRING_VALUE.data);
    ASSERT(!string_view_equals(EMPTY_STRING_VIEW, (string_view_t){NULL, 0}));
    ASSERT(string_view_hash(EMPTY_STRING_VIEW) != string_view_hash((string_view_t){NULL, 0}));
    ASSERT(!decode_utf8_n(NULL, 0).data && !decode_utf16(NULL, 0).data);
    ASSERT(!format_string_view((string_view_t){NULL, 0}).data);
    ASSERT(!unicode_string_case((string_view_t){NULL, 0}, true).data);
    return true;
}

bool test_unicode_case(void) {
    const char *locales[] = {"C", "C.UTF-8", "en_US.UTF-8", "tr_TR.UTF-8", ""};
    for (size_t i = 0; i < sizeof(locales) / sizeof(*locales); ++i) {
        setlocale(LC_ALL, locales[i]);
        gchar_t *end;
        ASSERT(STRTOD(U"1.5tail", &end) == 1.5 && *end == U't');
        char number[32];
        double_to_string(1.5, number, sizeof(number));
        ASSERT(!strcmp(number, "1.5"));
        double_to_string(1e20, number, sizeof(number));
        ASSERT(!strcmp(number, "1e+20"));
        double_to_string(-1e-20, number, sizeof(number));
        ASSERT(!strcmp(number, "-1e-20"));
        double_to_string(INFINITY, number, sizeof(number));
        ASSERT(!strcmp(number, "inf"));
        double_to_string(-INFINITY, number, sizeof(number));
        ASSERT(!strcmp(number, "-inf"));
        double_to_string(NAN, number, sizeof(number));
        ASSERT(!strcmp(number, "nan"));
        double_to_string(123.5, number, 3);
        ASSERT(!strcmp(number, "12"));
        ASSERT(TOUPPER(0xdf) == 0xdf);
        ASSERT(TOLOWER(0x10400) == 0x10428);
        string_value_t upper =
            unicode_string_case(VALUE_TO_VIEW(STATIC_STRING(U"aß\0я\U00010428")), true);
        ASSERT(string_view_equals(VALUE_TO_VIEW(upper),
                                  VALUE_TO_VIEW(STATIC_STRING(U"ASS\0Я\U00010400"))));
        FREE_STRING(upper);
        string_value_t lower =
            unicode_string_case(VALUE_TO_VIEW(STATIC_STRING(U"ΟΣ ΟΣΑ ΟΣ\u0301\0Σ İ")), false);
        ASSERT(string_view_equals(VALUE_TO_VIEW(lower),
                                  VALUE_TO_VIEW(STATIC_STRING(U"ος οσα ος\u0301\0σ i\u0307"))));
        FREE_STRING(lower);
        lower = unicode_string_case(EMPTY_STRING_VIEW, false);
        ASSERT(lower.data && !lower.length);
        FREE_STRING(lower);
    }
    setlocale(LC_ALL, "C");
    return true;
}

bool test_unicode_binary(void) {
    code_builder_t *instructions = create_code_builder();
    data_builder_t *data = create_data_builder();
    string_view_t text = VALUE_TO_VIEW(STATIC_STRING(U"A\0Ж\U0001f600e\u0301"));
    uint32_t id = add_string_to_data_segment_ex(data, text);
    ASSERT(id == add_string_to_data_segment_ex(data, text));
    ASSERT(id != add_string_to_data_segment(data, U"A"));
    const uint8_t expected[] = {0x41, 0, 0,    0, 0, 0, 0, 0, 0x16, 4, 0, 0, 0, 0xf6,
                                1,    0, 0x65, 0, 0, 0, 1, 3, 0,    0, 0, 0, 0, 0};
    ASSERT(data->descriptors[id].size == sizeof(expected));
    ASSERT(!memcmp(data->data, expected, sizeof(expected)));
    add_instruction(instructions, (instruction_t){.opcode = SLOAD, .arg1 = id});
    add_instruction(instructions, (instruction_t){.opcode = END});
    bytecode_t *code = link_code_and_data(instructions, data);
    destroy_code_builder(instructions);
    destroy_data_builder(data);
    const char *path = "unicode-binary-test.gbin";
    for (unsigned test = 0; test < 6; ++test) {
        memcpy(code->data, expected, sizeof(expected));
        code->data_descriptors[id].offset = 0;
        code->data_descriptors[id].size = sizeof(expected);
        if (test == 1) {
            code->data[0] = 0;
            code->data[1] = 0xd8;
        }
        if (test == 2) {
            code->data[0] = 0;
            code->data[1] = 0;
            code->data[2] = 0x11;
        }
        if (test == 3)
            code->data_descriptors[id].size--;
        if (test == 4)
            code->data_descriptors[id].offset = 1;
        if (test == 5)
            code->data[sizeof(expected) - 4] = 1;
        ASSERT(save_binary_program(path, code, NULL));
        binary_program_t loaded = load_binary_program(path);
        ASSERT((loaded.code != NULL) == (test == 0));
        if (loaded.code) {
            const gchar_t *s = (const gchar_t *)loaded.code->data;
            ASSERT(!MEMCMP(s, text.data, text.length));
            ASSERT((const uint8_t *)s >= (const uint8_t *)loaded.code->buffer);
            ASSERT((const uint8_t *)(s + text.length + 1)
                   <= (const uint8_t *)loaded.code->buffer + loaded.code->buffer_size);
        }
        destroy_binary_program(&loaded);
    }
    free_bytecode(code);
    ASSERT(!remove(path));
    return true;
}
