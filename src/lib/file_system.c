/** @file file_system.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Common filesystem contracts and checked stream operations.
 */
#include "file_system.h"

#include "allocate.h"
#include "file_system_internal.h"
#include "string_ext.h"

#include <errno.h>
#include <string.h>

void clear_fs_error(fs_error_t *error) {
    if (error) {
        FREE(error->path);
        *error = (fs_error_t){0};
    }
}

bool fs_fail(fs_error_t *error,
             fs_status_t status,
             unsigned long code,
             const char *operation,
             const char *path) {
    if (error) {
        clear_fs_error(error);
        error->status = status;
        error->native_code = code;
        error->operation = operation;
        if (path) {
            error->path = ALLOC(strlen(path) + 1);
            strcpy(error->path, path);
        }
    }
    return false;
}

bool validate_fs_path(const char *path, size_t length, fs_error_t *error) {
    bool valid = path && length && !memchr(path, 0, length);
    if (valid) {
        string_value_t decoded = decode_utf8_n(path, length);
        valid = decoded.data != NULL;
        FREE_STRING(decoded);
    }
    if (valid)
        return true;
    fs_fail(error, FS_INVALID, EILSEQ, "validate UTF-8 path", NULL);
    if (error && path) {
        error->path = ALLOC(length + 1);
        memcpy(error->path, path, length);
        error->path[length] = 0;
    }
    return false;
}

bool fs_valid(const char *path, fs_error_t *error) {
    return validate_fs_path(path, path ? strlen(path) : 0, error);
}

char *join_fs_path(const char *directory, const char *name, fs_error_t *error) {
    if (!fs_valid(directory, error) || !fs_valid(name, error))
        return NULL;
    size_t n = strlen(directory), m = strlen(name);
    char *out = ALLOC(n + m + 2);
    memcpy(out, directory, n);
    out[n] = '/';
    memcpy(out + n + 1, name, m + 1);
    return out;
}

void clear_fs_entries(fs_entries_t *entries) {
    for (size_t i = 0; i < entries->count; i++)
        FREE(entries->names[i]);
    FREE(entries->names);
    *entries = (fs_entries_t){0};
}

bool write_fs_file(FILE *file, const void *data, size_t size, const char *path, fs_error_t *error) {
    if (!file || (!data && size))
        return fs_fail(error, FS_INVALID, EINVAL, "write", path);
    if (FWRITE(data, 1, size, file) != size)
        return fs_fail(error, FS_IO, errno ? errno : EIO, "write", path);
    return true;
}

bool close_fs_file(FILE *file, const char *path, fs_error_t *error) {
    if (!file)
        return fs_fail(error, FS_INVALID, EINVAL, "close", path);
    if (FCLOSE(file))
        return fs_fail(error, FS_IO, errno ? errno : EIO, "close", path);
    return true;
}

void free_fs_arguments(int argc, char **argv) {
    for (int i = 0; i < argc; i++)
        FREE(argv[i]);
    FREE(argv);
}

bool publish_fs_replace(const char *prepared, const char *destination, fs_error_t *error) {
    fs_info_t info;
    if (!inspect_fs_path(prepared, false, &info, error))
        return false;
    if (info.kind != FS_FILE || info.hard_links != 1)
        return fs_fail(error, FS_INVALID, EINVAL, "publish regular unique file", prepared);
    return rename_fs_path(prepared, destination, error);
}
