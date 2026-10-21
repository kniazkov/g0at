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

static bool wide_identifier_valid(const wchar_t *text, size_t length, size_t maximum) {
    if (!text || length == 0 || length > maximum || text[length - 1] == L'.')
        return false;
    if (!((text[0] >= L'a' && text[0] <= L'z') || (text[0] >= L'0' && text[0] <= L'9')))
        return false;
    for (size_t i = 0; i < length; i++) {
        wchar_t ch = text[i];
        if (!((ch >= L'a' && ch <= L'z') || (ch >= L'0' && ch <= L'9') || ch == L'.' || ch == L'-'
              || ch == L'_'))
            return false;
    }
    /* The reserved device names are ASCII and never exceed four characters. */
    size_t stem = 0;
    while (stem < length && text[stem] != L'.')
        stem++;
    if (stem == 3) {
        if ((text[0] == L'c' && text[1] == L'o' && text[2] == L'n')
            || (text[0] == L'p' && text[1] == L'r' && text[2] == L'n')
            || (text[0] == L'a' && text[1] == L'u' && text[2] == L'x')
            || (text[0] == L'n' && text[1] == L'u' && text[2] == L'l'))
            return false;
    }
    if (stem == 4
        && ((text[0] == L'c' && text[1] == L'o' && text[2] == L'm')
            || (text[0] == L'l' && text[1] == L'p' && text[2] == L't'))
        && text[3] >= L'0' && text[3] <= L'9')
        return false;
    return true;
}

bool goat_module_parse_import_wide(const wchar_t *text,
                                   size_t length,
                                   goat_module_import_wide_t *result) {
    if (!result)
        return false;
    *result = (goat_module_import_wide_t){0};
    if (!text)
        return false;
    size_t split = 0;
    while (split < length && text[split] != L'@')
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
