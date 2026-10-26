/** @file native_environment.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Platform facts shared by producer inspection and native variant selection.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { GOAT_OS_UNKNOWN, GOAT_OS_LINUX, GOAT_OS_WINDOWS } goat_native_os;

typedef enum {
    GOAT_ARCH_UNKNOWN,
    GOAT_ARCH_X86,
    GOAT_ARCH_X86_64,
    GOAT_ARCH_AARCH64
} goat_native_arch;

typedef enum {
    GOAT_RUNTIME_UNKNOWN,
    GOAT_RUNTIME_GLIBC,
    GOAT_RUNTIME_MUSL,
    GOAT_RUNTIME_MSVCRT,
    GOAT_RUNTIME_UCRT
} goat_native_runtime;

typedef struct {
    uint32_t components[4];
    bool known;
} goat_native_version;

#define GOAT_CPU_I686 (UINT64_C(1) << 0)
#define GOAT_CPU_SSE2 (UINT64_C(1) << 1)
#define GOAT_CPU_SSE3 (UINT64_C(1) << 2)
#define GOAT_CPU_SSSE3 (UINT64_C(1) << 3)
#define GOAT_CPU_SSE4_1 (UINT64_C(1) << 4)
#define GOAT_CPU_SSE4_2 (UINT64_C(1) << 5)
#define GOAT_CPU_AVX (UINT64_C(1) << 6)
#define GOAT_CPU_AVX2 (UINT64_C(1) << 7)
#define GOAT_CPU_FMA (UINT64_C(1) << 8)
#define GOAT_CPU_BMI1 (UINT64_C(1) << 9)
#define GOAT_CPU_BMI2 (UINT64_C(1) << 10)
#define GOAT_CPU_AVX512F (UINT64_C(1) << 11)
#define GOAT_CPU_FP (UINT64_C(1) << 12)
#define GOAT_CPU_NEON (UINT64_C(1) << 13)
#define GOAT_CPU_AES (UINT64_C(1) << 14)
#define GOAT_CPU_SHA1 (UINT64_C(1) << 15)
#define GOAT_CPU_SHA2 (UINT64_C(1) << 16)
#define GOAT_CPU_CRC32 (UINT64_C(1) << 17)
#define GOAT_CPU_LSE (UINT64_C(1) << 18)
#define GOAT_CPU_SVE (UINT64_C(1) << 19)
#define GOAT_CPU_SVE2 (UINT64_C(1) << 20)

typedef struct {
    goat_native_os os;
    goat_native_arch architecture;
    unsigned bits;
    bool little_endian;
    goat_native_version os_version;
    goat_native_runtime runtime;
    goat_native_version runtime_version;
    uint64_t cpu_features;
} goat_native_environment;

/** Detects the running process, never MSYSTEM/PATH or a build compiler.
 * Unknown versions remain explicitly unknown. CPU flags are usable by this OS,
 * including AVX register-state support. Returns false for unsupported platforms.
 * This does not detect producer requirements or promise lower-OS compatibility.
 */
bool goat_native_environment_detect(goat_native_environment *out);
