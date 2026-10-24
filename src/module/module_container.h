#pragma once

#include "lib/zip_archive.h"
#include "module_json.h"

typedef enum {
    GOAT_CONTAINER_OK,
    GOAT_CONTAINER_INVALID,
    GOAT_CONTAINER_MEMORY,
    GOAT_CONTAINER_LIMIT,
    GOAT_CONTAINER_ZIP,
    GOAT_CONTAINER_LAYOUT,
    GOAT_CONTAINER_PATH,
    GOAT_CONTAINER_DUPLICATE,
    GOAT_CONTAINER_FILE_TYPE,
    GOAT_CONTAINER_REQUIRED,
    GOAT_CONTAINER_UNDECLARED,
    GOAT_CONTAINER_JSON,
    GOAT_CONTAINER_MAIN_HASH,
    GOAT_CONTAINER_NATIVE_HASH
} goat_container_status;

typedef struct {
    goat_container_status status;
    size_t entry_index; /* Original central-directory index, SIZE_MAX if global. */
    zip_status_t zip_error;
    goat_module_json_error json_error;
} goat_container_error;

typedef struct goat_module_container goat_module_container;

typedef struct {
    const char *name;
    size_t name_length;
    zip_view_t bytes;
} goat_container_native;

/** Validate the entire .gbin before exposing any contents. Input is borrowed and
 * immutable only during this call; successful result owns the expanded snapshot.
 * max_expanded limits the sum of declared uncompressed sizes, before allocation.
 * Use SIZE_MAX for the address-space limit. Failure clears *out; error is optional.
 * No filesystem operations, OS loading, bytecode semantics or graph validation.
 * MEMORY/LIMIT must be propagated as load failures, never as module-not-found.
 */
goat_container_status goat_module_container_read(const void *data,
                                                 size_t size,
                                                 size_t max_expanded,
                                                 goat_module_container **out,
                                                 goat_container_error *error);

/** Reporting stages are ordered and independent of any native execution mode. */
typedef enum {
    GOAT_CONTAINER_CHECK_LAYOUT,
    GOAT_CONTAINER_CHECK_CONTENT,
    GOAT_CONTAINER_CHECK_JSON,
    GOAT_CONTAINER_CHECK_DECLARATIONS,
    GOAT_CONTAINER_CHECK_NATIVE_HASH,
    GOAT_CONTAINER_CHECK_MAIN_HASH
} goat_container_check;

typedef enum {
    GOAT_CONTAINER_CHECK_PASSED,
    GOAT_CONTAINER_CHECK_FAILED,
    GOAT_CONTAINER_CHECK_SKIPPED
} goat_container_check_state;

typedef struct {
    goat_container_check check;
    goat_container_check_state state;
    goat_container_check prerequisite; /* Meaningful only for SKIPPED. */
    goat_container_error error;
    zip_view_t entry;  /* Bounded borrowed name, valid only during callback. */
    const char *field; /* JSON path, valid only during callback; otherwise NULL. */
    size_t field_length;
} goat_container_diagnostic;

typedef void (*goat_container_report_fn)(const goat_container_diagnostic *diagnostic,
                                         void *context);
/** Borrowed partial snapshot for diagnostics ONLY: metadata has passed schema
 * validation, but hashes/declarations/individual payload CRCs MAY HAVE FAILED.
 * Bytecode data is NULL when its CRC/read failed. Native entries may be absent;
 * any non-NULL payload data has passed CRC, not necessarily SHA-256. Inspection
 * may validate available bytes or metadata independently, never execute, OS-load,
 * or retain the snapshot beyond this callback. Only read's successful output is
 * a fully verified container usable by consumers.
 */
typedef void (*goat_container_inspect_fn)(const goat_module_container *container, void *context);
/** Same validation engine and ownership as read. Collects independent failures;
 * unavailable dependent checks are explicitly skipped, never marked passed.
 * Callback data is borrowed only during the callback. Output is NULL on any error.
 */
goat_container_status goat_module_container_read_report(const void *data,
                                                        size_t size,
                                                        size_t max_expanded,
                                                        goat_module_container **out,
                                                        goat_container_error *error,
                                                        goat_container_report_fn report,
                                                        void *context,
                                                        goat_container_inspect_fn inspect);
void goat_module_container_free(goat_module_container *container);
const char *goat_container_status_string(goat_container_status status);
/** Borrowed immutable views, valid until container_free. NULL handles yield empty views. */
const yyjson_doc *goat_module_container_metadata(const goat_module_container *container);
zip_view_t goat_module_container_bytecode(const goat_module_container *container);
size_t goat_module_container_native_count(const goat_module_container *container);
bool goat_module_container_native_at(const goat_module_container *container,
                                     size_t index,
                                     goat_container_native *out);
