/** @file module_files.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Process-owned source-file identities, scopes and one shared bytecode image.
 */
#pragma once

#include "context.h"
#include "exception.h"
#include "lib/string_ext.h"
#include "vm/module_bytecode.h"

typedef struct module_files_t module_files_t;
typedef struct module_file_t module_file_t;

/** Validate metadata/file links and the entire image before creating any scopes.
 * This internal model API does not replace archive/hash/native validation (#185).
 * On success the process owns the set until destroy_process; inputs may be freed.
 * One image is stored per set, never one image per include/file. Separate calls
 * create distinct module instances, even for identical names/bytes. Loader cache
 * policy belongs to the loader. Failure clears out and leaves the process unchanged.
 * Core allocation failure follows the fatal allocator policy.
 */
module_bytecode_status_t create_module_files(process_t *process,
                                             const char *metadata,
                                             size_t metadata_size,
                                             const void *image,
                                             size_t image_size,
                                             module_files_t **out,
                                             module_bytecode_error_t *error);
/** Trusted snapshot path: metadata and code must be a fully validated, matching
 * module pair. Does not reparse, revalidate or copy the immutable bytecode image.
 * The process owns fresh scopes and a runtime wrapper with independent bindings.
 * The caller must retain the source image until AFTER destroy_process, including
 * all image-backed objects and wrappers. Metadata is borrowed only for this call.
 * Invalid NULL arguments or unrepresentable file storage clear out and return false.
 * This API neither executes files nor grants partial graphs permission to run.
 */
bool create_module_files_from_snapshot(process_t *process,
                                       const yyjson_doc *metadata,
                                       const bytecode_t *code,
                                       module_files_t **out);
/** Borrowed process-lifetime views. NULL sets have zero files/no members. */
size_t module_files_count(const module_files_t *files);
module_file_t *module_files_get(module_files_t *files, uint32_t id);
/** @brief Borrowed file set for an exact process-owned module identity; no I/O. */
module_files_t *find_module_files(process_t *process, const module_t *module);
module_file_t *module_files_main(module_files_t *files);
/** The wire buffer is immutable; the wrapper's VM caches remain outside it. */
bytecode_t *module_files_bytecode(module_files_t *files);
module_t *module_files_identity(module_files_t *files);
/** @brief Immutable logical metadata, borrowed for the process lifetime. */
string_view_t module_files_name(const module_files_t *files);
string_view_t module_files_version(const module_files_t *files);
/** Trusted graph loader only: bind a validated import descriptor to a direct
 * dependency's main file. Both sets must belong to the same process. Bindings
 * and string caches are memory-only and never alter the bytecode wire buffer.
 * Repeating the same binding is harmless; replacing a target is rejected.
 */
bool bind_module_files_import(module_files_t *consumer,
                              uint32_t descriptor,
                              module_files_t *target);
/** Borrowed prebound main file; unbound/out-of-range descriptors return NULL. */
module_file_t *module_files_import(module_files_t *consumer, uint32_t descriptor);
/** Borrowed process-lifetime string cache entry for a validated string descriptor.
 * Cached objects are GC roots and never shared by descriptor number across images.
 */
object_t *module_files_string(module_files_t *files, uint32_t descriptor);
/** Non-NULL file required for these identity/diagnostic accessors. */
uint32_t module_file_id(const module_file_t *file);
instr_index_t module_file_entry(const module_file_t *file);
object_t *module_file_scope(const module_file_t *file);
string_view_t module_file_name(const module_file_t *file);
bool module_file_external(const module_file_t *file);
/** Fresh execution frame using the SAME persistent file scope. The caller is
 * only the control-flow predecessor: its locals, receiver and access grants are
 * not inherited. Builtins remain available. Does not execute or initialize the
 * file; the initialization state machine decides whether entry may run (#196).
 * NULL arguments or a caller from another process return NULL.
 */
context_t *create_module_file_context(module_file_t *file, context_t *caller);
/** Internal process lifecycle: mark scopes before GC; free after all objects. */
void mark_module_files(const process_t *process);
void destroy_module_files(process_t *process);

/** @brief Runtime state, separate from immutable bytecode and shared by all includes. */
typedef enum {
    MODULE_FILE_UNINITIALIZED,
    MODULE_FILE_INITIALIZING,
    MODULE_FILE_INITIALIZED,
    MODULE_FILE_FAILED
} module_file_state_t;

