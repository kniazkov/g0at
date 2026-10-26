/** @file native_artifact.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Static exported-symbol inspection, without executing an image.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef struct native_artifact_t native_artifact_t;

/** @brief Read a bounded ELF/PE snapshot. Owned diagnostics are freed with FREE. */
native_artifact_t *open_native_artifact(const char *path, char **diagnostic);
/** @brief Copy and inspect the exact owned build snapshot, without reopening its file. */
native_artifact_t *open_native_artifact_bytes(const void *bytes, size_t size, char **diagnostic);
void close_native_artifact(native_artifact_t *artifact);
/** @brief Borrow the exact file-backed descriptor envelope until close.
 * This checks envelope bounds/magic/terminator, not JSON semantics; the caller
 * passes the returned snapshot to goat_module_descriptor_read and compares it.
 */
bool native_artifact_descriptor(const native_artifact_t *artifact,
                                const void **bytes,
                                size_t *size,
                                char **diagnostic);
/** @brief A defined, externally exported, executable-backed function, not a forwarder. */
bool native_artifact_has_function(const native_artifact_t *artifact, const char *symbol);
