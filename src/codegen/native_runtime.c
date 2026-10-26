/** @file native_runtime.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Relocatable native support library bundled with the interpreter.
 */
#include "native_runtime.h"

#include "lib/allocate.h"
#include "lib/file_system.h"
#include "native_runtime_data.inc"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#    include <sys/stat.h>
#endif

static const struct {
    const char *name;
    const unsigned char *data;
    size_t size;
} files[] = {{"unicode.h", unicode_data, sizeof(unicode_data)},
             {"native_abi.h", native_abi_data, sizeof(native_abi_data)},
             {"native_runtime.h", native_runtime_data, sizeof(native_runtime_data)},
             {"native_strings.h", native_strings_data, sizeof(native_strings_data)},
             {"module_abi.h", module_abi_data, sizeof(module_abi_data)},
             {GOAT_NATIVE_ARCHIVE_NAME, native_archive_data, sizeof(native_archive_data)}};

static char *runtime_path(const char *source, const char *name) {
    size_t prefix = 0;
    for (size_t i = 0; source[i]; i++)
        if (source[i] == '/' || source[i] == '\\')
            prefix = i + 1;
    char *path = ALLOC(prefix + strlen(name) + 1);
    memcpy(path, source, prefix);
    strcpy(path + prefix, name);
    return path;
}

static bool write_runtime_file(const char *path, const unsigned char *data, size_t size) {
    FILE *file = FOPEN(path, "rb");
    if (file) {
        bool same = true;
        for (size_t i = 0; i < size; i++) {
            if (FGETC(file) != data[i]) {
                same = false;
                break;
            }
        }
        same = same && FGETC(file) == EOF && !FERROR(file);
        FCLOSE(file);
        if (!same)
            errno = EEXIST;
        return same;
    }
    if (errno != ENOENT)
        return false;
    file = FOPEN(path, "wbx");
    if (!file)
        return false;
    bool success = FWRITE(data, 1, size, file) == size;
    if (FCLOSE(file))
        success = false;
    if (!success)
        remove_fs_entry(path, NULL);
    return success;
}

bool write_native_runtime_files(const char *source_path) {
    for (size_t i = 0; i < sizeof(files) / sizeof(*files); i++) {
        char *path = runtime_path(source_path, files[i].name);
        bool success = write_runtime_file(path, files[i].data, files[i].size);
        FREE(path);
        if (!success)
            return false;
    }
    return true;
}

void remove_native_runtime_files(const char *source_path) {
    for (size_t i = 0; i < sizeof(files) / sizeof(*files); i++) {
        char *path = runtime_path(source_path, files[i].name);
        remove_fs_entry(path, NULL);
        FREE(path);
    }
}

bool write_native_runtime_sources(const char *source_path) {
    for (size_t i = 0; i + 1 < sizeof(files) / sizeof(*files); ++i) {
        char *path = runtime_path(source_path, files[i].name);
        bool success = write_runtime_file(path, files[i].data, files[i].size);
        FREE(path);
        if (!success)
            return false;
    }

    static const struct {
        const char *name;
        const unsigned char *data;
        size_t size;
    } sources[] = {
        {"runtime_strings.c", runtime_strings_data, sizeof(runtime_strings_data)},
        {"runtime_str.c", runtime_str_data, sizeof(runtime_str_data)},
        {"runtime_number_format.c", runtime_number_format_data, sizeof(runtime_number_format_data)},
        {"runtime_unicode.c", runtime_unicode_data, sizeof(runtime_unicode_data)},
        {"number_format.h", number_format_header_data, sizeof(number_format_header_data)},
        {"unicode_data.inc", unicode_table_data, sizeof(unicode_table_data)}};

    for (size_t i = 0; i < sizeof(sources) / sizeof(*sources); ++i) {
        char *path = runtime_path(source_path, sources[i].name);
        bool success = write_runtime_file(path, sources[i].data, sources[i].size);
        FREE(path);
        if (!success)
            return false;
    }
    return true;
}