/** @brief NULL returns UNINITIALIZED; use identity accessors to validate a file. */
module_file_state_t module_file_state(const module_file_t *file);
/** @brief Borrowed owning image and process. */
bytecode_t *module_file_bytecode(const module_file_t *file);
process_t *module_file_process(const module_file_t *file);
/** @brief Borrowed original exception, or NULL for a file that has not failed. */
object_t *module_file_failure(const module_file_t *file);
/** @brief Borrowed full saved record; retain_exception before storing or propagating. */
exception_t module_file_exception(const module_file_t *file);
exception_t module_files_exception(process_t *process, module_t *module, uint32_t file_id);
/** @brief Borrowed failure of a declaring file; unknown identities return NULL. */
object_t *module_files_failure(process_t *process, module_t *module, uint32_t file_id);
/** @brief Borrowed file only for its exact persistent scope; otherwise NULL. */
module_file_t *module_scope_file(const object_t *scope);
/** @brief Failure only for an exact persistent scope, not ordinary escaped values. */
object_t *module_scope_failure(const object_t *scope);
/** @brief Executor returns true only after normal completion of the file top level.
 * The frame uses the persistent scope, no caller locals or receiver. The executor
 * must restore any descendant frames before returning, and must not free frame.
 * Blocking I/O is allowed: the gate imposes no deadline or execution budget.
 * It waits for the executor to return normally or with an escaping exception.
 * On failure, transfer one owned reference to the final thrown value into failure.
 * C NULL is not a thrown value; the Goat null singleton is valid. A false result
 * without a payload is converted to INVALID_OPERATION. A payload also overrides
 * a true result, so an escaping exception cannot be silently recorded as success.
 */
typedef bool (*module_file_executor_t)(module_file_t *file,
                                       context_t *frame,
                                       void *data,
                                       exception_t *failure);

typedef enum {
    MODULE_INITIALIZE_OK,
    MODULE_INITIALIZE_ARGUMENT,
    MODULE_INITIALIZE_FAILED
} module_initialize_status_t;

/** @brief Synchronous, serialized initialization gate for include/import consumers.
 * First entry reserves declarations and calls execute exactly once. Reentrant
 * cycles and completed files return the same borrowed scope without executing.
 * An escaping failure is terminal, never retried. Invalid arguments clear scope
 * and do not change state. Both output pointers are required and cleared on entry;
 * failure receives owned references to the original saved value AND origin trace,
 * never a clone or a new trace from the repeated caller. The caller must
 * release or propagate it before reusing the output. No filesystem access or image
 * mutation occurs. The executor is never invoked again after failure.
 * There is no initialization timeout, polling watchdog or elapsed-time failure:
 * INITIALIZING lasts until the synchronous executor returns. External process
 * termination is outside this contract and does not produce a cached timeout.
 * The executor runs under the recursive process execution lock. It may suspend
 * that lock around blocking I/O; the separate global initializer lock remains held.
 * Other OS threads wait without retaining the execution lock. Ready files bypass
 * the initializer lock. Ordinary model operations require the execution lock.
 */
module_initialize_status_t initialize_module_file(module_file_t *file,
                                                  context_t *caller,
                                                  module_file_executor_t execute,
                                                  void *data,
                                                  object_t **scope,
                                                  exception_t *failure);
/** @brief Format current root-to-leaf initialization chain, optionally ending at
 * target (e.g. the declaration read too early). Names are escaped, indices and
 * external markers disambiguate source files. Caller frees with FREE_STRING.
 * Capture while the executor is active, before unwinding its frame.
 */
string_value_t module_initialization_chain(process_t *process, const module_file_t *target);

/** @brief Direct program entry excludes the initializer lock and publishes declarations
 * individually. Only the launch path may use this API, never include/import. */
module_initialize_status_t initialize_module_file_direct(module_file_t *file,
                                                         context_t *caller,
                                                         module_file_executor_t execute,
                                                         void *data,
                                                         object_t **scope,
                                                         exception_t *failure);
/** @brief Whether this OS execution thread has an active file diagnostic chain. */
bool module_initialization_active(process_t *process);
