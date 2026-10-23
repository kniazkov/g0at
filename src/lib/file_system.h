/** @file file_system.h
 * @copyright 2026 Ivan Kniazkov
 * @brief UTF-8 filesystem operations with explicit errors and staged publication.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/** @brief Portable error category; native_code retains errno or a Windows error. */
typedef enum {
    FS_OK,
    FS_INVALID,
    FS_NOT_FOUND,
    FS_ACCESS,
    FS_EXISTS,
    FS_UNSUPPORTED,
    FS_CHANGED,
    FS_IO
} fs_status_t;

/** @brief Initialize with {0}; clear before releasing. path is owned, operation is static. */
typedef struct {
    fs_status_t status;
    unsigned long native_code;
    const char *operation;
    char *path;
} fs_error_t;

/** @brief Object kind; Windows reparse points are always links when not followed. */
typedef enum { FS_FILE, FS_DIRECTORY, FS_LINK, FS_OTHER } fs_kind_t;

typedef struct {
    fs_kind_t kind;
    uint64_t size, device, identity;
} fs_info_t;

/** @brief A complete owned list, excluding dot and dot-dot. No partial list on failure. */
typedef struct {
    char **names;
    size_t count;
} fs_entries_t;

void clear_fs_error(fs_error_t *error);
/** @brief Validate a bounded UTF-8 path; rejects embedded NUL, empty and malformed input. */
bool validate_fs_path(const char *path, size_t length, fs_error_t *error);
char *join_fs_path(const char *directory, const char *name, fs_error_t *error);
char *absolute_fs_path(const char *path, fs_error_t *error);
char *temporary_fs_directory(fs_error_t *error);
/** @brief Returns an owned UTF-8 environment value; absence is FS_NOT_FOUND. */
char *fs_environment(const char *name, fs_error_t *error);
bool inspect_fs_path(const char *path, bool follow_links, fs_info_t *info, fs_error_t *error);
bool create_fs_directory(const char *path, fs_error_t *error);
/** @brief Atomically creates an exclusive random sibling of prefix. Free path with FREE. */
char *create_fs_temporary(const char *prefix, bool directory, FILE **file, fs_error_t *error);
bool list_fs_directory(const char *path, fs_entries_t *entries, fs_error_t *error);
void clear_fs_entries(fs_entries_t *entries);
/** @brief Removes one entry, never recursively or by following its final link. */
bool remove_fs_entry(const char *path, fs_error_t *error);
/** @brief Removes an owned tree using anchored handles; never descends into links/reparse points.
 */
bool remove_fs_tree(const char *path, fs_error_t *error);
/** @brief UTF-8 fopen; supports rb, wb, and exclusive wbx on both CRTs. */
FILE *FOPEN(const char *path, const char *mode);
/** @brief Checked stream writes and close; close consumes the stream even on failure. */
bool write_fs_file(FILE *file, const void *data, size_t size, const char *path, fs_error_t *error);
bool close_fs_file(FILE *file, const char *path, fs_error_t *error);
/** @brief Copies applicable access control from destination to an unpublished regular file.
 * Absent destination or a final link keeps normal creation/inheritance rights.
 * Publication replaces the link itself. Prepared must never be a link.
 * Caller exclusively owns prepared and serializes destination changes through publication.
 */
bool prepare_fs_replace(const char *prepared, const char *destination, fs_error_t *error);
/** @brief Atomic same-filesystem rename only. No delete/copy fallback. Call prepare first. */
bool publish_fs_replace(const char *prepared, const char *destination, fs_error_t *error);
/** @brief Strict Windows UTF-16 CLI adapter, owned UTF-8 argv. Free with free_fs_arguments. */
bool fs_arguments(int *argc, char ***argv, fs_error_t *error);
void free_fs_arguments(int argc, char **argv);
