/** @file native_continuation_descriptor.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Continuation descriptor ownership without importing the scalar ABI.
 */
#pragma once
#include <goat/native_continuation.h>

typedef struct native_function_descriptor_t native_function_descriptor_t;
/** Success consumes one owner reference; failure leaves it with the caller. */
native_function_descriptor_t *
create_native_continuation_descriptor(const goat_native_continuation_entry_t *entry,
                                      void *owner,
                                      void (*release_owner)(void *));
void release_native_function_descriptor(native_function_descriptor_t *function);
