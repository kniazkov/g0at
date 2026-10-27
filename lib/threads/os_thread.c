/** @file os_thread.c
 * @copyright 2026 Ivan Kniazkov
 * @brief pthread and Windows implementations behind opaque portable handles.
 */
#ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#endif
#include "lib/threads/os_thread.h"

#include "lib/memory/allocate.h"
#include "lib/platform/windows_target.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#ifdef _WIN32
#    include <limits.h>
#    include <process.h>
#    include <windows.h>
#else
#    include <errno.h>
#    include <pthread.h>
#endif

struct os_thread_t {
    os_thread_function_t function;
    void *argument, *result;
    bool joined;
#ifdef _WIN32
    HANDLE handle;
    unsigned id;
#else
    pthread_t handle;
#endif
};

struct os_mutex_t {
    atomic_uintptr_t owner;
    atomic_uint condition_waiters;
    bool recursive;
    size_t depth; /* Accessed only by the owner while the native lock is held. */
#ifdef _WIN32
    SRWLOCK native;
#else
    pthread_mutex_t native;
#endif
};

struct os_condition_t {
    atomic_uint waiters;
#ifdef _WIN32
    CONDITION_VARIABLE native;
#else
    pthread_cond_t native;
#endif
};

/* Unique among live C threads; this gives no thread a special VM role. */
static _Thread_local unsigned char identity;

