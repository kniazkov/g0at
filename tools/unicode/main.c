/* Copyright 2026 Ivan Kniazkov
 * Distributed under the MIT license; see LICENSE.txt. */
#include "lib/hash/sha256.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#    include <windows.h>
#else
#    include <dirent.h>
#endif

#ifndef GOAT_UNICODE_ROOT
#    define GOAT_UNICODE_ROOT "."
#endif

/* This program never includes the generated Unicode tables. */
typedef struct buffer {
    char *data;
    size_t size, capacity;
} buffer;

typedef struct mapping {
    unsigned code, count, values[3];
} mapping;

typedef struct mappings {
    mapping *items;
    size_t count;
} mappings;

typedef struct range {
    unsigned first, last;
} range;

typedef struct ranges {
    range *items;
    size_t count;
} ranges;

static const char *input_name;
static size_t input_line;

static void fail(const char *message) {
    if (input_name)
        fprintf(stderr, "unicode: %s:%zu: %s\n", input_name, input_line, message);
    else
        fprintf(stderr, "unicode: %s\n", message);
    exit(EXIT_FAILURE);
}

static void *resize(void *old, size_t size) {
    void *p = realloc(old, size);
    if (!p)
        fail("out of memory");
    return p;
}

static void append(buffer *b, const char *format, ...) {
    va_list args, copy;
    va_start(args, format);
    va_copy(copy, args);
    int n = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (n < 0 || (size_t)n > SIZE_MAX - b->size - 1)
        fail("output too large");
    size_t need = b->size + (size_t)n + 1;
    if (need > b->capacity) {
        b->capacity = need > SIZE_MAX / 2 ? need : need * 2;
        b->data = resize(b->data, b->capacity);
    }
    vsnprintf(b->data + b->size, b->capacity - b->size, format, args);
    va_end(args);
    b->size += (size_t)n;
}

static char *join(const char *root, const char *path) {
    buffer b = {0};
    append(&b, "%s/%s", root, path);
    return b.data;
}

static buffer read_file(const char *path, bool required) {
    buffer b = {0};
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (required) {
            fprintf(stderr, "unicode: cannot open %s\n", path);
            fail("input file unavailable");
        }
        return b;
    }
    char chunk[8192];
    size_t count;
    while ((count = fread(chunk, 1, sizeof(chunk), f)) != 0) {
        if (count > SIZE_MAX - b.size - 1)
            fail("input too large");
        b.data = resize(b.data, b.size + count + 1);
        memcpy(b.data + b.size, chunk, count);
        b.size += count;
        b.data[b.size] = 0;
    }
    if (ferror(f) || fclose(f))
        fail("failed reading file");
    if (!b.data) {
        b.data = resize(NULL, 1);
        b.data[0] = 0;
    }
    return b;
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s))
        s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        *--end = 0;
    return s;
}

/* Unlike strtok, preserve empty semicolon fields. */
static size_t fields(char *line, char **out, size_t capacity) {
    size_t n = 0;
    for (;;) {
        if (n == capacity)
            fail("too many fields");
        char *end = strchr(line, ';');
        if (end)
            *end = 0;
        out[n++] = trim(line);
        if (!end)
            return n;
        line = end + 1;
    }
}

static unsigned codepoint(const char *s) {
    if (!*s)
        fail("missing code point");
    for (const char *p = s; *p; p++)
        if (!isxdigit((unsigned char)*p))
            fail("invalid hexadecimal code point");
    errno = 0;
    char *end;
    unsigned long value = strtoul(s, &end, 16);
    if (errno || *end || value > 0x10ffffUL)
        fail("code point out of range");
    return (unsigned)value;
}

static mapping sequence(unsigned code, char *text) {
    mapping m = {0};
    m.code = code;
    char *s = trim(text);
    while (*s) {
        char *end = s;
        while (*end && !isspace((unsigned char)*end))
            end++;
        bool more = *end != 0;
        *end = 0;
        if (m.count == 3)
            fail("case mapping exceeds three scalars");
        unsigned value = codepoint(s);
        if (value >= 0xd800 && value <= 0xdfff)
            fail("surrogate in case mapping");
        m.values[m.count++] = value;
        s = more ? trim(end + 1) : end;
    }
    if (!m.count)
        fail("empty case mapping");
    return m;
}

static void put(mappings *table, mapping m) {
    for (size_t i = 0; i < table->count; i++)
        if (table->items[i].code == m.code) {
            table->items[i] = m;
            return;
        }
    table->items = resize(table->items, (table->count + 1) * sizeof(mapping));
    table->items[table->count++] = m;
}

