/**
 * @file identity.c
 * @brief Module identifier validation and zero-copy import parsing.
 */
#include <goat/module_identity.h>

/* Only storage access differs. Grammar, limits and reserved names are shared. */
typedef struct {
    const void *data;
    bool wide;
} identifier_input;

static uint32_t character(identifier_input input, size_t index) {
    return input.wide ? ((const gchar_t *)input.data)[index]
                      : ((const unsigned char *)input.data)[index];
}

static bool ascii_alnum(uint32_t ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
}

static bool reserved_device(identifier_input input, size_t start, size_t length) {
    size_t stem = 0;
    while (stem < length && character(input, start + stem) != '.')
        stem++;
    if (stem != 3 && stem != 4)
        return false;
    uint32_t a = character(input, start), b = character(input, start + 1);
    uint32_t c = character(input, start + 2);
    if (stem == 3)
        return (a == 'c' && b == 'o' && c == 'n') || (a == 'p' && b == 'r' && c == 'n')
               || (a == 'a' && b == 'u' && c == 'x') || (a == 'n' && b == 'u' && c == 'l');
    uint32_t digit = character(input, start + 3);
    return ((a == 'c' && b == 'o' && c == 'm') || (a == 'l' && b == 'p' && c == 't'))
           && digit >= '0' && digit <= '9';
}

static bool identifier_valid(identifier_input input, size_t start, size_t length, size_t maximum) {
    if (!input.data || length == 0 || length > maximum || !ascii_alnum(character(input, start))
        || character(input, start + length - 1) == '.')
        return false;
    for (size_t i = 1; i < length; i++) {
        uint32_t ch = character(input, start + i);
        if (!ascii_alnum(ch) && ch != '.' && ch != '-' && ch != '_')
            return false;
    }
    return !reserved_device(input, start, length);
}

bool goat_module_name_valid(const char *text, size_t length) {
    return identifier_valid((identifier_input){text, false},
                            0,
                            length,
                            GOAT_MODULE_NAME_MAX_LENGTH);
}

bool goat_module_version_valid(const char *text, size_t length) {
    return identifier_valid((identifier_input){text, false},
                            0,
                            length,
                            GOAT_MODULE_VERSION_MAX_LENGTH);
}

bool goat_module_name_valid_wide(const gchar_t *text, size_t length) {
    return identifier_valid((identifier_input){text, true}, 0, length, GOAT_MODULE_NAME_MAX_LENGTH);
}

bool goat_module_version_valid_wide(const gchar_t *text, size_t length) {
    return identifier_valid((identifier_input){text, true},
                            0,
                            length,
                            GOAT_MODULE_VERSION_MAX_LENGTH);
}

static bool parse_import(identifier_input input, size_t length, size_t *split) {
    /* Reject excessive lengths before scanning or doing offset arithmetic. */
    if (!input.data || !length
        || length > GOAT_MODULE_NAME_MAX_LENGTH + 1 + GOAT_MODULE_VERSION_MAX_LENGTH)
        return false;
    *split = 0;
    while (*split < length && character(input, *split) != '@')
        ++*split;
    if (!identifier_valid(input, 0, *split, GOAT_MODULE_NAME_MAX_LENGTH))
        return false;
    return *split == length
           || identifier_valid(input,
                               *split + 1,
                               length - *split - 1,
                               GOAT_MODULE_VERSION_MAX_LENGTH);
}

bool goat_module_parse_import(const char *text, size_t length, goat_module_import_t *result) {
    if (!result)
        return false;
    *result = (goat_module_import_t){0};
    size_t split;
    if (!parse_import((identifier_input){text, false}, length, &split))
        return false;
    result->name = (goat_module_slice_t){text, split};
    if (split < length) {
        result->has_version = true;
        result->version = (goat_module_slice_t){text + split + 1, length - split - 1};
    }
    return true;
}

bool goat_module_parse_import_wide(const gchar_t *text,
                                   size_t length,
                                   goat_module_import_wide_t *result) {
    if (!result)
        return false;
    *result = (goat_module_import_wide_t){0};
    size_t split;
    if (!parse_import((identifier_input){text, true}, length, &split))
        return false;
    result->name = (goat_module_wide_slice_t){text, split};
    if (split < length) {
        result->has_version = true;
        result->version = (goat_module_wide_slice_t){text + split + 1, length - split - 1};
    }
    return true;
}
