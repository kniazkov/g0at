/** @file module_native.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Per-call dispatch of validated module-v1 native specializations.
 */
#pragma once
#include "module/module_abi.h"
#include "module_files.h"
#include "thread.h"

/** A resolved adapter and readable immutable library ranges. All remain valid until
 * the binding's cleanup callback runs after the final program thread and objects.
 * readable must not call Goat code; it covers only trusted library-owned ranges.
 */
typedef struct module_native_entry_t {
    goat_module_adapter_v1 adapter;
    goat_module_readable readable;
    void *readable_state;
} module_native_entry_t;

/** Called only for a unique applicable specialization, never while testing guards.
 * The host resolver validates actual library ABI/symbols (#210). It may cache the
 * loaded library. False means a load/ABI lookup failure before adapter entry.
 * The execution lock is held; a blocking loader may suspend/resume it itself.
 */
typedef bool (*module_native_resolver_t)(void *state,
                                         uint32_t specialization_id,
                                         const char *symbol,
                                         module_native_entry_t *out);
/** Bind one already schema-validated, platform-selected variant before execution.
 * Copies metadata; validates guards, function identity and formal arity. Returns
 * false without taking state ownership on invalid/repeated/late binding. Success
 * takes state ownership, released exactly once by cleanup (if non-NULL).
 * No library is opened. required affects selected load failures, not absence of a
 * matching call. Main-program required preflight belongs to loader #210.
 */
bool bind_module_files_native(module_files_t *files,
                              const yyjson_val *variant,
                              bool required,
                              module_native_resolver_t resolver,
                              void *state,
                              void (*cleanup)(void *));

typedef struct module_native_binding_t module_native_binding_t;
/** Internal lifetime/access hooks; ordinary clients use bind_module_files_native. */
module_native_binding_t *module_files_native(const module_files_t *files);
bool set_module_files_native(module_files_t *files, module_native_binding_t *binding);
void destroy_module_native_binding(module_native_binding_t *binding);

/** Internal dispatch result. FALLBACK never consumes/re-evaluates arguments. */
typedef enum {
    MODULE_NATIVE_FALLBACK,
    MODULE_NATIVE_COMPLETE,
    MODULE_NATIVE_ERROR
} module_native_call_status_t;

/** Called only after normal VM access/file checks; module/body/file identify code.
 * Successful calls consume actual arguments and push a copied result. Errors are
 * terminal, with native_status, module/function diagnostic and origin trace set.
 */
module_native_call_status_t call_module_native(module_files_t *files,
                                               uint32_t file_id,
                                               uint32_t function_id,
                                               instr_index_t body,
                                               size_t formal_count,
                                               uint16_t actual_count,
                                               thread_t *thread);
