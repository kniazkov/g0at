/**
 * @file test_lib_safety.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Regression tests for allocation, paths, search boundaries, and UTF-8 I/O.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#    include <io.h>
#    include <process.h>
#else
#    include <unistd.h>
#endif

#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/io.h"
#include "lib/pair.h"
#include "lib/path.h"
#include "lib/string_ext.h"
#include "test_lib.h"
#include "test_macro.h"

bool test_allocation_alignment() {
    const size_t sizes[] = {0, 1, 3, sizeof(max_align_t), 257};
    size_t before = get_allocated_memory_size();
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        unsigned char *p = ALLOC(sizes[i]);
        ASSERT((uintptr_t)p % _Alignof(max_align_t) == 0);
        memset(p, 0x5A, sizes[i] ? sizes[i] : 1);
        FREE(p);
        p = CALLOC(sizes[i]);
        ASSERT((uintptr_t)p % _Alignof(max_align_t) == 0);
        for (size_t j = 0; j < sizes[i]; j++) {
            ASSERT(p[j] == 0);
        }
        FREE(p);
    }
    max_align_t *p = ALLOC(sizeof(max_align_t));
    *p = (max_align_t){0};
    FREE(p);
    FREE(NULL);
    ASSERT(get_allocated_memory_size() == before);
    return true;
}

bool test_arena_alignment_and_growth() {
    size_t before = get_allocated_memory_size();
    /* Exercise both the minimum/default size and explicit chunk sizing. */
    for (size_t kb = 0; kb <= 1; kb++) {
        arena_t *arena = create_arena(kb);
        unsigned char *blocks[160];
        size_t sizes[160];
        for (size_t i = 0; i < 160; i++) {
            /* Mix odd sizes, dedicated chunks, and the big-object boundary. */
            sizes[i] = i % 4 == 0 ? 513 : (i % 4 == 1 ? 255 : i % 37 + 1);
            blocks[i] = alloc_from_arena(arena, sizes[i]);
            ASSERT((uintptr_t)blocks[i] % _Alignof(max_align_t) == 0);
            memset(blocks[i], (unsigned char)i, sizes[i]);
        }
        for (size_t i = 0; i < 160; i++) {
            for (size_t j = 0; j < sizes[i]; j++) {
                ASSERT(blocks[i][j] == (unsigned char)i);
            }
        }
        void *zero = alloc_from_arena(arena, 0);
        ASSERT((uintptr_t)zero % _Alignof(max_align_t) == 0);
        max_align_t *aligned = alloc_zeroed_from_arena(arena, sizeof(max_align_t));
        ASSERT((uintptr_t)aligned % _Alignof(max_align_t) == 0);
        const unsigned char *bytes = (const unsigned char *)aligned;
        for (size_t i = 0; i < sizeof(*aligned); i++) {
            ASSERT(bytes[i] == 0);
        }
        *aligned = (max_align_t){0};
        const unsigned char source[] = {1, 2, 3};
        ASSERT(memcmp(copy_object_to_arena(arena, source, sizeof(source)), source, sizeof(source))
               == 0);
        string_view_t text = copy_string_to_arena(arena, L"abc", 3);
        ASSERT(text.length == 3 && wcscmp(text.data, L"abc") == 0);
        destroy_arena(arena);
    }
    ASSERT(get_allocated_memory_size() == before);
    return true;
}

bool test_binary_search_boundaries() {
    pair_t pairs[] = {{L"b", L"first"}, {L"d", L"last"}};
    ASSERT(binary_search(NULL, 0, L"a", string_comparator) == NULL);
    ASSERT(binary_search(pairs, 1, L"a", string_comparator) == NULL);
    ASSERT(binary_search(pairs, 1, L"c", string_comparator) == NULL);
    ASSERT(binary_search(pairs, 1, L"b", string_comparator) == pairs[0].value);
    ASSERT(binary_search(pairs, 2, L"a", string_comparator) == NULL);
    ASSERT(binary_search(pairs, 2, L"c", string_comparator) == NULL);
    ASSERT(binary_search(pairs, 2, L"z", string_comparator) == NULL);
    ASSERT(binary_search(pairs, 2, L"b", string_comparator) == pairs[0].value);
    ASSERT(binary_search(pairs, 2, L"d", string_comparator) == pairs[1].value);
    return true;
}

