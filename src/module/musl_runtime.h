/** @file musl_runtime.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Fail-closed adapter for the read-only musl dynamic-loader image layout.
 */
#pragma once
#include "native_environment.h"

#include <stddef.h>
#include <string.h>

/** @brief Accumulated evidence from read-only loadable segments of one image. */
typedef struct {
    bool banner;
    unsigned versions;
    goat_native_version version;
} musl_image_evidence_t;

/** @brief Inspect bounded bytes, never execute a loader or infer libc from its filename.
 * Supported musl release images contain their architecture-specific loader banner
 * and one NUL-delimited x.y.z release version in read-only segments. Unknown,
 * development-suffixed, or ambiguous layouts are intentionally unsupported.
 */
static inline void musl_image_scan(musl_image_evidence_t *out,
                                   const unsigned char *bytes,
                                   size_t size,
                                   goat_native_arch architecture) {
    const char *banner = architecture == GOAT_ARCH_X86_64
                             ? "musl libc (x86_64)\nVersion %s\nDynamic Program Loader\n"
                         : architecture == GOAT_ARCH_AARCH64
                             ? "musl libc (aarch64)\nVersion %s\nDynamic Program Loader\n"
                             : NULL;
    if (!banner)
        return;
    size_t length = strlen(banner);
    for (size_t i = 0; i < size; i++) {
        if (length <= size - i && !memcmp(bytes + i, banner, length))
            out->banner = true;
        if ((i && bytes[i - 1]) || bytes[i] < '0' || bytes[i] > '9')
            continue;
        size_t p = i;
        goat_native_version candidate = {.known = true};
        bool valid = true;
        for (size_t part = 0; part < 3 && valid; part++) {
            size_t start = p;
            uint32_t value = 0;
            while (p < size && bytes[p] >= '0' && bytes[p] <= '9') {
                unsigned digit = bytes[p++] - '0';
                if (value > (UINT32_MAX - digit) / 10) {
                    valid = false;
                    break;
                }
                value = value * 10 + digit;
            }
            candidate.components[part] = value;
            if (p == start || p == size || bytes[p] != (part == 2 ? 0 : '.'))
                valid = false;
            p++;
        }
        if (valid) {
            if (out->versions < 2)
                out->versions++;
            out->version = candidate;
        }
    }
}

/** @brief Only a unique released musl 1.x image is a supported runtime. */
static inline bool musl_image_version(const musl_image_evidence_t *evidence,
                                      goat_native_version *out) {
    if (!evidence->banner || evidence->versions != 1 || evidence->version.components[0] != 1)
        return false;
    *out = evidence->version;
    return true;
}
