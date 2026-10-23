/** @file native_runtime.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Relocatable native support library bundled with the interpreter.
 */
#include "native_runtime.h"

#include "lib/allocate.h"
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
    FILE *file = fopen(path, "rb");
    if (file) {
        bool same = true;
        for (size_t i = 0; i < size; i++) {
            if (fgetc(file) != data[i]) {
                same = false;
                break;
            }
        }
        same = same && fgetc(file) == EOF && !ferror(file);
        fclose(file);
        if (!same)
            errno = EEXIST;
        return same;
    }
    if (errno != ENOENT)
        return false;
#ifdef _WIN32
    /* The legacy MSVCRT used by MinGW does not support fopen's C11 'x' mode. */
    int fd = _open(path, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
    if (fd < 0)
        return false;
    file = _fdopen(fd, "wb");
    if (!file) {
        int error = errno;
        _close(fd);
        remove(path);
        errno = error;
        return false;
    }
#else
    file = fopen(path, "wbx");
#endif
    if (!file)
        return false;
    bool success = fwrite(data, 1, size, file) == size;
    if (fclose(file))
        success = false;
    if (!success)
        remove(path);
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
        remove(path);
        FREE(path);
    }
}