bool test_path_lifetime() {
    size_t before = get_allocated_memory_size();
    path_t *path = create_path("goat_missing_test_directory/source.goat");
    ASSERT(path->normal_path != path->full_path);
    ASSERT(strcmp(path->file_name, "source.goat") == 0);
    ASSERT(strcmp(path->extension, "goat") == 0);
    free_path(path);
    path = create_path(".");
    ASSERT(path->full_path != NULL);
    ASSERT(path->normal_path != path->full_path);
    free_path(path);
    path = create_path("");
    ASSERT(path->normal_path == NULL && path->full_path == NULL);
    free_path(path);
    free_path(create_path(NULL));
    free_path(NULL);
    ASSERT(get_allocated_memory_size() == before);
    return true;
}

/** @brief Creates a unique test file exclusively, including on legacy Windows CRTs. */
static FILE *create_test_file(char *name, size_t capacity) {
    static unsigned sequence;
    for (unsigned attempt = 0; attempt < 100; attempt++) {
#ifdef _WIN32
        long pid = (long)_getpid();
#else
        long pid = (long)getpid();
#endif
        snprintf(name, capacity, "goat_lib_%ld_%u.tmp", pid, sequence++);
#ifdef _WIN32
        int fd = _open(name, _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
        int fd = open(name, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
#endif
        if (fd < 0) {
            if (errno == EEXIST)
                continue;
            perror("create test file");
            return NULL;
        }
#ifdef _WIN32
        FILE *file = _fdopen(fd, "w+b");
        if (!file)
            _close(fd);
#else
        FILE *file = fdopen(fd, "w+b");
        if (!file)
            close(fd);
#endif
        if (!file)
            remove(name);
        return file;
    }
    return NULL;
}

bool test_utf8_formatted_output() {
    char name[80];
    FILE *file = create_test_file(name, sizeof(name));
    ASSERT(file != NULL);
    /* Percent signs supplied as data must never become a second format string. */
    fprintf_utf8(file,
                 L"%s | %a | %d%% | %s",
                 L"100%% %s %n %",
                 "file%name",
                 42,
                 L"\u041f\u0440\u0438\u0432\u0435\u0442");
    ASSERT(fflush(file) == 0);
    rewind(file);
    char buffer[128] = {0};
    size_t count = fread(buffer, 1, sizeof(buffer) - 1, file);
    ASSERT(!ferror(file));
    ASSERT(fclose(file) == 0);
    ASSERT(remove(name) == 0);
    const char expected[] = "100%% %s %n % | file%name | 42% | "
                            "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";
    ASSERT(count == strlen(expected));
    ASSERT(strcmp(buffer, expected) == 0);
    return true;
}

bool test_utf8_file_io() {
    char name[80];
    FILE *file = create_test_file(name, sizeof(name));
    ASSERT(file != NULL);
    ASSERT(fclose(file) == 0);
    size_t before = get_allocated_memory_size();
    string_value_t text = read_utf8_file(name);
    bool empty_ok = text.data != NULL && text.length == 0;
    FREE_STRING(text);
    bool no_leak = get_allocated_memory_size() == before;
    bool written = write_utf8_file(name, L"hello\n\u041f\u0440\u0438\u0432\u0435\u0442\n100%");
    text = read_utf8_file(name);
    bool roundtrip_ok =
        text.data && wcscmp(text.data, L"hello\n\u041f\u0440\u0438\u0432\u0435\u0442\n100%") == 0;
    FREE_STRING(text);
    int removed = remove(name);
    ASSERT(empty_ok && no_leak && written && roundtrip_ok && removed == 0);
    text = read_utf8_file(name);
    ASSERT(text.data == NULL);
    ASSERT(get_allocated_memory_size() == before);
#ifndef _WIN32
    /* Linux may defer a write error until fclose flushes the stdio buffer. */
    file = fopen("/dev/full", "r");
    if (file) {
        fclose(file);
        ASSERT(!write_utf8_file("/dev/full", L"test"));
    }
#endif
    return true;
}
