/** @file gc_scalar_native_fixture.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Private callback of the scalar GC fixture; not a runtime service.
 */
#pragma once
#include <goat/native_abi.h>

typedef void(GOAT_NATIVE_CALL *gc_scalar_probe_t)(void);
GOAT_NATIVE_API void GOAT_NATIVE_CALL gc_scalar_configure(unsigned mode, gc_scalar_probe_t probe);
