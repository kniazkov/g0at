/** @file file_system_linux.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Linux descriptor-relative traversal and POSIX ACL preservation.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "file_system_internal.h"
#ifndef _WIN32
#include "allocate.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

bool fs_native_error(fs_error_t *error, const char *operation, const char *path) {
    int code = errno;
    fs_status_t status = code == ENOENT || code == ENOTDIR  ? FS_NOT_FOUND
                         : code == EACCES || code == EPERM  ? FS_ACCESS
                         : code == EEXIST                   ? FS_EXISTS
                         : code == ENOTSUP || code == EXDEV ? FS_UNSUPPORTED
                                                            : FS_IO;
    return fs_fail(error, status, code, operation, path);
}

FILE *FOPEN(const char *path, const char *mode) {
    if (!fs_valid(path, NULL)) {
        errno = EILSEQ;
        return NULL;
    }
    return fopen(path, mode);
}

char *absolute_fs_path(const char *path, fs_error_t *error) {
    if (!fs_valid(path, error))
        return NULL;
    if (path[0] == '/') {
        char *out = ALLOC(strlen(path) + 1);
        strcpy(out, path);
        return out;
    }
    char *cwd = getcwd(NULL, 0);
    if (!cwd) {
        fs_native_error(error, "getcwd", path);
        return NULL;
    }
    char *out = join_fs_path(cwd, path, error);
    free(cwd);
    return out;
}

char *fs_environment(const char *name, fs_error_t *error) {
    if (!fs_valid(name, error))
        return NULL;
    const char *value = getenv(name);
    if (!value) {
        fs_fail(error, FS_NOT_FOUND, ENOENT, "environment", name);
        return NULL;
    }
    if (!fs_valid(value, error))
        return NULL;
    char *out = ALLOC(strlen(value) + 1);
    strcpy(out, value);
    return out;
}

char *temporary_fs_directory(fs_error_t *error) {
    const char *base = getenv("TMPDIR");
    return absolute_fs_path(base && *base ? base : "/tmp", error);
}

static void info_from_stat(const struct stat *s, fs_info_t *info) {
    *info = (fs_info_t){.kind = S_ISREG(s->st_mode)   ? FS_FILE
                                : S_ISDIR(s->st_mode) ? FS_DIRECTORY
                                : S_ISLNK(s->st_mode) ? FS_LINK
                                                      : FS_OTHER,
                        .size = s->st_size,
                        .device = s->st_dev,
                        .identity = s->st_ino};
}

bool inspect_fs_path(const char *path, bool follow_links, fs_info_t *info, fs_error_t *error) {
    if (!fs_valid(path, error) || !info)
        return false;
    struct stat s;
    if ((follow_links ? stat(path, &s) : lstat(path, &s)))
        return fs_native_error(error, "stat", path);
    info_from_stat(&s, info);
    return true;
}

bool create_fs_directory(const char *path, fs_error_t *error) {
    if (!fs_valid(path, error))
        return false;
    return !mkdir(path, 0777) || fs_native_error(error, "mkdir", path);
}

char *create_fs_temporary(const char *prefix, bool directory, FILE **file, fs_error_t *error) {
    if (file)
        *file = NULL;
    if (!fs_valid(prefix, error) || (!directory && !file))
        return NULL;
    char *path = ALLOC(strlen(prefix) + 40);
    for (int attempt = 0; attempt < 128; attempt++) {
        unsigned char random[16];
        if (getrandom(random, sizeof(random), 0) != sizeof(random)) {
            fs_native_error(error, "random", prefix);
            break;
        }
        strcpy(path, prefix);
        size_t n = strlen(path);
        path[n++] = '-';
        for (size_t i = 0; i < sizeof(random); i++) {
            sprintf(path + n, "%02x", random[i]);
            n += 2;
        }
        int fd = -1;
        bool ok = directory ? !mkdir(path, 0700)
                            : (fd = open(path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0666)) >= 0;
        if (ok) {
            if (directory)
                return path;
            *file = fdopen(fd, "w+b");
            if (*file)
                return path;
            int saved = errno;
            close(fd);
            unlink(path);
            errno = saved;
            fs_native_error(error, "fdopen", path);
            break;
        }
        if (errno != EEXIST) {
            fs_native_error(error, "create temporary", path);
            break;
        }
        if (attempt == 127)
            fs_native_error(error, "create temporary", path);
    }
    FREE(path);
    return NULL;
}

bool list_fs_directory(const char *path, fs_entries_t *entries, fs_error_t *error) {
    if (!entries || !fs_valid(path, error))
        return false;
    *entries = (fs_entries_t){0};
    DIR *dir = opendir(path);
    if (!dir)
        return fs_native_error(error, "opendir", path);
    bool ok = true;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry) {
            if (errno)
                ok = fs_native_error(error, "readdir", path);
            break;
        }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        if (!fs_valid(entry->d_name, error)) {
            ok = false;
            break;
        }
        char **names = ALLOC((entries->count + 1) * sizeof(*names));
        if (entries->count)
            memcpy(names, entries->names, entries->count * sizeof(*names));
        FREE(entries->names);
        entries->names = names;
        names[entries->count] = ALLOC(strlen(entry->d_name) + 1);
        strcpy(names[entries->count++], entry->d_name);
    }
    if (closedir(dir) && ok)
        ok = fs_native_error(error, "closedir", path);
    if (!ok)
        clear_fs_entries(entries);
    return ok;
}

bool remove_fs_entry(const char *path, fs_error_t *error) {
    if (!fs_valid(path, error))
        return false;
    return !remove(path) || fs_native_error(error, "remove", path);
}

/* Every descent is relative to an already opened directory and uses O_NOFOLLOW.
 * A changed identity is an error, never a reason to follow the new object.
 */
