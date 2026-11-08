/** @file operator.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Operator property selection and resumable function invocation, without opcode routing.
 */
#pragma once
#include "access.h"

typedef enum {
    OPERATOR_NOT_APPLICABLE,
    OPERATOR_ABSENT,
    OPERATOR_FOUND,
    OPERATOR_ERROR
} operator_lookup_status_t;

/** @brief Owns the selected function or the failure, never both. */
typedef struct {
    operator_lookup_status_t status;
    object_t *function;
    exception_t exception;
} operator_lookup_t;

typedef enum {
    OPERATOR_RESULT_VALUE,
    OPERATOR_RESULT_BOOL,
    OPERATOR_RESULT_NEGATED_BOOL,
    OPERATOR_RESULT_DISCARD
} operator_result_policy_t;

/** @brief Ordinary property lookup on user objects; absence does not include null or denial. */
operator_lookup_t
lookup_operator_handler(const function_access_t *access, object_t *receiver, object_t *key);
/** @brief Releases owned members; safe to repeat. */
void clear_operator_lookup(operator_lookup_t *lookup);
/** @brief Borrows function and receiver; arguments already own their stack references.
 * True means synchronous completion or a scheduled Goat frame. False leaves the original
 * pending exception or terminal backend failure. Only successful completion resumes at resume.
 */
bool call_operator_handler(object_t *function,
                           object_t *receiver,
                           uint16_t count,
                           operator_result_policy_t policy,
                           instr_index_t resume,
                           thread_t *thread);

/** @brief Internal function entry preserving ordinary CALL/CALLM receiver semantics. */
bool call_function_with_operator_receiver(object_t *function,
                                          uint16_t count,
                                          object_t *receiver,
                                          thread_t *thread);
/** @brief Internal shared completion: consumes/transforms the stack result or leaves a traced
 * pending contract exception at origin. Called after the callee and its cleanups have exited. */
bool complete_operator_result(thread_t *thread,
                              operator_result_policy_t policy,
                              instr_index_t origin,
                              instr_index_t resume);
