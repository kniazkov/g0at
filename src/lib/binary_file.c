/** @file binary_file.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Binary file ownership and same-directory publication.
 */
#include "binary_file.h"

#include "allocate.h"
#include "file_system.h"

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
    FILE *file = FOPEN(path, "rb");
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
    fs_error_t error = {0};
    FILE *file = NULL;
    char *temporary = create_fs_temporary(path, false, &file, &error);
    if (!temporary) {
        clear_fs_error(&error);
        return false;
    }
    bool ok = write_fs_file(file, data, size, temporary, &error);
    if (!close_fs_file(file, temporary, &error))
        ok = false;
    if (ok)
        ok = prepare_fs_replace(temporary, path, &error);
    if (ok)
        ok = publish_fs_replace(temporary, path, &error);
    if (!ok)
        remove_fs_entry(temporary, NULL);
    FREE(temporary);
    clear_fs_error(&error);
    return ok;
}

uint64_t binary_checksum(const void *data, size_t size) {
    return extend_binary_checksum(UINT64_C(14695981039346656037), data, size);
}

uint64_t extend_binary_checksum(uint64_t value, const void *data, size_t size) {
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; i++)
        value = (value ^ bytes[i]) * UINT64_C(1099511628211);
    return value;
}
