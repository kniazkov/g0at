/** @file exception.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Exception payload; reserved for future debugging metadata.
 */
#pragma once

typedef struct object_t object_t;

/** @brief Owns one reference to a thrown value; C NULL means no pending exception. */
typedef struct {
    object_t *value; /**< Any Goat value, including the null singleton. */
} exception_t;
