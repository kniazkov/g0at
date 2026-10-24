/** @file module_files.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Process-owned source-file identities, scopes and one shared bytecode image.
 */
#pragma once

#include "context.h"
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
/** Borrowed process-lifetime views. NULL sets have zero files/no members. */
size_t module_files_count(const module_files_t *files);
module_file_t *module_files_get(module_files_t *files, uint32_t id);
module_file_t *module_files_main(module_files_t *files);
/** The wire buffer is immutable; the wrapper's VM caches remain outside it. */
bytecode_t *module_files_bytecode(module_files_t *files);
module_t *module_files_identity(module_files_t *files);
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
