/** @file module_search.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Ordered exact module discovery using a fixed program-wide path set.
 */
#include "module_search.h"

#include "goat/module_identity.h"
#include "lib/allocate.h"

#include <string.h>
#ifndef _WIN32
#    include <errno.h>
#endif

struct module_search {
    char *root;
    char **directories;
    size_t count;
};

static char *copy(const char *s, size_t n) {
    char *p = ALLOC(n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

static void append(char ***items, size_t *count, char *value) {
    char **next = ALLOC((*count + 1) * sizeof(char *));
    if (*count)
        memcpy(next, *items, *count * sizeof(char *));
    FREE(*items);
    *items = next;
    next[(*count)++] = value;
}

void clear_module_search_error(module_search_error_t *error) {
    if (!error)
        return;
    clear_fs_error(&error->filesystem);
    for (size_t i = 0; i < error->count; i++)
        FREE(error->attempted[i]);
    FREE(error->attempted);
    memset(error, 0, sizeof(*error));
}

void free_module_search(module_search_t *search) {
    if (!search)
        return;
    FREE(search->root);
    for (size_t i = 0; i < search->count; i++)
        FREE(search->directories[i]);
    FREE(search->directories);
    FREE(search);
}

const char *module_search_root(const module_search_t *search) {
    return search ? search->root : NULL;
}

size_t module_search_directory_count(const module_search_t *search) {
    return search ? search->count : 0;
}

const char *module_search_directory(const module_search_t *search, size_t index) {
    return search && index < search->count ? search->directories[index] : NULL;
}

static size_t parent_length(const char *path) {
    const char *slash = strrchr(path, '/');
    size_t n = slash ? (size_t)(slash - path) : 0;
    if (!n)
        return 1;
#ifdef _WIN32
    if (n == 2 && path[1] == ':')
        return 3;
#endif
    return n;
}

/* Dot components and repeated separators are safe to discard in a directory
 * prefix; dot-dot is deliberately retained until the filesystem resolves it. */
static void simplify_directory(char *path) {
    size_t read = 0, write = 0;
    if (path[0] == '/') {
        path[write++] = '/';
        read++;
        if (path[1] == '/' && path[2] != '/') {
            path[write++] = '/';
            read++;
        }
    }
    while (path[read]) {
        while (path[read] == '/')
            read++;
        size_t start = read;
        while (path[read] && path[read] != '/')
            read++;
        size_t n = read - start;
        if (!n || (n == 1 && path[start] == '.'))
            continue;
        if (write && path[write - 1] != '/')
            path[write++] = '/';
        memmove(path + write, path + start, n);
        write += n;
    }
#ifdef _WIN32
    if (write == 2 && path[1] == ':')
        path[write++] = '/';
#endif
    path[write] = 0;
}

static bool
add_directory(module_search_t *search, const char *cwd, const char *path, fs_error_t *error) {
#ifdef _WIN32
    if ((strlen(path) >= 2 && path[1] == ':'
         && (strlen(path) < 3 || (path[2] != '/' && path[2] != '\\')))
        || ((path[0] == '/' || path[0] == '\\') && path[1] != '/' && path[1] != '\\')) {
        char *invalid = canonical_fs_path(path, error);
        FREE(invalid);
        return false;
    }
    bool is_absolute = (strlen(path) >= 3 && path[1] == ':') || path[0] == '/' || path[0] == '\\';
#else
    bool is_absolute = path[0] == '/';
#endif
    char *anchored = is_absolute ? copy(path, strlen(path)) : join_fs_path(cwd, path, error);
    if (!anchored)
        return false;
    char *absolute = absolute_fs_path(anchored, error);
    FREE(anchored);
    if (!absolute)
        return false;
#ifdef _WIN32
    for (char *p = absolute; *p; p++)
        if (*p == '\\')
            *p = '/';
#endif
    simplify_directory(absolute);
    char *fixed = canonical_fs_path(absolute, error);
    if (fixed) {
        FREE(absolute);
    } else {
        /* Do not let an unavailable later directory prevent an earlier hit.
         * Preserve spelling: folding missing/.. or file/.. changes traversal. */
        if (error->status == FS_INVALID) {
            FREE(absolute);
            return false;
        }
        clear_fs_error(error);
        fixed = absolute;
    }
    for (size_t i = 0; i < search->count; i++) {
        if (!strcmp(search->directories[i], fixed)) {
            FREE(fixed);
            return true;
        }
    }
    append(&search->directories, &search->count, fixed);
    return true;
}

module_search_status_t prepare_module_search(const char *root,
                                             const char *module_dir,
                                             const char *environment,
                                             module_search_t **out,
                                             module_search_error_t *error) {
    module_search_error_t local = {0};
    module_search_error_t *e = error ? error : &local;
    clear_module_search_error(e);
    if (out)
        *out = NULL;
    module_search_t *search = NULL;
    char *cwd = NULL;
    if (!out || !root || !*root || (module_dir && !*module_dir)) {
        e->status = MODULE_SEARCH_ARGUMENT;
        goto done;
    }
    search = ALLOC(sizeof(*search));
    memset(search, 0, sizeof(*search));
    cwd = canonical_fs_path(".", &e->filesystem);
    if (!cwd)
        goto io;
    search->root = canonical_fs_path(root, &e->filesystem);
    if (!search->root)
        goto io;
    char *parent = copy(search->root, parent_length(search->root));
    char *modules = join_fs_path(parent, "modules", &e->filesystem);
    FREE(parent);
    bool added = modules && add_directory(search, cwd, modules, &e->filesystem);
    FREE(modules);
    if (!added || (module_dir && !add_directory(search, cwd, module_dir, &e->filesystem)))
        goto io;
#ifdef _WIN32
    const char separator = ';';
#else
    const char separator = ':';
#endif
    for (const char *p = environment; p && *p;) {
        const char *end = strchr(p, separator);
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (n) {
            char *part = copy(p, n);
            added = add_directory(search, cwd, part, &e->filesystem);
            FREE(part);
            if (!added)
                goto io;
        }
        p = end ? end + 1 : NULL;
    }
    *out = search;
    search = NULL;
    goto done;
io:
    e->status = MODULE_SEARCH_IO;
done:
    FREE(cwd);
    free_module_search(search);
    module_search_status_t status = e->status;
    clear_module_search_error(&local);
    return status;
}

static bool same(yyjson_val *metadata, const char *key, const char *expected) {
    yyjson_val *v = yyjson_obj_get(metadata, key);
    return yyjson_is_str(v) && yyjson_get_len(v) == strlen(expected)
           && !memcmp(yyjson_get_str(v), expected, strlen(expected));
}

module_search_status_t find_module(const module_search_t *search,
                                   const char *name,
                                   const char *version,
                                   const char *expected_hash,
                                   const module_validation_options_t *options,
                                   validated_module_t **out,
                                   module_search_error_t *error) {
    module_search_error_t local = {0};
    module_search_error_t *e = error ? error : &local;
    clear_module_search_error(e);
    if (out)
        *out = NULL;
    if (!search || !out || !name || !version || !goat_module_name_valid(name, strlen(name))
        || !goat_module_version_valid(version, strlen(version))) {
        e->status = MODULE_SEARCH_ARGUMENT;
        goto done;
    }
    if (expected_hash) {
        if (strlen(expected_hash) != 64) {
            e->status = MODULE_SEARCH_ARGUMENT;
            goto done;
        }
        for (size_t i = 0; i < 64; i++)
            if (!(expected_hash[i] >= '0' && expected_hash[i] <= '9')
                && !(expected_hash[i] >= 'a' && expected_hash[i] <= 'f')) {
                e->status = MODULE_SEARCH_ARGUMENT;
                goto done;
            }
    }
    char filename[GOAT_MODULE_VERSION_MAX_LENGTH + sizeof(".gbin")];
    strcpy(filename, version);
    strcat(filename, ".gbin");
    for (size_t i = 0; i < search->count; i++) {
        char *directory = join_fs_path(search->directories[i], name, &e->filesystem);
        char *path = directory ? join_fs_path(directory, filename, &e->filesystem) : NULL;
        FREE(directory);
        if (!path) {
            e->status = MODULE_SEARCH_IO;
            goto done;
        }
        append(&e->attempted, &e->count, path);
        fs_info_t info;
        if (!inspect_fs_path(path, false, &info, &e->filesystem)) {
            if (e->filesystem.status == FS_NOT_FOUND
#ifndef _WIN32
                && e->filesystem.native_code != ENOTDIR
#endif
            ) {
                clear_fs_error(&e->filesystem);
                continue;
            }
            e->status = MODULE_SEARCH_IO;
            goto done;
        }
        void *data = NULL;
        size_t size = 0, limit = UINT32_MAX - 1;
        if (options && options->max_archive < limit)
            limit = options->max_archive;
        if (!read_fs_regular_file(path, limit, &data, &size, &e->filesystem)) {
            e->status = MODULE_SEARCH_IO;
            goto done;
        }
        validated_module_t *module = NULL;
        e->validation = validate_module(data, size, path, options, &module);
        FREE(data);
        if (e->validation.status != MODULE_VALIDATION_OK) {
            e->status = MODULE_SEARCH_INVALID_MODULE;
            free_validated_module(module);
            goto done;
        }
        const yyjson_doc *doc = goat_module_container_metadata(validated_module_container(module));
        yyjson_val *metadata = yyjson_doc_get_root(doc);
        if (!same(metadata, "name", name) || !same(metadata, "version", version))
            e->status = MODULE_SEARCH_IDENTITY;
        else if (expected_hash && !same(metadata, "mainHash", expected_hash))
            e->status = MODULE_SEARCH_HASH;
        if (e->status)
            free_validated_module(module);
        else
            *out = module;
        goto done;
    }
    e->status = MODULE_SEARCH_NOT_FOUND;
done:;
    module_search_status_t status = e->status;
    clear_module_search_error(&local);
    return status;
}
