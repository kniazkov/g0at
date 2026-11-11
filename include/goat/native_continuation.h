/** @file native_continuation.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Optional native control-flow blocks over immutable original bytecode.
 * This is a separate, size-checked extension; the scalar ABI and wire format do not change.
 */
#ifndef GOAT_NATIVE_CONTINUATION_H
#define GOAT_NATIVE_CONTINUATION_H
#include <stdint.h>

/* Keep this extension usable by module-v1 hosts without importing scalar tags. */
#ifndef GOAT_NATIVE_CALL
#    ifdef _WIN32
#        define GOAT_NATIVE_CALL __cdecl
#    else
#        define GOAT_NATIVE_CALL
#    endif
#endif
#ifndef GOAT_NATIVE_API
#    if defined(_WIN32) && defined(GOAT_NATIVE_BUILD)
#        define GOAT_NATIVE_API __declspec(dllexport)
#    else
#        define GOAT_NATIVE_API
#    endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum {
    GOAT_NATIVE_CONTINUATION_VERSION = 1,
    GOAT_NATIVE_CONTINUE = 0,
    GOAT_NATIVE_COMPLETE = 1,
    GOAT_NATIVE_EXCEPTION = 2,
    GOAT_NATIVE_FAILED = 3
};

/** Host owns operands, frames and results. A step executes one original instruction,
 * including completion of any nested VM handler. CONTINUE alone supplies next_pc.
 * All other statuses must be propagated immediately, without replay or fallback.
 */
typedef struct goat_native_continuation_context_t {
    void *state;
    uint32_t(GOAT_NATIVE_CALL *step)(void *state, uint32_t pc, uint32_t *next_pc);
} goat_native_continuation_context_t;

typedef uint32_t(GOAT_NATIVE_CALL *goat_native_continuation_invoke_t)(
    const goat_native_continuation_context_t *context,
    uint32_t initial_pc);

typedef struct goat_native_continuation_entry_t {
    uint64_t function_id;
    uint32_t function_instruction; /**< Original FUNC instruction. */
    uint32_t first_instruction;
    uint32_t instruction_count; /**< Half-open body range, excludes nested deferred bodies. */
    goat_native_continuation_invoke_t invoke;
} goat_native_continuation_entry_t;

typedef struct goat_native_continuation_module_t {
    uint32_t version, struct_size, entry_size, entry_count;
    const goat_native_continuation_entry_t *entries;
} goat_native_continuation_module_t;

typedef const goat_native_continuation_module_t *(
    GOAT_NATIVE_CALL *goat_native_continuation_query_t)(uint32_t version);
GOAT_NATIVE_API const goat_native_continuation_module_t *GOAT_NATIVE_CALL
goat_native_continuation_query(uint32_t version);
#ifdef __cplusplus
}
#endif
#endif