static uintptr_t current_owner(void) {
    return (uintptr_t)&identity;
}
#ifdef _WIN32
static unsigned __stdcall thread_entry(void *data) {
#else
static void *thread_entry(void *data) {
#endif
    os_thread_t *thread = data;
    thread->result = thread->function(thread->argument);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

os_thread_status_t create_os_thread(os_thread_t **out,
                                    os_thread_function_t function,
                                    void *argument,
                                    size_t stack_size) {
    if (!out)
        return OS_THREAD_INVALID;
    *out = NULL;
    if (!function)
        return OS_THREAD_INVALID;
    os_thread_t *thread = CALLOC(sizeof(*thread));
    thread->function = function;
    thread->argument = argument;
#ifdef _WIN32
    if (stack_size > UINT_MAX) {
        FREE(thread);
        return OS_THREAD_INVALID;
    }
    thread->handle = (HANDLE)_beginthreadex(NULL,
                                            (unsigned)stack_size,
                                            thread_entry,
                                            thread,
                                            stack_size ? STACK_SIZE_PARAM_IS_A_RESERVATION : 0,
                                            &thread->id);
    if (!thread->handle) {
        FREE(thread);
        return OS_THREAD_ERROR;
    }
#else
    pthread_attr_t attributes;
    int error = pthread_attr_init(&attributes);
    if (error) {
        FREE(thread);
        return OS_THREAD_ERROR;
    }
    /* musl defaults to a small fixed stack, below the native entry guard.
     * Match the ordinary Linux development stack for Goat-owned workers while
     * preserving explicit small-stack requests and larger platform defaults. */
#    ifdef __linux__
    if (!stack_size) {
        size_t default_size = 0;
        error = pthread_attr_getstacksize(&attributes, &default_size);
        if (!error && default_size < 8 * 1024 * 1024)
            stack_size = 8 * 1024 * 1024;
    }
#    endif
    if (!error && stack_size)
        error = pthread_attr_setstacksize(&attributes, stack_size);
    if (!error)
        error = pthread_create(&thread->handle, &attributes, thread_entry, thread);
    pthread_attr_destroy(&attributes);
    if (error) {
        FREE(thread);
        return error == EINVAL ? OS_THREAD_INVALID : OS_THREAD_ERROR;
    }
#endif
    *out = thread;
    return OS_THREAD_OK;
}

os_thread_status_t join_os_thread(os_thread_t *thread, void **result) {
    if (!thread || thread->joined)
        return OS_THREAD_INVALID;
#ifdef _WIN32
    if (GetCurrentThreadId() == thread->id
        && WaitForSingleObject(thread->handle, 0) == WAIT_TIMEOUT)
        return OS_THREAD_DEADLOCK;
    if (WaitForSingleObject(thread->handle, INFINITE) != WAIT_OBJECT_0)
        return OS_THREAD_ERROR;
#else
    if (pthread_equal(pthread_self(), thread->handle))
        return OS_THREAD_DEADLOCK;
    if (pthread_join(thread->handle, NULL))
        return OS_THREAD_ERROR;
#endif
    thread->joined = true;
    if (result)
        *result = thread->result;
    return OS_THREAD_OK;
}

os_thread_status_t destroy_os_thread(os_thread_t *thread) {
    if (!thread)
        return OS_THREAD_OK;
    if (!thread->joined)
        return OS_THREAD_BUSY;
#ifdef _WIN32
    if (!CloseHandle(thread->handle))
        return OS_THREAD_ERROR;
#endif
    FREE(thread);
    return OS_THREAD_OK;
}

static os_thread_status_t create_mutex(os_mutex_t **out, bool recursive) {
    if (!out)
        return OS_THREAD_INVALID;
    *out = NULL;
    os_mutex_t *mutex = ALLOC(sizeof(*mutex));
    atomic_init(&mutex->owner, 0);
    atomic_init(&mutex->condition_waiters, 0);
    mutex->recursive = recursive;
    mutex->depth = 0;
#ifdef _WIN32
    InitializeSRWLock(&mutex->native);
#else
    if (pthread_mutex_init(&mutex->native, NULL)) {
        FREE(mutex);
        return OS_THREAD_ERROR;
    }
#endif
    *out = mutex;
    return OS_THREAD_OK;
}

os_thread_status_t create_os_mutex(os_mutex_t **out) {
    return create_mutex(out, false);
}

os_thread_status_t create_os_recursive_mutex(os_mutex_t **out) {
    return create_mutex(out, true);
}

static os_thread_status_t reenter_mutex(os_mutex_t *mutex) {
    if (!mutex->recursive)
        return OS_THREAD_DEADLOCK;
    if (mutex->depth == SIZE_MAX)
        return OS_THREAD_ERROR;
    mutex->depth++;
    return OS_THREAD_OK;
}

os_thread_status_t lock_os_mutex(os_mutex_t *mutex) {
    if (!mutex)
        return OS_THREAD_INVALID;
    if (atomic_load(&mutex->owner) == current_owner())
        return reenter_mutex(mutex);
#ifdef _WIN32
    AcquireSRWLockExclusive(&mutex->native);
#else
    if (pthread_mutex_lock(&mutex->native))
        return OS_THREAD_ERROR;
#endif
    mutex->depth = 1;
    atomic_store(&mutex->owner, current_owner());
    return OS_THREAD_OK;
}

os_thread_status_t try_lock_os_mutex(os_mutex_t *mutex) {
    if (!mutex)
        return OS_THREAD_INVALID;
    if (atomic_load(&mutex->owner) == current_owner())
        return reenter_mutex(mutex);
#ifdef _WIN32
    if (!TryAcquireSRWLockExclusive(&mutex->native))
        return OS_THREAD_BUSY;
#else
    int error = pthread_mutex_trylock(&mutex->native);
    if (error)
        return error == EBUSY ? OS_THREAD_BUSY : OS_THREAD_ERROR;
#endif
    mutex->depth = 1;
    atomic_store(&mutex->owner, current_owner());
    return OS_THREAD_OK;
}

os_thread_status_t unlock_os_mutex(os_mutex_t *mutex) {
    if (!mutex || atomic_load(&mutex->owner) != current_owner())
        return OS_THREAD_INVALID;
    if (--mutex->depth)
        return OS_THREAD_OK;
    atomic_store(&mutex->owner, 0);
#ifdef _WIN32
    ReleaseSRWLockExclusive(&mutex->native);
#else
    if (pthread_mutex_unlock(&mutex->native)) {
        mutex->depth = 1;
        atomic_store(&mutex->owner, current_owner());
        return OS_THREAD_ERROR;
    }
#endif
    return OS_THREAD_OK;
}

size_t os_mutex_depth(const os_mutex_t *mutex) {
    return mutex && atomic_load(&mutex->owner) == current_owner() ? mutex->depth : 0;
}

os_thread_status_t destroy_os_mutex(os_mutex_t *mutex) {
    if (!mutex)
        return OS_THREAD_OK;
    if (atomic_load(&mutex->owner) || atomic_load(&mutex->condition_waiters))
        return OS_THREAD_BUSY;
#ifndef _WIN32
    if (pthread_mutex_destroy(&mutex->native))
        return OS_THREAD_ERROR;
#endif
    FREE(mutex);
    return OS_THREAD_OK;
}

os_thread_status_t create_os_condition(os_condition_t **out) {
    if (!out)
        return OS_THREAD_INVALID;
    *out = NULL;
    os_condition_t *condition = ALLOC(sizeof(*condition));
    atomic_init(&condition->waiters, 0);
#ifdef _WIN32
    InitializeConditionVariable(&condition->native);
#else
    if (pthread_cond_init(&condition->native, NULL)) {
        FREE(condition);
        return OS_THREAD_ERROR;
    }
#endif
    *out = condition;
    return OS_THREAD_OK;
}

os_thread_status_t wait_os_condition(os_condition_t *condition, os_mutex_t *mutex) {
    if (!condition || !mutex || atomic_load(&mutex->owner) != current_owner())
        return OS_THREAD_INVALID;
    size_t depth = mutex->depth;
    mutex->depth = 0;
    atomic_fetch_add(&condition->waiters, 1);
    atomic_fetch_add(&mutex->condition_waiters, 1);
    atomic_store(&mutex->owner, 0);
#ifdef _WIN32
    bool success = SleepConditionVariableSRW(&condition->native, &mutex->native, INFINITE, 0) != 0;
#else
    bool success = pthread_cond_wait(&condition->native, &mutex->native) == 0;
#endif
    mutex->depth = depth;
    atomic_store(&mutex->owner, current_owner());
    atomic_fetch_sub(&mutex->condition_waiters, 1);
    atomic_fetch_sub(&condition->waiters, 1);
    return success ? OS_THREAD_OK : OS_THREAD_ERROR;
}

os_thread_status_t signal_os_condition(os_condition_t *condition) {
    if (!condition)
        return OS_THREAD_INVALID;
#ifdef _WIN32
    WakeConditionVariable(&condition->native);
#else
    if (pthread_cond_signal(&condition->native))
        return OS_THREAD_ERROR;
#endif
    return OS_THREAD_OK;
}

os_thread_status_t broadcast_os_condition(os_condition_t *condition) {
    if (!condition)
        return OS_THREAD_INVALID;
#ifdef _WIN32
    WakeAllConditionVariable(&condition->native);
#else
    if (pthread_cond_broadcast(&condition->native))
        return OS_THREAD_ERROR;
#endif
    return OS_THREAD_OK;
}

os_thread_status_t destroy_os_condition(os_condition_t *condition) {
    if (!condition)
        return OS_THREAD_OK;
    if (atomic_load(&condition->waiters))
        return OS_THREAD_BUSY;
#ifndef _WIN32
    if (pthread_cond_destroy(&condition->native))
        return OS_THREAD_ERROR;
#endif
    FREE(condition);
    return OS_THREAD_OK;
}
