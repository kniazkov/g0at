/** @file wire.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Fixed-width little-endian wire access, including unaligned input.
 */
#pragma once
#include <stdint.h>

static inline uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static inline void write_le32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (uint8_t)(value >> (8 * i));
}
