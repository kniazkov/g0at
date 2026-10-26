/** @file native_targets.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Human-readable enumeration of actually verified native toolchains.
 */
#pragma once
#include "options.h"

/** @brief Returns zero only when at least one target passed its compiler probe.
 * Uses the shared cleanup owner, never loads the generated probe library.
 */
int list_native_targets(const options_t *options);
