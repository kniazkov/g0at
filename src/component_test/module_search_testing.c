/** @file module_search_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Fixed search paths and first-candidate validation fixture driver.
 */
#ifndef _WIN32
#    define _POSIX_C_SOURCE 200809L
#endif
#include "lib/allocate.h"
#include "vm/module_search.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <direct.h>
void begin_module_search_permissions(void);
void end_module_search_permissions(void);
#    define chdir _chdir
#else
#    include <unistd.h>
#endif

static void quoted(const char *text) {
    if (!text) {
        fputs("null", stdout);
        return;
    }
    putchar('"');
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (*p == '"' || *p == '\\')
            putchar('\\');
        if (*p < 32)
            printf("\\u%04x", *p);
        else
            putchar(*p);
    }
    putchar('"');
}

int main(int argc, char **argv) {
    if (argc == 1) {
        module_search_error_t error = {0};
        module_search_t *search = (void *)(uintptr_t)1;
        assert(prepare_module_search(NULL, NULL, NULL, &search, &error) == MODULE_SEARCH_ARGUMENT);
        assert(!search && error.status == MODULE_SEARCH_ARGUMENT);
        assert(prepare_module_search(".", NULL, NULL, NULL, NULL) == MODULE_SEARCH_ARGUMENT);
        validated_module_t *module = (void *)(uintptr_t)1;
        assert(find_module(NULL, "sample", "1", NULL, NULL, &module, &error)
               == MODULE_SEARCH_ARGUMENT);
        assert(!module && !error.count);
        assert(!module_search_root(NULL) && !module_search_directory_count(NULL));
        assert(!module_search_directory(NULL, 0));
        clear_module_search_error(&error);
        free_module_search(NULL);
        clear_module_search_error(NULL);
        puts("Search API cleanup checks passed");
        return 0;
    }
#ifdef _WIN32
    fs_error_t arguments_error = {0};
    assert(fs_arguments(&argc, &argv, &arguments_error));
    clear_fs_error(&arguments_error);
    begin_module_search_permissions();
#endif
    assert(argc == 10);
    module_search_t *search = NULL;
    module_search_error_t error = {0};
    module_search_status_t status;
    if (!strcmp(argv[8], "process"))
        status = prepare_module_search_from_environment(argv[1],
                                                        *argv[2] ? argv[2] : NULL,
                                                        &search,
                                                        &error);
    else
        status = prepare_module_search(argv[1],
                                       *argv[2] ? argv[2] : NULL,
                                       !strcmp(argv[8], "absent") ? NULL : argv[3],
                                       &search,
                                       &error);
    printf("{\"status\":%d,\"root\":", status);
    quoted(search ? module_search_root(search) : NULL);
    fputs(",\"directories\":[", stdout);
    if (search) {
        for (size_t i = 0; i < module_search_directory_count(search); i++) {
            if (i)
                putchar(',');
            quoted(module_search_directory(search, i));
        }
    }
    puts("]}");
    fflush(stdout);
    if (status == MODULE_SEARCH_OK) {
        /* Python may retarget the root link after preparation, before lookup. */
        assert(getchar() == '\n');
        if (*argv[9]) {
#ifdef _WIN32
            assert(!_putenv_s("GOAT_MODULE_PATH", argv[9]));
#else
            assert(!setenv("GOAT_MODULE_PATH", argv[9], 1));
#endif
        }
        if (*argv[7])
            assert(!chdir(argv[7]));
        validated_module_t *module = (void *)(uintptr_t)1;
        status =
            find_module(search, argv[4], argv[5], *argv[6] ? argv[6] : NULL, NULL, &module, &error);
        assert((status == MODULE_SEARCH_OK) == (module != NULL));
        printf("{\"status\":%d,\"filesystem\":%d,\"validation\":%d,\"attempted\":[",
               status,
               error.filesystem.status,
               error.validation.status);
        for (size_t i = 0; i < error.count; i++) {
            if (i)
                putchar(',');
            quoted(error.attempted[i]);
        }
        fputs("],\"native_code\":", stdout);
        printf("%lu", error.filesystem.native_code);
        fputs(",\"operation\":", stdout);
        quoted(error.filesystem.operation);
        fputs(",\"error_path\":", stdout);
        quoted(error.filesystem.path);
        char *diagnostic = format_module_search_error(&error, argv[4], argv[5], "fixture.goat");
        fputs(",\"diagnostic\":", stdout);
        quoted(diagnostic);
        FREE(diagnostic);
        fputs(",\"archive\":", stdout);
        quoted(module ? validated_module_archive(module) : NULL);
        /* Reuse one graph-wide search set; clearing prior diagnostics cannot
         * invalidate a previously returned snapshot. */
        if (module) {
            validated_module_t *second = NULL;
            assert(find_module(search,
                               argv[4],
                               argv[5],
                               *argv[6] ? argv[6] : NULL,
                               NULL,
                               &second,
                               &error)
                   == MODULE_SEARCH_OK);
            assert(second && second != module);
            const bytecode_t *a = validated_module_bytecode(module);
            const bytecode_t *b = validated_module_bytecode(second);
            assert(a->buffer != b->buffer && a->buffer_size == b->buffer_size);
            assert(!memcmp(a->buffer, b->buffer, a->buffer_size));
            free_validated_module(second);
        }
        /* Snapshot ownership is independent of search/error objects. */
        free_module_search(search);
        search = NULL;
        clear_module_search_error(&error);
        if (module) {
            const yyjson_doc *metadata =
                goat_module_container_metadata(validated_module_container(module));
            assert(validated_module_bytecode(module)->instructions_count);
            fputs(",\"hash\":", stdout);
            quoted(yyjson_get_str(yyjson_obj_get(yyjson_doc_get_root(metadata), "mainHash")));
        }
        puts("}");
        free_validated_module(module);
    }
    free_module_search(search);
    clear_module_search_error(&error);
#ifdef _WIN32
    end_module_search_permissions();
    free_fs_arguments(argc, argv);
#endif
    assert(!get_allocated_memory_size());
    return 0;
}
