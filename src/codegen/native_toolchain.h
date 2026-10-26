/** @file native_toolchain.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Deterministic, bounded compiler discovery without loading generated libraries.
 */
#pragma once
#include "lib/cleanup_service.h"
#include "module/native_environment.h"

#define NATIVE_PROBE_DEFAULT_SECONDS 10

typedef struct native_toolchain_context_t native_toolchain_context_t;

typedef struct {
    char *compiler;
    char *name;
    char *version;
    char *target;
    char *runtime_library; /**< Actual musl libc selected by the successful link probe. */
    const char *const *flags;
    goat_native_environment requirements;
    uint64_t probe_timeout_seconds;
    bool available;
    bool timed_out;
    char *diagnostic;
} native_toolchain_t;

typedef struct {
    const native_toolchain_t **items;
    size_t count;
    const native_toolchain_t *selected;
    char *diagnostic;
} native_toolchain_search_t;

/** @brief Borrows one owner's cleaner. Context caches both successful and failed probes.
 * Environment must remain unchanged while this context is used. Not concurrently callable.
 */
native_toolchain_context_t *create_native_toolchain_context(cleanup_service_t *cleanup);
void destroy_native_toolchain_context(native_toolchain_context_t *context);
/** @brief Search PATH only, or only explicit compiler. Stable order; ordinary search stops
 * at first success. Enumerate probes all supported candidate targets when target is NULL.
 * timeout_seconds must be positive and representable in milliseconds.
 * Returned candidate pointers remain valid until context destruction; release search arrays.
 */
native_toolchain_search_t find_native_toolchains(native_toolchain_context_t *context,
                                                 const char *target,
                                                 const char *compiler,
                                                 uint64_t timeout_seconds,
                                                 bool enumerate);
void destroy_native_toolchain_search(native_toolchain_search_t *search);
/** @brief Resolves host or a concrete supported v1 target. Never substitutes a different target. */
bool native_toolchain_target(const char *target, goat_native_environment *environment);
/** @brief Validate the linked result and its dependencies against the selected target.
 * No OS loading. Requirements include actual dependency versions, not build-host versions.
 */
bool native_toolchain_inspect(native_toolchain_context_t *context,
                              const native_toolchain_t *toolchain,
                              const char *library,
                              goat_native_environment *requirements,
                              char **diagnostic);
