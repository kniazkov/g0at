/** @file module_library.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Owned OS image with checked module-v1 descriptor and adapter exports.
 */
#pragma once
#include "module_native.h"

typedef struct module_library_t module_library_t;

typedef enum {
    MODULE_LIBRARY_LOAD,
    MODULE_LIBRARY_DESCRIPTOR,
    MODULE_LIBRARY_SYMBOLS
} module_library_stage_t;

typedef void (*module_library_observer_t)(module_library_stage_t stage,
                                          bool completed,
                                          void *context);

typedef struct {
    module_library_t *library;
    bool valid;
    char *diagnostic;
} module_library_result_t;

/** Load trusted native code from an explicit path. Before returning success,
 * check readable immutable image ownership of the complete descriptor, compare
 * it with the validated archive, and resolve every declared executable adapter.
 * No legacy query is called. Diagnostics use FREE; release the successful image
 * only after all users/threads and managed objects have stopped using it.
 */
module_library_result_t
load_module_library(const char *path, const yyjson_doc *metadata, size_t variant_index);
void release_module_library(module_library_t *library);
/** Borrow a previously validated adapter and immutable-range checker. */
bool resolve_module_library_entry(module_library_t *library,
                                  const char *symbol,
                                  module_native_entry_t *out);

/** Observed loader retains even a rejected loaded image for explicit checked unload.
 * Caller must release result.library on every outcome; valid alone means ABI success.
 * Observer reports stage entry and successful completion before subsequent native code.
 */
module_library_result_t load_module_library_observed(const char *path,
                                                     const yyjson_doc *metadata,
                                                     size_t variant_index,
                                                     module_library_observer_t observer,
                                                     void *context);
bool release_module_library_checked(module_library_t *library, char **diagnostic);
