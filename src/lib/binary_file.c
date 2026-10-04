/** @file binary_file.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Binary file ownership and same-directory publication.
 */
#include "binary_file.h"

#include "allocate.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#    include <io.h>
#    include <process.h>
#    include <windows.h>
#else
#    include <unistd.h>
#endif

void *read_binary_file(const char *path, size_t limit, size_t *size) {
    *size = 0;
    FILE *file = fopen(path, "rb");
    if (!file)
        return NULL;
    long length = -1;
    if (!fseek(file, 0, SEEK_END))
        length = ftell(file);
    if (length < 0 || (uint64_t)length > limit || fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return NULL;
    }
    void *data = ALLOC(length ? (size_t)length : 1);
    bool ok = fread(data, 1, length, file) == (size_t)length && fgetc(file) == EOF && !ferror(file);
    if (fclose(file))
        ok = false;
    if (!ok) {
        FREE(data);
        return NULL;
    }
    *size = (size_t)length;
    return data;
}

bool write_binary_file(const char *path, const void *data, size_t size) {
    size_t length = strlen(path) + 64;
    char *temporary = ALLOC(length);
    int fd = -1;
    for (unsigned i = 0; i < 128 && fd < 0; i++) {
#ifdef _WIN32
        snprintf(temporary, length, "%s.tmp-%lu-%u", path, (unsigned long)_getpid(), i);
        fd = _open(temporary, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
        snprintf(temporary, length, "%s.tmp-%lu-%u", path, (unsigned long)getpid(), i);
        fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0600);
#endif
    }
    bool ok = false;
    if (fd >= 0) {
#ifdef _WIN32
        FILE *file = _fdopen(fd, "wb");
#else
        FILE *file = fdopen(fd, "wb");
#endif
        if (file) {
            ok = fwrite(data, 1, size, file) == size;
            if (fclose(file))
                ok = false;
        } else {
#ifdef _WIN32
            _close(fd);
#else
            close(fd);
#endif
        }
        if (ok) {
#ifdef _WIN32
            ok = MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
                 != 0;
#else
            ok = rename(temporary, path) == 0;
#endif
        }
        if (!ok)
            remove(temporary);
    }
    FREE(temporary);
    return ok;
}

uint64_t binary_checksum(const void *data, size_t size) {
    const unsigned char *bytes = data;
    uint64_t value = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < size; i++)
        value = (value ^ bytes[i]) * UINT64_C(1099511628211);
    return value;
}