static bool remove_at(int parent,
                      const char *name,
                      const char *path,
                      dev_t device,
                      unsigned depth,
                      fs_error_t *error) {
    if (depth > 256)
        return fs_fail(error, FS_UNSUPPORTED, ELOOP, "tree depth", path);
    struct stat before, opened, current;
    if (fstatat(parent, name, &before, AT_SYMLINK_NOFOLLOW))
        return fs_native_error(error, "stat tree", path);
    if (!S_ISDIR(before.st_mode))
        return !unlinkat(parent, name, 0) || fs_native_error(error, "unlink", path);
    if (before.st_dev != device)
        return fs_fail(error, FS_UNSUPPORTED, EXDEV, "mount boundary", path);
    int fd = openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        return fs_native_error(error, "open tree", path);
    bool ok =
        !fstat(fd, &opened) && opened.st_dev == before.st_dev && opened.st_ino == before.st_ino;
    if (!ok) {
        close(fd);
        return fs_fail(error, FS_CHANGED, ESTALE, "tree identity", path);
    }
    DIR *dir = fdopendir(fd);
    if (!dir) {
        int saved = errno;
        close(fd);
        errno = saved;
        return fs_native_error(error, "fdopendir", path);
    }
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry) {
            if (errno)
                ok = fs_native_error(error, "readdir tree", path);
            break;
        }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        char *child = join_fs_path(path, entry->d_name, error);
        if (!child) {
            ok = false;
            break;
        }
        ok = remove_at(fd, entry->d_name, child, device, depth + 1, error);
        FREE(child);
        if (!ok)
            break;
    }
    if (closedir(dir) && ok)
        ok = fs_native_error(error, "closedir tree", path);
    if (ok
        && (fstatat(parent, name, &current, AT_SYMLINK_NOFOLLOW) || current.st_dev != before.st_dev
            || current.st_ino != before.st_ino))
        ok = fs_fail(error, FS_CHANGED, ESTALE, "tree identity", path);
    if (ok && unlinkat(parent, name, AT_REMOVEDIR))
        ok = fs_native_error(error, "rmdir", path);
    return ok;
}

