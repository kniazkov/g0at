/** @file native_test_thread.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Test-only host thread with a bounded stack reservation.
 */
#pragma once
#include <stdbool.h>
/** @brief Runs and joins a callback on a thread with a 128 KiB stack. */
bool run_on_small_stack(void *(*callback)(void *), void *data);
