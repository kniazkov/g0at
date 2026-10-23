/**
 * @file identity.c
 * @brief Module identifier validation and zero-copy import parsing.
 */
#include <goat/module_identity.h>
#include <string.h>

static bool ascii_alnum(unsigned int ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
}

static bool reserved_device(const char *text, size_t length) {
    size_t stem = 0;
    while (stem < length && text[stem] != '.')
        stem++;
    if (stem == 3) {
        return !memcmp(text, "con", 3) || !memcmp(text, "prn", 3) || !memcmp(text, "aux", 3)
               || !memcmp(text, "nul", 3);
    }
    if (stem == 4) {
        bool port = !memcmp(text, "com", 3) || !memcmp(text, "lpt", 3);
        return port && text[3] >= '0' && text[3] <= '9';
    }
    return false;
}

static bool identifier_valid(const char *text, size_t length, size_t maximum) {
    if (!text || length == 0 || length > maximum || !ascii_alnum((unsigned char)text[0])
        || text[length - 1] == '.')
        return false;
    for (size_t i = 1; i < length; i++) {
        unsigned int ch = (unsigned char)text[i];
        if (!ascii_alnum(ch) && ch != '.' && ch != '-' && ch != '_')
            return false;
    }
    return !reserved_device(text, length);
}

bool goat_module_name_valid(const char *text, size_t length) {
    return identifier_valid(text, length, 255);
}

bool goat_module_version_valid(const char *text, size_t length) {
    return identifier_valid(text, length, 250);
}

bool goat_module_parse_import(const char *text, size_t length, goat_module_import_t *result) {
    if (!result)
        return false;
    *result = (goat_module_import_t){0};
    if (!text)
        return false;
    size_t split = 0;
    while (split < length && text[split] != '@')
        split++;
    if (!goat_module_name_valid(text, split))
        return false;
    if (split < length && !goat_module_version_valid(text + split + 1, length - split - 1))
        return false;
    result->name = (goat_module_slice_t){text, split};
    if (split < length) {
        result->has_version = true;
        result->version = (goat_module_slice_t){text + split + 1, length - split - 1};
    }
    return true;
}

static bool wide_identifier_valid(const goat_char_t *text, size_t length, size_t maximum) {
    if (!text || length == 0 || length > maximum || text[length - 1] == U'.')
        return false;
    if (!((text[0] >= U'a' && text[0] <= U'z') || (text[0] >= U'0' && text[0] <= U'9')))
        return false;
    for (size_t i = 0; i < length; i++) {
        goat_char_t ch = text[i];
        if (!((ch >= U'a' && ch <= U'z') || (ch >= U'0' && ch <= U'9') || ch == U'.' || ch == U'-'
              || ch == U'_'))
            return false;
    }
    /* The reserved device names are ASCII and never exceed four characters. */
    size_t stem = 0;
    while (stem < length && text[stem] != U'.')
        stem++;
    if (stem == 3) {
        if ((text[0] == U'c' && text[1] == U'o' && text[2] == U'n')
            || (text[0] == U'p' && text[1] == U'r' && text[2] == U'n')
            || (text[0] == U'a' && text[1] == U'u' && text[2] == U'x')
            || (text[0] == U'n' && text[1] == U'u' && text[2] == U'l'))
            return false;
    }
    if (stem == 4
        && ((text[0] == U'c' && text[1] == U'o' && text[2] == U'm')
            || (text[0] == U'l' && text[1] == U'p' && text[2] == U't'))
        && text[3] >= U'0' && text[3] <= U'9')
        return false;
    return true;
}

bool goat_module_parse_import_wide(const goat_char_t *text,
                                   size_t length,
                                   goat_module_import_wide_t *result) {
    if (!result)
        return false;
    *result = (goat_module_import_wide_t){0};
    if (!text)
        return false;
    size_t split = 0;
    while (split < length && text[split] != U'@')
        split++;
    if (!wide_identifier_valid(text, split, 255))
        return false;
    if (split < length && !wide_identifier_valid(text + split + 1, length - split - 1, 250))
        return false;
    result->name = (goat_module_wide_slice_t){text, split};
    if (split < length) {
        result->has_version = true;
        result->version = (goat_module_wide_slice_t){text + split + 1, length - split - 1};
    }
    return true;
}
