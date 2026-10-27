/** @file process_exit.c
 * @copyright 2026 Ivan Kniazkov
 * @brief One-shot process shutdown with an independent output-deadline watchdog.
 */
#ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#endif
#include "lib/process/process_exit.h"

#include "lib/platform/windows_target.h"
#include "lib/process/child_process.h"

#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#    include <windows.h>
#else
#    include <errno.h>
#    include <pthread.h>
#    include <signal.h>
#    include <sys/eventfd.h>
#    include <time.h>
#    include <unistd.h>
#endif

bool process_exit_select(process_exit_arbiter_t *arbiter, unsigned code) {
    if (!arbiter || code > 255)
        return false;
    unsigned expected = 0;
    return atomic_compare_exchange_strong_explicit(&arbiter->selection,
                                                   &expected,
                                                   code + 1,
                                                   memory_order_acq_rel,
                                                   memory_order_acquire);
}

int process_exit_selected(const process_exit_arbiter_t *arbiter) {
    if (!arbiter)
        return -1;
    return (int)atomic_load_explicit(&arbiter->selection, memory_order_acquire) - 1;
}

static process_exit_arbiter_t termination;
/** @brief Written once by the winner before publishing flush_started. */
static uint64_t deadline;
/** @brief 0 unprepared, 1 preparing, 2 ready, 3 unavailable. */
static atomic_uint preparation;
static atomic_bool watchdog_ready, flush_started;
#ifdef _WIN32
static HANDLE wake_event;
#else
static int wake_event = -1;
/* Resolve the terminating symbol through an eager data relocation, not a first
   lazy PLT call while another native thread might hold the dynamic loader lock. */
static void (*volatile terminate_os_process)(int) = _Exit;
#endif

/** @brief OS teardown owns every remaining thread/library; no CRT finalization. */
static _Noreturn void terminate_selected(void) {
    int code = process_exit_selected(&termination);
#ifdef _WIN32
    TerminateProcess(GetCurrentProcess(), (UINT)code);
    /* GetCurrentProcess() is an always-valid pseudo handle. Never fall back to
       ExitProcess, which invokes potentially blocked DLL detach handlers. */
    for (;;)
        Sleep(INFINITE);
#else
    terminate_os_process(code);
    for (;;) {
    }
#endif
}

static void pause_ms(unsigned milliseconds) {
#ifdef _WIN32
    Sleep(milliseconds);
#else
    struct timespec remaining = {milliseconds / 1000, (long)(milliseconds % 1000) * 1000000};
    while (nanosleep(&remaining, &remaining) && errno == EINTR) {
    }
#endif
}

#ifdef _WIN32
static DWORD WINAPI watchdog(void *unused) {
#else
static void *watchdog(void *unused) {
#endif
    (void)unused;
    /* Resolve the deadline sleep path before any user/native code can hold an
       OS loader lock. The watchdog needs no first-call dynamic binding later. */
    pause_ms(0);
#ifndef _WIN32
    uint64_t probe;
    (void)read(wake_event, &probe, 0);  /* Nonblocking, invalid eventfd size; binds read. */
    volatile int initial_errno = errno; /* Binds the thread-local errno accessor too. */
    (void)initial_errno;
#endif
#if defined(GOAT_TEST_PROCESS_EXIT_FAILURE) && GOAT_TEST_PROCESS_EXIT_FAILURE == 2
    /* Simulates loader-lock-delayed thread entry in an isolated test build. */
    while (atomic_load_explicit(&preparation, memory_order_acquire) == 1)
        pause_ms(1);
#endif
    atomic_store_explicit(&watchdog_ready, true, memory_order_release);
#ifdef _WIN32
    WaitForSingleObject(wake_event, INFINITE);
#else
    uint64_t wake;
    while (read(wake_event, &wake, sizeof(wake)) < 0 && errno == EINTR) {
    }
#endif
    /* The publication is independent of OS wait semantics and publishes the
       common deadline before this thread inspects its ordinary immutable value. */
    while (!atomic_load_explicit(&flush_started, memory_order_acquire))
        pause_ms(1);
    for (;;) {
        uint64_t now = child_monotonic_ms();
        if (now >= deadline)
            terminate_selected();
        pause_ms((unsigned)(deadline - now));
    }
}

bool process_exit_prepare(void) {
    unsigned expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&preparation,
                                                 &expected,
                                                 1,
                                                 memory_order_acq_rel,
                                                 memory_order_acquire)) {
        while (expected == 1) {
            pause_ms(1);
            expected = atomic_load_explicit(&preparation, memory_order_acquire);
        }
        return expected == 2;
    }
#if defined(GOAT_TEST_PROCESS_EXIT_FAILURE) && GOAT_TEST_PROCESS_EXIT_FAILURE == 1
    /* Separate fault-injection executable only; production has no failure knob. */
    atomic_store_explicit(&preparation, 3, memory_order_release);
    return false;
#endif
    uint64_t ready_deadline = child_monotonic_ms() + 1000;
#ifdef _WIN32
    wake_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    HANDLE thread = wake_event ? CreateThread(NULL, 0, watchdog, NULL, 0, NULL) : NULL;
    bool success = thread != NULL;
    if (thread)
        CloseHandle(thread);
    else if (wake_event)
        CloseHandle(wake_event);
#else
    /* One nonblocking notification cannot fill an initially zero event counter.
       Its read side waits in the already running watchdog, never in user code. */
    wake_event = eventfd(0, EFD_CLOEXEC);
    pthread_t thread;
    bool success = wake_event >= 0 && !pthread_create(&thread, NULL, watchdog, NULL);
    if (success)
        pthread_detach(thread);
    else if (wake_event >= 0)
        close(wake_event);
#endif
    if (success) {
        while (!atomic_load_explicit(&watchdog_ready, memory_order_acquire)
               && child_monotonic_ms() < ready_deadline)
            pause_ms(1);
        success = atomic_load_explicit(&watchdog_ready, memory_order_acquire);
        /* A late thread retains its event and process-lifetime state. Never
           close them underneath it; an unavailable watchdog skips later flush. */
    }
    atomic_store_explicit(&preparation, success ? 2 : 3, memory_order_release);
    return success;
}

_Noreturn void process_exit_request(unsigned code) {
    /* Validation failures belong to the language layer. Defensively avoid an
       invalid status if a host violates this API's precondition. */
    if (code > 255)
        code = EXIT_FAILURE;
    if (!process_exit_select(&termination, code)) {
        /* The winner owns output flushing. Losers neither return to user code
           nor terminate early and discard its remaining bounded output budget. */
        for (;;)
            pause_ms(1000);
    }
    if (atomic_load_explicit(&preparation, memory_order_acquire) != 2)
        terminate_selected();
    deadline = child_monotonic_ms() + 1000;
    atomic_store_explicit(&flush_started, true, memory_order_release);
#ifdef _WIN32
    if (!SetEvent(wake_event))
        terminate_selected();
#else
    uint64_t wake = 1;
    ssize_t written;
    do {
        written = write(wake_event, &wake, sizeof(wake));
    } while (written < 0 && errno == EINTR);
    if (written != sizeof(wake))
        terminate_selected();
    /* Flush errors must not replace the selected status with SIGPIPE. Blocking
       it only in this thread avoids changing the application's signal policy. */
    sigset_t blocked;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &blocked, NULL))
        terminate_selected();
#endif
    /* Never fflush(NULL): unrelated streams and their handlers are not part of
       this operation. Both writes and stream-lock waits share this deadline. */
    fflush(stdout);
    fflush(stderr);
    terminate_selected();
}
