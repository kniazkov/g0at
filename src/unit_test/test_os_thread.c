/** @file test_os_thread.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Synchronization uses predicates and handshakes, never timing assumptions.
 */
#include "test_os_thread.h"

#include "lib/allocate.h"
#include "lib/child_process.h"
#include "lib/file_system.h"
#include "lib/os_thread.h"
#include "test_macro.h"

#include <stdint.h>
#include <stdio.h>

static void *echo(void *data) {
    return data;
}

static void *foreign_unlock(void *data) {
    os_mutex_t *mutex = data;
    return unlock_os_mutex(mutex) == OS_THREAD_INVALID && try_lock_os_mutex(mutex) == OS_THREAD_BUSY
               ? data
               : NULL;
}

typedef struct {
    os_thread_t *thread;
    os_mutex_t *mutex;
} self_join_t;

static void *self_join(void *data) {
    self_join_t *task = data;
    if (lock_os_mutex(task->mutex) != OS_THREAD_OK)
        return NULL;
    bool ok = join_os_thread(task->thread, NULL) == OS_THREAD_DEADLOCK;
    ok &= unlock_os_mutex(task->mutex) == OS_THREAD_OK;
    return ok ? data : NULL;
}

static bool lifecycle(void) {
    int value = 37;
    for (int i = 0; i < 32; i++) {
        os_thread_t *thread;
        ASSERT(create_os_thread(&thread, echo, &value, i % 2 ? 128 * 1024 : 0) == OS_THREAD_OK);
        ASSERT(destroy_os_thread(thread) == OS_THREAD_BUSY);
        void *result = NULL;
        ASSERT(join_os_thread(thread, &result) == OS_THREAD_OK && result == &value);
        ASSERT(join_os_thread(thread, NULL) == OS_THREAD_INVALID);
        ASSERT(destroy_os_thread(thread) == OS_THREAD_OK);
    }
    self_join_t self;
    ASSERT(create_os_mutex(&self.mutex) == OS_THREAD_OK);
    ASSERT(lock_os_mutex(self.mutex) == OS_THREAD_OK);
    ASSERT(create_os_thread(&self.thread, self_join, &self, 0) == OS_THREAD_OK);
    /* Publish the handle before the worker is allowed to inspect it. */
    ASSERT(unlock_os_mutex(self.mutex) == OS_THREAD_OK);
    void *result;
    ASSERT(join_os_thread(self.thread, &result) == OS_THREAD_OK && result == &self);
    ASSERT(destroy_os_thread(self.thread) == OS_THREAD_OK);
    ASSERT(destroy_os_mutex(self.mutex) == OS_THREAD_OK);
    return true;
}

static bool misuse(void) {
    os_thread_t *thread = (os_thread_t *)(uintptr_t)1;
    ASSERT(create_os_thread(&thread, NULL, NULL, 0) == OS_THREAD_INVALID && !thread);
    ASSERT(create_os_thread(NULL, echo, NULL, 0) == OS_THREAD_INVALID);
    ASSERT(join_os_thread(NULL, NULL) == OS_THREAD_INVALID);
    ASSERT(destroy_os_thread(NULL) == OS_THREAD_OK);
    ASSERT(create_os_mutex(NULL) == OS_THREAD_INVALID);
    ASSERT(lock_os_mutex(NULL) == OS_THREAD_INVALID);
    ASSERT(try_lock_os_mutex(NULL) == OS_THREAD_INVALID);
    ASSERT(unlock_os_mutex(NULL) == OS_THREAD_INVALID);
    ASSERT(create_os_condition(NULL) == OS_THREAD_INVALID);
    ASSERT(wait_os_condition(NULL, NULL) == OS_THREAD_INVALID);
    ASSERT(signal_os_condition(NULL) == OS_THREAD_INVALID);
    ASSERT(broadcast_os_condition(NULL) == OS_THREAD_INVALID);
    ASSERT(destroy_os_mutex(NULL) == OS_THREAD_OK);
    ASSERT(destroy_os_condition(NULL) == OS_THREAD_OK);
    os_mutex_t *mutex;
    os_condition_t *condition;
    ASSERT(create_os_mutex(&mutex) == OS_THREAD_OK);
    ASSERT(create_os_condition(&condition) == OS_THREAD_OK);
    ASSERT(unlock_os_mutex(mutex) == OS_THREAD_INVALID);
    ASSERT(wait_os_condition(condition, mutex) == OS_THREAD_INVALID);
    ASSERT(try_lock_os_mutex(mutex) == OS_THREAD_OK);
    ASSERT(lock_os_mutex(mutex) == OS_THREAD_DEADLOCK);
    ASSERT(try_lock_os_mutex(mutex) == OS_THREAD_DEADLOCK);
    ASSERT(destroy_os_mutex(mutex) == OS_THREAD_BUSY);
    ASSERT(create_os_thread(&thread, foreign_unlock, mutex, 0) == OS_THREAD_OK);
    void *result;
    ASSERT(join_os_thread(thread, &result) == OS_THREAD_OK && result == mutex);
    ASSERT(destroy_os_thread(thread) == OS_THREAD_OK);
    ASSERT(unlock_os_mutex(mutex) == OS_THREAD_OK);
    /* Notifications without waiters are legal and are not stored. */
    ASSERT(signal_os_condition(condition) == OS_THREAD_OK);
    ASSERT(broadcast_os_condition(condition) == OS_THREAD_OK);
    ASSERT(destroy_os_condition(condition) == OS_THREAD_OK);
    ASSERT(destroy_os_mutex(mutex) == OS_THREAD_OK);
    return true;
}

typedef struct {
    os_mutex_t *mutex;
    os_condition_t *ready, *work;
    unsigned waiting, counter;
    bool start;
} concurrent_t;