static int mapping_order(const void *a, const void *b) {
    unsigned x = ((const mapping *)a)->code, y = ((const mapping *)b)->code;
    return (x > y) - (x < y);
}

static int range_order(const void *a, const void *b) {
    const range *x = a, *y = b;
    if (x->first != y->first)
        return (x->first > y->first) - (x->first < y->first);
    return (x->last > y->last) - (x->last < y->last);
}

static void
parse_file(const char *directory, const char *name, mappings tables[4], ranges props[3]) {
    char *path = join(directory, name);
    buffer source = read_file(path, true);
    if (memchr(source.data, 0, source.size))
        fail("NUL byte in UCD input");
    input_name = name;
    input_line = 0;
    char *line = source.data;
    while (*line) {
        input_line++;
        char *next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        char *comment = strchr(line, '#');
        if (comment)
            *comment = 0;
        line = trim(line);
        if (*line) {
            char *f[20];
            size_t n = fields(line, f, 20);
            if (!strcmp(name, "UnicodeData.txt")) {
                if (n != 15)
                    fail("UnicodeData requires 15 fields");
                unsigned code = codepoint(f[0]);
                for (size_t i = 0; i < 2; i++)
                    if (*f[12 + i]) {
                        mapping m = sequence(code, f[12 + i]);
                        if (m.count != 1)
                            fail("simple case mapping must have one scalar");
                        put(&tables[i], m);
                        put(&tables[i + 2], m);
                    }
            } else if (!strcmp(name, "SpecialCasing.txt")) {
                if (n < 5 || n > 6 || (n == 6 && *f[5]))
                    fail("SpecialCasing requires five fields");
                unsigned code = codepoint(f[0]);
                if (!*f[4]) {
                    mapping lower = sequence(code, f[1]);
                    (void)sequence(code, f[2]);
                    mapping upper = sequence(code, f[3]);
                    put(&tables[3], lower);
                    put(&tables[2], upper);
                }
            } else {
                if (n < 2)
                    fail("property record requires a value");
                const char *names[] = {"Cased", "Case_Ignorable", "White_Space"};
                for (size_t i = 0; i < 3; i++) {
                    if (strcmp(f[1], names[i]))
                        continue;
                    if (n != 2)
                        fail("invalid property fields");
                    char *end = strstr(f[0], "..");
                    unsigned last;
                    if (end) {
                        *end = 0;
                        last = codepoint(end + 2);
                    } else
                        last = codepoint(f[0]);
                    unsigned first = codepoint(f[0]);
                    if (last < first)
                        fail("reversed property range");
                    ranges *r = &props[i];
                    r->items = resize(r->items, (r->count + 1) * sizeof(range));
                    r->items[r->count++] = (range){first, last};
                }
            }
        }
        if (!next)
            break;
        line = next;
    }
    input_name = NULL;
    free(source.data);
    free(path);
}

static int string_order(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static void add_filename(char ***names, size_t *count, const char *name) {
    size_t n = strlen(name);
    if (n < 4 || strcmp(name + n - 4, ".txt"))
        return;
    *names = resize(*names, (*count + 1) * sizeof(char *));
    (*names)[*count] = resize(NULL, n + 1);
    memcpy((*names)[(*count)++], name, n + 1);
}

static buffer manifest(const char *directory) {
    char **names = NULL;
    size_t count = 0;
#ifdef _WIN32
    char *pattern = join(directory, "*");
    WIN32_FIND_DATAA entry;
    HANDLE search = FindFirstFileA(pattern, &entry);
    free(pattern);
    if (search == INVALID_HANDLE_VALUE)
        fail("cannot enumerate UCD directory");
    for (;;) {
        add_filename(&names, &count, entry.cFileName);
        if (!FindNextFileA(search, &entry)) {
            DWORD error = GetLastError();
            FindClose(search);
            if (error != ERROR_NO_MORE_FILES)
                fail("cannot read UCD directory");
            break;
        }
    }
#else
    DIR *dir = opendir(directory);
    if (!dir)
        fail("cannot enumerate UCD directory");
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry) {
            int error = errno;
            closedir(dir);
            if (error)
                fail("cannot read UCD directory");
            break;
        }
        add_filename(&names, &count, entry->d_name);
    }
#endif
    if (!count)
        fail("no UCD text files");
    qsort(names, count, sizeof(char *), string_order);
    buffer result = {0};
    for (size_t i = 0; i < count; i++) {
        char *path = join(directory, names[i]);
        buffer data = read_file(path, true);
        uint8_t digest[SHA256_SIZE];
        sha256(data.data, data.size, digest);
        for (size_t j = 0; j < SHA256_SIZE; j++)
            append(&result, "%02x", digest[j]);
        append(&result, "  %s\n", names[i]);
        free(data.data);
        free(path);
        free(names[i]);
    }
    free(names);
    return result;
}