bool remove_fs_tree(const char *path, fs_error_t *error) {
    char *absolute = absolute_fs_path(path, error);
    if (!absolute)
        return false;
    size_t n = strlen(absolute);
    while (n > 1 && absolute[n - 1] == '/')
        absolute[--n] = 0;
    char *slash = strrchr(absolute, '/');
    char *name = slash + 1;
    if (!*name || !strcmp(name, ".") || !strcmp(name, "..")) {
        FREE(absolute);
        return fs_fail(error, FS_INVALID, EINVAL, "remove tree root", path);
    }
    *slash = 0;
    int parent = open(*absolute ? absolute : "/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    bool ok = false;
    struct stat s;
    if (parent < 0)
        fs_native_error(error, "open parent", path);
    else {
        if (fstatat(parent, name, &s, AT_SYMLINK_NOFOLLOW))
            fs_native_error(error, "stat root", path);
        else
            ok = remove_at(parent, name, path, s.st_dev, 0, error);
        if (close(parent) && ok)
            ok = fs_native_error(error, "close parent", path);
    }
    FREE(absolute);
    return ok;
}

bool prepare_fs_replace(const char *prepared, const char *destination, fs_error_t *error) {
    if (!fs_valid(prepared, error) || !fs_valid(destination, error))
        return false;
    int target = open(prepared, O_WRONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (target < 0)
        return fs_native_error(error, "open prepared", prepared);
    struct stat t, s;
    bool ok = !fstat(target, &t) && S_ISREG(t.st_mode) && t.st_nlink == 1;
    if (!ok)
        fs_fail(error, FS_INVALID, EINVAL, "prepared regular unique file", prepared);
    struct stat entry;
    bool link = ok && !lstat(destination, &entry) && S_ISLNK(entry.st_mode);
    int source =
        ok && !link ? open(destination, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK) : -1;
    if (ok && !link && source < 0 && errno != ENOENT)
        ok = fs_native_error(error, "open permissions", destination);
    if (ok && source >= 0) {
        if (fstat(source, &s) || !S_ISREG(s.st_mode))
            ok = fs_fail(error, FS_INVALID, EINVAL, "destination regular file", destination);
        if (ok && fchown(target, s.st_uid, s.st_gid))
            ok = fs_native_error(error, "copy owner", prepared);
        if (ok && fchmod(target, s.st_mode & 07777))
            ok = fs_native_error(error, "copy mode", prepared);
        if (ok) {
            ssize_t size = fgetxattr(source, "system.posix_acl_access", NULL, 0);
            if (size < 0 && errno != ENODATA)
                ok = fs_native_error(error, "read ACL", destination);
            else if (size >= 0) {
                void *acl = ALLOC(size ? (size_t)size : 1);
                if (fgetxattr(source, "system.posix_acl_access", acl, size) != size)
                    ok = fs_native_error(error, "read ACL", destination);
                else if (fsetxattr(target, "system.posix_acl_access", acl, size, 0))
                    ok = fs_native_error(error, "copy ACL", prepared);
                FREE(acl);
            } else if (fremovexattr(target, "system.posix_acl_access") && errno != ENODATA
                       && errno != ENOTSUP)
                ok = fs_native_error(error, "clear inherited ACL", prepared);
        }
    }
    if (source >= 0 && close(source) && ok)
        ok = fs_native_error(error, "close permissions", destination);
    if (close(target) && ok)
        ok = fs_native_error(error, "close prepared", prepared);
    return ok;
}

bool publish_fs_replace(const char *prepared, const char *destination, fs_error_t *error) {
    if (!fs_valid(prepared, error) || !fs_valid(destination, error))
        return false;
    return !rename(prepared, destination) || fs_native_error(error, "publish rename", destination);
}

bool fs_arguments(int *argc, char ***argv, fs_error_t *error) {
    if (!argc || !argv)
        return fs_fail(error, FS_INVALID, EINVAL, "arguments", NULL);
    char **copy = CALLOC(((size_t)*argc + 1) * sizeof(*copy));
    for (int i = 0; i < *argc; i++) {
        /* Empty non-path arguments are legal. */
        if ((*argv)[i][0] && !fs_valid((*argv)[i], error)) {
            free_fs_arguments(i, copy);
            return false;
        }
        copy[i] = ALLOC(strlen((*argv)[i]) + 1);
        strcpy(copy[i], (*argv)[i]);
    }
    *argv = copy;
    return true;
}
#endif
