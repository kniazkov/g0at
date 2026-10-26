/** @file native_requirements.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Inspect native images and dependency requirements without loading code.
 */
#pragma once
#include "module/native_environment.h"

/** @brief Resolve an ELF dependency in the selected toolchain, returning an owned path.
 * Return NULL when the dependency cannot be established. The caller frees the path.
 */
typedef char *(*native_dependency_resolver_t)(const char *name, void *context);

/** @brief Inspect an image and its ELF dependency closure; never execute or load it.
 * expected supplies the proven compiler target and effective CPU profile. Unknown
 * formats, dynamic dependencies or requirement encodings fail closed. The result
 * uses version requirements and ABI notes from files, never build-host versions.
 * For musl, the proven linked libc release is a conservative floor (there are no
 * symbol versions); its bounded read-only loader layout must be recognized. The
 * explicit musl target kernel profile is Linux 2.6.39 (x86-64), 3.7 (AArch64).
 * diagnostic is owned and must be freed. PE supports the Windows 10 system CRT
 * profile only; arbitrary DLL dependencies and delay imports are rejected.
 */
bool inspect_native_requirements(const char *path,
                                 const goat_native_environment *expected,
                                 native_dependency_resolver_t resolver,
                                 void *context,
                                 goat_native_environment *out,
                                 char **diagnostic);
