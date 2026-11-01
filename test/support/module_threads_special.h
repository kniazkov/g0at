/** @file module_threads_special.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent native lifetime and initializer pipe-handshake observations.
 */
#pragma once
#include "test/support/execution.h"

/** Source is an existing absolute Unicode main.goat path with sibling child.goat.
 * Run in its parent directory, preserving absolute native spelling or basename. */
void module_threads_lifetime(scenario_run_t *run,
                             const char *fixture,
                             const char *source_absolute,
                             bool relative);
/** Require waiting and ready BEFORE releasing both blocked inputs, then done and
 * owner exit zero. Each line/exit retains its original 30-second watchdog. */
void module_threads_handshake(scenario_run_t *run,
                              const char *fixture,
                              const char *archive,
                              const char *unrelated,
                              const char *const *environment);
