/** @file string_builder.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Built-in StringBuilder prototype and per-receiver native storage.
 */
#pragma once
#include "lib/strings/string_ext.h"
#include "object.h"

/** @brief Immutable root prototype, registered beside String and Integer. */
object_t *get_string_builder_proto(void);
/** @brief Borrowed own storage only; never searches ancestors. */
string_builder_t *get_string_builder_storage(const object_t *obj);
/** @brief Allocate empty own storage once; NULL for non-user objects. */
string_builder_t *initialize_string_builder_storage(object_t *obj);
/** @brief Copy native contents for clone/flat, without invoking init. */
void copy_string_builder_storage(object_t *target, const object_t *source);
/** @brief Independent owned string snapshot, including embedded zeros. */
string_value_t string_builder_snapshot(const object_t *obj);
