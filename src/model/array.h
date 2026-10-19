/** @file array.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Internal array operation API; not part of the native ABI.
 * Callers validate receiver type, bounds and separator type. Operands are borrowed;
 * operation results own their reference. Storage details stay inside the model.
 */
#pragma once
#include "object.h"

/** @brief Inserts at a validated boundary; returns the new length. */
operation_result_t
insert_array_elements(object_t *obj, size_t index, object_t **args, size_t count);
/** @brief Removes a validated element, transferring its reference to the result. */
operation_result_t remove_array_element(object_t *obj, size_t index);
/** @brief Releases all elements and storage; returns the retained receiver. */
operation_result_t clear_array_elements(object_t *obj);
/** @brief Searches using virtual equality, returning an index or exception. */
operation_result_t find_array_element(object_t *obj, object_t *value, bool last);
/** @brief Stable sort by virtual comparison; leaves order unchanged on failure. */
operation_result_t sort_array_elements(object_t *obj);
/** @brief Reverses elements in place and retains the receiver. */
operation_result_t reverse_array_elements(object_t *obj);
/** @brief Copies a validated half-open range into independent array storage. */
operation_result_t slice_array(object_t *obj, size_t begin, size_t end);
/** @brief Formats notation, or joins values with a borrowed string/character separator. */
operation_result_t array_to_string(object_t *obj, object_t *separator_object);
