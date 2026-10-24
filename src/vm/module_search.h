/** @file module_search.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Fixed program-wide search paths and first-candidate module validation.
 */
#pragma once
#include "lib/file_system.h"
#include "module_validation.h"

typedef enum {
    MODULE_SEARCH_OK,
    MODULE_SEARCH_ARGUMENT,
    MODULE_SEARCH_IO,
    MODULE_SEARCH_NOT_FOUND,
    MODULE_SEARCH_INVALID_MODULE,
    MODULE_SEARCH_IDENTITY,
    MODULE_SEARCH_HASH
} module_search_status_t;

/** Initialize with zero; clear after use. attempted contains only actually probed
 * candidates, in order. filesystem and validation retain the original cause.
 */
typedef struct {
    module_search_status_t status;
    fs_error_t filesystem;
    module_validation_result_t validation;
    char **attempted;
    size_t count;
} module_search_error_t;
typedef struct module_search module_search_t;

/** Canonicalize the existing root .gmod/.goat/.gbin and capture cwd once. Search:
 * real root's modules, explicit module_dir (optional), environment path list.
 * environment is an explicit snapshot; NULL means absent, not getenv. Separator
 * is native (';' Windows, ':' POSIX), empty elements ignored. Paths are literal.
 * All callers in a graph must reuse this object; libraries do not add paths.
 * Existing directory aliases deduplicate after symlink resolution. Missing
 * directories are retained and need not exist until a lookup is requested.
 */
module_search_status_t prepare_module_search(const char *root,
                                             const char *module_dir,
                                             const char *environment,
                                             module_search_t **out,
                                             module_search_error_t *error);
/** Capture GOAT_MODULE_PATH through the UTF-8 OS adapter at program startup.
 * Unset and empty values are valid. Subsequent environment/cwd changes have no effect.
 */
module_search_status_t prepare_module_search_from_environment(const char *root,
                                                              const char *module_dir,
                                                              module_search_t **out,
                                                              module_search_error_t *error);
/** Owned English diagnostic (FREE), including consumer and only checked paths.
 * Structured filesystem/validation details remain available for richer frontends.
 * NULL error/name/version/consumer arguments are permitted.
 */
char *format_module_search_error(const module_search_error_t *error,
                                 const char *name,
                                 const char *version,
                                 const char *consumer);
void free_module_search(module_search_t *search);
void clear_module_search_error(module_search_error_t *error);
const char *module_search_root(const module_search_t *search);
size_t module_search_directory_count(const module_search_t *search);
const char *module_search_directory(const module_search_t *search, size_t index);

/** Find exactly name/version.gbin, with no recursive enumeration. Only a missing
 * candidate continues. Any existing candidate's read, validation, identity or
 * expected lowercase SHA-256 mismatch stops search. expected_hash may be NULL.
 * Read once through checked regular-file I/O; success returns an owned validated
 * snapshot. Caller owns snapshot and search independently. No execution/loading.
 */
module_search_status_t find_module(const module_search_t *search,
                                   const char *name,
                                   const char *version,
                                   const char *expected_hash,
                                   const module_validation_options_t *options,
                                   validated_module_t **out,
                                   module_search_error_t *error);
