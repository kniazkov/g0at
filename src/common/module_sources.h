/** @file module_sources.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Owned, syntax-only source graph for a module build or direct source input.
 */
#pragma once
#include "lib/file_system.h"
#include "module/module_json.h"
#include "parser/source_includes.h"

#include <stdbool.h>
#include <stddef.h>

/** Paths are build-only canonical identities. name/external are portable metadata.
 * source is an owned immutable UTF-8 snapshot, length excludes trailing NUL.
 * Include targets are file indices in source order, not execution order.
 */
typedef struct {
    char *path, *name;
    bool external;
    char *source;
    size_t length;
    source_includes_t includes;
    size_t *targets;
} module_source_t;

typedef struct {
    char *descriptor_path, *base_directory;
    yyjson_doc *descriptor; /**< Normalized complete .gmod; NULL for direct source. */
    module_source_t *files;
    size_t count;
} module_sources_t;

typedef enum {
    MODULE_SOURCES_OK,
    MODULE_SOURCES_ARGUMENT,
    MODULE_SOURCES_IO,
    MODULE_SOURCES_JSON,
    MODULE_SOURCES_SYNTAX,
    MODULE_SOURCES_LIMIT
} module_sources_status_t;

/** Initialize with zero and clear after use. Including site is retained for I/O errors. */
typedef struct {
    module_sources_status_t status;
    char *path;
    size_t row, column, offset;
    const char *message;
    goat_module_json_error json;
    fs_error_t filesystem;
} module_sources_error_t;

/** All limits must be positive. NULL options select bounded defaults. */
typedef struct {
    size_t max_files, max_file_bytes, max_total_bytes;
} module_sources_options_t;

/** Complete descriptor validation precedes source discovery. No execution, dependency
 * search, native compiler lookup or publication. Explicit-stack depth-first numbering,
 * main = 0. Symlinks deduplicate; distinct hardlink paths do not. Failure clears out.
 * Consumers use snapshots; later .gbin execution must not reopen these build paths.
 */
module_sources_status_t module_sources_read(const char *input,
                                            bool descriptor,
                                            const module_sources_options_t *options,
                                            module_sources_t **out,
                                            module_sources_error_t *error);
void free_module_sources(module_sources_t *sources);
void clear_module_sources_error(module_sources_error_t *error);
