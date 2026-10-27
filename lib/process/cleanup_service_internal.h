/** @file cleanup_service_internal.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Injectable deletion clock for deterministic retry-budget tests.
 */
#pragma once
#include "lib/process/cleanup_service.h"

typedef struct {
    uint64_t (*now)(void *context);
    void (*pause)(void *context, unsigned milliseconds);
    bool (*remove)(void *context, const char *path, fs_error_t *error);
    void *context;
} cleanup_retry_ops_t;

cleanup_result_t
cleanup_retry_directory(const char *path, const cleanup_retry_ops_t *ops, uint64_t started);
