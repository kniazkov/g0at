/**
 * @file path.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of platform-specific path manipulation utilities.
 */

#include <string.h>
#ifdef _WIN32
#    include <windows.h>
#else
#    include <limits.h>
#    include <stdlib.h>
#    include <sys/stat.h>
#endif

#include "lib/allocate.h"
#include "path.h"

#ifdef _WIN32
#    define PATH_SEPARATOR '\\'
#else
#    define PATH_SEPARATOR '/'
#endif

/**
 * @brief Function to normalize slashes in file paths based on the current operating system.
 * @note The returned string is dynamically allocated. It is the caller's responsibility to free the
 * memory when it is no longer needed to avoid memory leaks.
 */
static char *normalize_path(const char *path) {
    size_t len = strlen(path);
    char *normal = (char *)ALLOC(len + 1);
    for (size_t index = 0; index < len; index++) {
        if (path[index] == '/' || path[index] == '\\') {
            normal[index] = PATH_SEPARATOR;
        } else {
            normal[index] = path[index];
        }
    }
    normal[len] = '\0';
    return normal;
}

/**
 * @brief Converts a relative path to absolute path.
 * @return Absolute path or NULL if absolute path can't be extracted.
 */
static char *get_absolute_path(const char *rel_path) {
    char *full_path = NULL;
#ifdef _WIN32
    DWORD size = GetFullPathNameA(rel_path, 0, NULL, NULL);
    if (size == 0) {
        return NULL;
    }
    full_path = (char *)ALLOC(size);
    if (GetFullPathNameA(rel_path, size, full_path, NULL) == 0) {
        FREE(full_path);
        return NULL;
    }
#else
    full_path = (char *)ALLOC(PATH_MAX);
    if (!realpath(rel_path, full_path)) {
        FREE(full_path);
        return NULL;
    }
#endif
    return full_path;
}

path_t *create_path(const char *input) {
    path_t *p = (path_t *)CALLOC(sizeof(path_t));
    if (input && *input) {
        p->normal_path = normalize_path(input);
        p->full_path = get_absolute_path(p->normal_path);
        if (p->full_path == NULL) {
            size_t length = strlen(p->normal_path) + 1;
            p->full_path = ALLOC(length);
            memcpy(p->full_path, p->normal_path, length);
        }
        char *last_separator = strrchr(p->full_path, PATH_SEPARATOR);
        if (last_separator) {
            char *name = last_separator + 1;
            if (*name != '\0') {
                p->file_name = name;
            }
            size_t dir_len = last_separator - p->full_path;
            if (dir_len > 0) {
                p->dir_name = ALLOC(dir_len + 1);
                memcpy(p->dir_name, p->full_path, dir_len);
                p->dir_name[dir_len] = '\0';
            }
        } else {
            p->file_name = p->full_path;
        }
        if (p->file_name) {
            char *dot = strrchr(p->file_name, '.');
            if (dot && dot != p->file_name) {
                p->extension = dot + 1;
            }
        }
    }
    return p;
}

void free_path(path_t *path) {
    if (!path) {
        return;
    }
    FREE(path->normal_path);
    FREE(path->full_path);
    FREE(path->dir_name);
    FREE(path);
}

bool paths_refer_to_same_file(const path_t *left, const path_t *right) {
    if (!left || !right || !left->full_path || !right->full_path)
        return false;
#ifdef _WIN32
    if (!strcasecmp(left->full_path, right->full_path))
        return true;
    HANDLE a = CreateFileA(left->full_path,
                           FILE_READ_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL,
                           OPEN_EXISTING,
                           0,
                           NULL);
    HANDLE b = CreateFileA(right->full_path,
                           FILE_READ_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL,
                           OPEN_EXISTING,
                           0,
                           NULL);
    BY_HANDLE_FILE_INFORMATION x, y;
    bool same = a != INVALID_HANDLE_VALUE && b != INVALID_HANDLE_VALUE
                && GetFileInformationByHandle(a, &x) && GetFileInformationByHandle(b, &y)
                && x.dwVolumeSerialNumber == y.dwVolumeSerialNumber
                && x.nFileIndexHigh == y.nFileIndexHigh && x.nFileIndexLow == y.nFileIndexLow;
    if (a != INVALID_HANDLE_VALUE)
        CloseHandle(a);
    if (b != INVALID_HANDLE_VALUE)
        CloseHandle(b);
    return same;
#else
    if (!strcmp(left->full_path, right->full_path))
        return true;
    struct stat a, b;
    return !stat(left->full_path, &a) && !stat(right->full_path, &b) && a.st_dev == b.st_dev
           && a.st_ino == b.st_ino;
#endif
}
