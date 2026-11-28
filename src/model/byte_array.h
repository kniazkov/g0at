/** @file byte_array.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Builtin prototype and exclusively owned ByteArray storage.
 */
#pragma once
#include "lib/bytes/byte_buffer.h"
#include "object.h"
/** @brief Immutable root prototype. */
object_t *get_byte_array_proto(void);
/** @brief Borrow own storage; never search ancestors. */
byte_buffer_t *get_byte_array_storage(const object_t *obj);
/** @brief Internal allocation after validation; caller owns the process lock. */
byte_buffer_t *initialize_byte_array_storage(object_t *obj);
/** @brief Idempotent builtin construction with own readonly length. */
operation_result_t initialize_byte_array(object_t *receiver);
/** @brief Clone/flat payload copy without rerunning init. */
void copy_byte_array_storage(object_t *target, const object_t *source);
/** @brief Strict bytes for payload receivers; ordinary fallback for other objects. */
operation_result_t byte_array_get_index(process_t *process, object_t *obj, object_array_t indices);
/** @brief Strict indexed write; does not grow the buffer. */
operation_result_t
byte_array_set_index(process_t *process, object_t *obj, object_array_t indices, object_t *value);

/** @brief Whether indexed access uses ordinary property/access/module semantics. */
bool byte_array_property_index(const object_t *obj, bool store);

/** @brief Fresh literal instance, owned by caller, with copied bytes. */
operation_result_t create_byte_array_literal(thread_t *thread, const uint8_t *bytes, size_t size);

/** @brief Canonical uppercase hex snapshot with checked allocation size. */
string_value_t byte_array_snapshot(const object_t *obj);

/** @brief Checked allocation size for hex text including its terminator. */
bool byte_array_hex_size(size_t count, size_t *bytes);
