/** @file module_native.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Native platform metadata and selection before any OS library loading.
 */
#pragma once
#include "module_container.h"

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

typedef enum {
    GOAT_NATIVE_OK,
    GOAT_NATIVE_ARGUMENT,
    GOAT_NATIVE_INVALID_METADATA,
    GOAT_NATIVE_NO_MATCH,
    GOAT_NATIVE_AMBIGUOUS
} goat_native_status;

typedef struct {
    goat_native_status status;
    size_t variant_index; /* SIZE_MAX if not associated with a variant. */
} goat_native_error;

/** Detects the running process, never MSYSTEM/PATH or a build compiler.
 * Unknown versions remain explicitly unknown. CPU flags are usable by this OS,
 * including AVX register-state support. Returns false for unsupported platforms.
 * This does not detect producer requirements or promise lower-OS compatibility.
 */
bool goat_native_environment_detect(goat_native_environment *out);

/** Parse a strict target.schema.json profile. Version fields describe available
 * versions here. Unknown CPU names confer no capability; known names require
 * valid architectural placement. A profile can model missing capabilities;
 * selection requires the complete prerequisite set. Clears *out on failure.
 */
goat_module_json_error
goat_native_environment_read(const char *data, size_t size, goat_native_environment *out);

/** Select exactly one compatible variant in an already hash-verified container.
 * All target records are validated, including incompatible variants. Unknown
 * requirements/ABI versions exclude variants; two matches are an error, with no
 * preference ordering. ABI versions must name interfaces the caller implements.
 * No OS loading, code execution or mutation. The result is an index in the
 * normalized metadata.native array, not the ZIP central directory.
 * Failure/no match clears *selected_index to SIZE_MAX. An empty ABI list allows
 * no native variant. Does not implement native mode/VM fallback policy.
 */
goat_native_status goat_module_native_select(const goat_module_container *container,
                                             const goat_native_environment *environment,
                                             const uint32_t *supported_abis,
                                             size_t abi_count,
                                             size_t *selected_index,
                                             goat_native_error *error);
const char *goat_native_status_string(goat_native_status status);
