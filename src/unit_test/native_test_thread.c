/** @file native_test_thread.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Exercises native adapters on a reserved small OS thread stack.
 */
#include "native_test_thread.h"

#include "lib/os_thread.h"

bool run_on_small_stack(void *(*callback)(void *), void *data) {
    os_thread_t *thread;
    if (create_os_thread(&thread, callback, data, 128 * 1024) != OS_THREAD_OK)
        return false;
    if (join_os_thread(thread, NULL) != OS_THREAD_OK)
        return false;
    return destroy_os_thread(thread) == OS_THREAD_OK;
}