static void *counter_worker(void *data) {
    concurrent_t *task = data;
    if (lock_os_mutex(task->mutex) != OS_THREAD_OK)
        return NULL;
    task->waiting++;
    if (signal_os_condition(task->ready) != OS_THREAD_OK)
        return NULL;
    while (!task->start)
        if (wait_os_condition(task->work, task->mutex) != OS_THREAD_OK)
            return NULL;
    if (unlock_os_mutex(task->mutex) != OS_THREAD_OK)
        return NULL;
    for (int i = 0; i < 10000; i++) {
        if (lock_os_mutex(task->mutex) != OS_THREAD_OK)
            return NULL;
        task->counter++;
        if (unlock_os_mutex(task->mutex) != OS_THREAD_OK)
            return NULL;
    }
    return data;
}

static bool concurrent(void) {
    concurrent_t task = {0};
    ASSERT(create_os_mutex(&task.mutex) == OS_THREAD_OK);
    ASSERT(create_os_condition(&task.ready) == OS_THREAD_OK);
    ASSERT(create_os_condition(&task.work) == OS_THREAD_OK);
    os_thread_t *threads[4];
    for (int i = 0; i < 4; i++)
        ASSERT(create_os_thread(&threads[i], counter_worker, &task, 0) == OS_THREAD_OK);
    ASSERT(lock_os_mutex(task.mutex) == OS_THREAD_OK);
    while (task.waiting != 4)
        ASSERT(wait_os_condition(task.ready, task.mutex) == OS_THREAD_OK);
    /* All four announced readiness while holding this mutex, then atomically waited. */
    ASSERT(destroy_os_condition(task.work) == OS_THREAD_BUSY);
    ASSERT(unlock_os_mutex(task.mutex) == OS_THREAD_OK);
    ASSERT(destroy_os_mutex(task.mutex) == OS_THREAD_BUSY);
    ASSERT(lock_os_mutex(task.mutex) == OS_THREAD_OK);
    task.start = true;
    ASSERT(broadcast_os_condition(task.work) == OS_THREAD_OK);
    ASSERT(unlock_os_mutex(task.mutex) == OS_THREAD_OK);
    for (int i = 0; i < 4; i++) {
        void *result;
        ASSERT(join_os_thread(threads[i], &result) == OS_THREAD_OK && result == &task);
        ASSERT(destroy_os_thread(threads[i]) == OS_THREAD_OK);
    }
    ASSERT(task.counter == 40000);
    ASSERT(destroy_os_condition(task.ready) == OS_THREAD_OK);
    ASSERT(destroy_os_condition(task.work) == OS_THREAD_OK);
    ASSERT(destroy_os_mutex(task.mutex) == OS_THREAD_OK);
    return true;
}

typedef struct {
    os_mutex_t *mutex;
    os_condition_t *condition;
    unsigned request, response;
} exchange_t;

static void *consumer(void *data) {
    exchange_t *task = data;
    if (lock_os_mutex(task->mutex) != OS_THREAD_OK)
        return NULL;
    for (unsigned i = 1; i <= 100; i++) {
        while (task->request != i)
            if (wait_os_condition(task->condition, task->mutex) != OS_THREAD_OK)
                return NULL;
        task->response = i;
        if (signal_os_condition(task->condition) != OS_THREAD_OK)
            return NULL;
    }
    return unlock_os_mutex(task->mutex) == OS_THREAD_OK ? data : NULL;
}

static bool signal_exchange(void) {
    exchange_t task = {0};
    ASSERT(create_os_mutex(&task.mutex) == OS_THREAD_OK);
    ASSERT(create_os_condition(&task.condition) == OS_THREAD_OK);
    os_thread_t *thread;
    ASSERT(create_os_thread(&thread, consumer, &task, 0) == OS_THREAD_OK);
    ASSERT(lock_os_mutex(task.mutex) == OS_THREAD_OK);
    for (unsigned i = 1; i <= 100; i++) {
        task.request = i;
        ASSERT(signal_os_condition(task.condition) == OS_THREAD_OK);
        while (task.response != i)
            ASSERT(wait_os_condition(task.condition, task.mutex) == OS_THREAD_OK);
    }
    ASSERT(unlock_os_mutex(task.mutex) == OS_THREAD_OK);
    void *result;
    ASSERT(join_os_thread(thread, &result) == OS_THREAD_OK && result == &task);
    ASSERT(destroy_os_thread(thread) == OS_THREAD_OK);
    ASSERT(destroy_os_condition(task.condition) == OS_THREAD_OK);
    ASSERT(destroy_os_mutex(task.mutex) == OS_THREAD_OK);
    return true;
}

bool run_os_thread_tests(void) {
    size_t before = get_allocated_memory_size();
    return lifecycle() && misuse() && concurrent() && signal_exchange()
           && get_allocated_memory_size() == before;
}

bool test_os_threads(void) {
    const char *args[] = {"unit_testing", "--os-thread-tests", NULL};
    child_options_t options = {.arguments = args,
                               .empty_stdin = true,
                               .capture_output = true,
                               .deadline = child_deadline_after(10000)};
    child_process_t *child = start_child_process(&options);
    ASSERT(child);
    const child_result_t *result = wait_child_process(child);
    bool success = result->status == CHILD_EXITED && result->exit_code == 0 && !result->system_error
                   && !result->cleanup_error;
    if (!success) {
        FPRINTF(stderr,
                "Thread fixture: status=%d, exit=%lld, error=%lu, cleanup=%lu\n",
                result->status,
                (long long)result->exit_code,
                result->system_error,
                result->cleanup_error);
        FWRITE(result->err.data, 1, result->err.size, stderr);
    }
    destroy_child_process(child);
    return success;
}
