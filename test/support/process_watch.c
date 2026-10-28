/** @file process_watch.c
 * @copyright 2026 Ivan Kniazkov
 * @brief OS-only observer: intentionally independent of child_process and cleanup.
 */
#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif
#if defined(_WIN32) && !defined(_WIN32_WINNT)
#    define _WIN32_WINNT 0x0600
#endif
#include "test/support/process_watch.h"

#include <limits.h>
#include <stdlib.h>
#ifdef _WIN32
#    include <windows.h>
#else
#    include <errno.h>
#    include <poll.h>
#    include <signal.h>
#    include <sys/syscall.h>
#    include <time.h>
#    include <unistd.h>
#endif

struct process_watch_t {
#ifdef _WIN32
    HANDLE handle;
#else
    int handle;
#endif
};

static process_watch_result_t failure(unsigned long *error, unsigned long code) {
    if (error)
        *error = code;
    return PROCESS_WATCH_ERROR;
}

static void clear_error(unsigned long *error) {
    if (error)
        *error = 0;
}

static process_watch_result_t invalid(unsigned long *error) {
#ifdef _WIN32
    return failure(error, ERROR_INVALID_PARAMETER);
#else
    return failure(error, EINVAL);
#endif
}

uint64_t process_watch_now_ms(void) {
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec now;
    /* Failing closed prevents a clock failure from creating an unbounded wait. */
    if (clock_gettime(CLOCK_MONOTONIC, &now))
        return UINT64_MAX;
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
#endif
}

process_watch_t *process_watch_open(uint64_t pid, unsigned long *error) {
    clear_error(error);
#ifdef _WIN32
    if (!pid || pid > UINT32_MAX) {
#else
    if (!pid || pid > INT_MAX) {
#endif
        invalid(error);
        return NULL;
    }
    process_watch_t *watch = malloc(sizeof(*watch));
    if (!watch) {
#ifdef _WIN32
        failure(error, ERROR_NOT_ENOUGH_MEMORY);
#else
        failure(error, ENOMEM);
#endif
        return NULL;
    }
#ifdef _WIN32
    watch->handle = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, (DWORD)pid);
    if (!watch->handle) {
        failure(error, GetLastError());
#else
#    ifdef SYS_pidfd_open
    watch->handle = (int)syscall(SYS_pidfd_open, (pid_t)pid, 0);
#    else
    watch->handle = -1;
    errno = ENOSYS;
#    endif
    if (watch->handle < 0) {
        failure(error, errno);
#endif
        free(watch);
        return NULL;
    }
    return watch;
}

process_watch_result_t
process_watch_wait(process_watch_t *watch, uint64_t deadline_ms, unsigned long *error) {
    clear_error(error);
    if (!watch)
        return invalid(error);
    for (;;) {
        uint64_t now = process_watch_now_ms();
        uint64_t remaining = deadline_ms > now ? deadline_ms - now : 0;
#ifdef _WIN32
        /* Never pass INFINITE: even very distant deadlines stay bounded. */
        DWORD interval = remaining > (uint64_t)INFINITE - 1 ? INFINITE - 1 : (DWORD)remaining;
        DWORD result = WaitForSingleObject(watch->handle, interval);
        if (result == WAIT_OBJECT_0)
            return PROCESS_WATCH_OK;
        if (result == WAIT_FAILED)
            return failure(error, GetLastError());
        if (result != WAIT_TIMEOUT)
            return failure(error, ERROR_INVALID_DATA);
#else
        int interval = remaining > INT_MAX ? INT_MAX : (int)remaining;
        struct pollfd descriptor = {watch->handle, POLLIN, 0};
        int result = poll(&descriptor, 1, interval);
        if (result < 0) {
            if (errno == EINTR) {
                if (process_watch_now_ms() >= deadline_ms)
                    return PROCESS_WATCH_TIMEOUT;
                continue;
            }
            return failure(error, errno);
        }
        if (descriptor.revents & POLLNVAL)
            return failure(error, EBADF);
        /* A reaped process can also expose HUP; the pinned identity is dead. */
        if (descriptor.revents & (POLLIN | POLLHUP))
            return PROCESS_WATCH_OK;
        if (descriptor.revents & POLLERR)
            return failure(error, EIO);
#endif
        if (process_watch_now_ms() >= deadline_ms)
            return PROCESS_WATCH_TIMEOUT;
    }
}

process_watch_result_t
process_watch_alive(process_watch_t *watch, bool *alive, unsigned long *error) {
    if (!alive)
        return invalid(error);
    process_watch_result_t result = process_watch_wait(watch, 0, error);
    if (result == PROCESS_WATCH_ERROR)
        return result;
    *alive = result == PROCESS_WATCH_TIMEOUT;
    return PROCESS_WATCH_OK;
}

process_watch_result_t process_watch_kill(process_watch_t *watch, unsigned long *error) {
    clear_error(error);
    if (!watch)
        return invalid(error);
#ifdef _WIN32
    if (!TerminateProcess(watch->handle, 99))
        return failure(error, GetLastError());
#else
#    ifdef SYS_pidfd_send_signal
    if (syscall(SYS_pidfd_send_signal, watch->handle, SIGKILL, NULL, 0))
        return failure(error, errno);
#    else
    return failure(error, ENOSYS);
#    endif
#endif
    return PROCESS_WATCH_OK;
}

process_watch_result_t process_watch_close(process_watch_t **watch, unsigned long *error) {
    clear_error(error);
    if (!watch)
        return invalid(error);
    if (!*watch)
        return PROCESS_WATCH_OK;
    process_watch_t *owned = *watch;
    *watch = NULL;
    process_watch_result_t result = PROCESS_WATCH_OK;
#ifdef _WIN32
    if (!CloseHandle(owned->handle))
        result = failure(error, GetLastError());
#else
    /* Linux releases the descriptor even on EINTR; retrying could close reuse. */
    if (close(owned->handle))
        result = failure(error, errno);
#endif
    free(owned);
    return result;
}

process_watch_result_t process_watch_until(process_watch_predicate_t predicate,
                                           void *context,
                                           uint64_t deadline_ms,
                                           unsigned long *error) {
    clear_error(error);
    if (!predicate)
        return invalid(error);
#ifdef _WIN32
    HANDLE interval_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!interval_event)
        return failure(error, GetLastError());
#endif
    process_watch_result_t result;
    for (;;) {
        result = predicate(context, error);
        if (result != PROCESS_WATCH_TIMEOUT)
            break;
        uint64_t now = process_watch_now_ms();
        if (now >= deadline_ms)
            break;
        uint64_t remaining = deadline_ms - now;
        unsigned interval = remaining > 10 ? 10 : (unsigned)remaining;
#ifdef _WIN32
        if (WaitForSingleObject(interval_event, interval) == WAIT_FAILED) {
            result = failure(error, GetLastError());
            break;
        }
#else
        if (poll(NULL, 0, (int)interval) < 0 && errno != EINTR) {
            result = failure(error, errno);
            break;
        }
#endif
    }
#ifdef _WIN32
    if (!CloseHandle(interval_event) && result != PROCESS_WATCH_ERROR)
        result = failure(error, GetLastError());
#endif
    return result;
}
