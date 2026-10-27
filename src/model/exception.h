/** @file exception.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Owned exception values and immutable origin traces.
 */
#pragma once

#include "common/types.h"
#include "lib/collections/value.h"

#include <stdint.h>

typedef struct object_t object_t;
typedef struct module_t module_t;
typedef struct context_t context_t;
typedef struct process_t process_t;
typedef struct exception_trace_t exception_trace_t;

/** @brief One source frame, ordered from throwing context towards its callers. */
typedef struct {
    module_t *module; /**< Retained identity, not a serialized pointer. */
    uint32_t file_id;
    instr_index_t instruction;
    bool native_entry; /**< Native status identifies the callee entry, not an exact failing
                          operation. */
} exception_trace_frame_t;

/** @brief Owns one value reference and one optional immutable trace reference. */
typedef struct {
    object_t *value; /**< Any Goat value; C NULL alone means no exception. */
    const exception_trace_t *trace;
} exception_t;

/** @brief Retains both members; the caller owns the returned record. */
exception_t retain_exception(exception_t exception);
/** @brief Releases both members and resets the record to empty. */
void clear_exception(exception_t *exception);
/** @brief Captures a new immutable context trace. Caller owns the reference. */
const exception_trace_t *capture_exception_trace(context_t *context, instr_index_t instruction);
/** @brief Capture native callee entry and caller chain without allocating a VM call context. */
const exception_trace_t *capture_native_exception_trace(context_t *caller,
                                                        module_t *module,
                                                        uint32_t file,
                                                        instr_index_t body);
const exception_trace_t *retain_exception_trace(const exception_trace_t *trace);
void release_exception_trace(const exception_trace_t *trace);
size_t exception_trace_size(const exception_trace_t *trace);
/** @brief Borrowed frame, or NULL for an invalid index. */
const exception_trace_frame_t *exception_trace_frame(const exception_trace_t *trace, size_t index);

/** @brief Formats immutable origins using the process-owned validated module mapping.
 * Never opens source files. The process must still own the referenced modules.
 * Unknown identities remain explicit numeric locations, not guessed filenames.
 * Caller releases the returned string with FREE_STRING().
 */
string_value_t format_exception_trace(process_t *process, const exception_trace_t *trace);