static bool matches(const buffer *actual, const buffer *expected) {
    size_t a = 0, e = 0;
    while (a < actual->size && e < expected->size) {
        if (actual->data[a] == '\r' && a + 1 < actual->size && actual->data[a + 1] == '\n')
            a++;
        if (actual->data[a++] != expected->data[e++])
            return false;
    }
    return a == actual->size && e == expected->size;
}

int main(int argc, char **argv) {
    const char *root = GOAT_UNICODE_ROOT;
    int mode = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--check") || !strcmp(argv[i], "--update")) {
            if (mode)
                fail("specify exactly one mode");
            mode = !strcmp(argv[i], "--check") ? 1 : 2;
        } else if (!strcmp(argv[i], "--root") && i + 1 < argc)
            root = argv[++i];
        else if (!strcmp(argv[i], "--help")) {
            puts("unicode_generator (--check | --update) [--root REPOSITORY]");
            return 0;
        } else
            fail("invalid arguments (use --help)");
    }
    if (!mode)
        fail("explicit --check or --update required");
    char *directory = join(root, "third_party/unicode/17.0.0");
    mappings tables[4] = {{0}};
    ranges props[3] = {{0}};
    const char *files[] = {"UnicodeData.txt",
                           "SpecialCasing.txt",
                           "DerivedCoreProperties.txt",
                           "PropList.txt"};
    for (size_t i = 0; i < 4; i++)
        parse_file(directory, files[i], tables, props);
    buffer outputs[2] = {{0}};
    append(&outputs[0],
           "/* Generated by tools/unicode; Unicode 17.0.0. Do not edit.\n"
           " * Implementation-only Unicode tables, included after their private types.\n"
           " * Intentional project exception: no #pragma once and no include guard.\n"
           " * Keep generated table formatting reproducible without clang-format.\n"
           " */\n// clang-format off\n");
    const char *table_names[] = {"simple_upper", "simple_lower", "full_upper", "full_lower"};
    for (size_t i = 0; i < 4; i++) {
        mappings *t = &tables[i];
        if (!t->count)
            fail("empty case table");
        qsort(t->items, t->count, sizeof(mapping), mapping_order);
        append(&outputs[0], "static const case_entry_t %s[] = {\n", table_names[i]);
        for (size_t j = 0; j < t->count; j++) {
            mapping *m = &t->items[j];
            append(&outputs[0], "    {0x%x, %u, {", m->code, m->count);
            for (unsigned k = 0; k < m->count; k++)
                append(&outputs[0], "%s0x%x", k ? "," : "", m->values[k]);
            append(&outputs[0], "}},\n");
        }
        append(&outputs[0], "};\n");
        free(t->items);
    }
    const char *property_names[] = {"Cased", "Case_Ignorable", "White_Space"};
    for (size_t i = 0; i < 3; i++) {
        ranges *r = &props[i];
        if (!r->count)
            fail("empty property table");
        qsort(r->items, r->count, sizeof(range), range_order);
        append(&outputs[0], "static const scalar_range_t %s[] = {\n", property_names[i]);
        for (size_t j = 0; j < r->count; j++)
            append(&outputs[0], "    {0x%x,0x%x},\n", r->items[j].first, r->items[j].last);
        append(&outputs[0], "};\n");
        free(r->items);
    }
    outputs[1] = manifest(directory);
    char *paths[] = {join(root, "lib/strings/unicode_data.h"), join(directory, "SHA256SUMS")};
    int result = 0;
    for (size_t i = 0; i < 2; i++) {
        buffer old = read_file(paths[i], false);
        bool same = old.data && matches(&old, &outputs[i]);
        free(old.data);
        if (!same && mode == 1) {
            fprintf(stderr, "unicode: stale or missing generated file: %s\n", paths[i]);
            result = 1;
        } else if (!same) {
            FILE *file = fopen(paths[i], "wb");
            if (!file) {
                fprintf(stderr, "unicode: cannot write %s\n", paths[i]);
                fail("output unavailable");
            }
            bool written = fwrite(outputs[i].data, 1, outputs[i].size, file) == outputs[i].size;
            if (fclose(file) || !written)
                fail("failed writing output");
        }
        free(paths[i]);
        free(outputs[i].data);
    }
    free(directory);
    return result;
}
