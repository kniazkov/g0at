/** @file process_exit_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Coordinated subprocess probes of the process-wide exit arbiter and output budget.
 */
#ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#endif
#include "lib/files/file_system.h"
#include "lib/process/process_exit.h"
#include "lib/threads/os_thread.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <io.h>
#    define LOCK_STREAM _lock_file
#    define UNLOCK_STREAM _unlock_file
#    define CLOSE_FD _close
#else
#    include <unistd.h>
#    define LOCK_STREAM flockfile
#    define UNLOCK_STREAM funlockfile
#    define CLOSE_FD close
#endif

static os_mutex_t *mutex;
static os_condition_t *changed;
static bool ready, start;
static FILE *held;
static const char *marker;
static char out_buffer[2 * 1024 * 1024], err_buffer[2 * 1024 * 1024];
static char payload[1024 * 1024];

static void signal_parent(void) {
    FILE *file = FOPEN(marker, "wb");
    assert(file);
    assert(FPUTS("ready", file) >= 0);
    assert(!FCLOSE(file));
}

static void at_exit(void) {
    /* This must never run: CRT finalization could unload live native code. */
    FPUTS("ATEXIT", stderr);
}

static void *hold_stream(void *unused) {
    (void)unused;
    LOCK_STREAM(held);
    assert(lock_os_mutex(mutex) == OS_THREAD_OK);
    ready = true;
    signal_os_condition(changed);
    while (!start)
        wait_os_condition(changed, mutex);
    /* No test releases this lock. Only OS process termination can finish. */
    return NULL;
}

static void *race_exit(void *value) {
    assert(lock_os_mutex(mutex) == OS_THREAD_OK);
    while (!start)
        wait_os_condition(changed, mutex);
    unlock_os_mutex(mutex);
    process_exit_request((unsigned)(uintptr_t)value);
}

static void *prepare_exit(void *unused) {
    (void)unused;
    assert(process_exit_prepare());
    return NULL;
}

static void *shared_exit(void *unused) {
    (void)unused;
    signal_parent();
    process_exit_request(73);
}

static void arbiter_tests(void) {
    process_exit_arbiter_t arbiter = {0};
    assert(process_exit_selected(&arbiter) == -1);
    assert(!process_exit_select(&arbiter, 256));
    assert(process_exit_selected(&arbiter) == -1);
    assert(process_exit_select(&arbiter, 0));
    assert(process_exit_selected(&arbiter) == 0);
    for (unsigned i = 0; i <= 255; ++i) {
        assert(!process_exit_select(&arbiter, i));
        assert(process_exit_selected(&arbiter) == 0);
    }
    process_exit_arbiter_t second = {0};
    assert(process_exit_select(&second, 255));
    assert(!process_exit_select(&second, 0));
    assert(process_exit_selected(&second) == 255);
    assert(!process_exit_select(NULL, 0));
    assert(process_exit_selected(NULL) == -1);
}

int main(int argc, char **argv) {
    fs_error_t argument_error = {0};
    assert(fs_arguments(&argc, &argv, &argument_error));
    assert(argc >= 2);
    if (!strcmp(argv[1], "arbiter")) {
        arbiter_tests();
        free_fs_arguments(argc, argv);
        return 0;
    }
    if (!strcmp(argv[1], "unprepared"))
        process_exit_request(83);
    if (!strcmp(argv[1], "failed-prepare")) {
        assert(!process_exit_prepare());
        assert(!process_exit_prepare());
        process_exit_request(91);
    }
    if (!strcmp(argv[1], "prepare-race")) {
        os_thread_t *threads[8];
        for (unsigned i = 0; i < 8; ++i)
            assert(create_os_thread(threads + i, prepare_exit, NULL, 0) == OS_THREAD_OK);
        for (unsigned i = 0; i < 8; ++i) {
            assert(join_os_thread(threads[i], NULL) == OS_THREAD_OK);
            assert(destroy_os_thread(threads[i]) == OS_THREAD_OK);
        }
        assert(process_exit_prepare());
        free_fs_arguments(argc, argv);
        return 0;
    }
    assert(process_exit_prepare());
    assert(process_exit_prepare());
    assert(argc == 3);
    marker = argv[2];
    assert(!atexit(at_exit));
    assert(create_os_mutex(&mutex) == OS_THREAD_OK);
    assert(create_os_condition(&changed) == OS_THREAD_OK);
    assert(!setvbuf(stdout, out_buffer, _IOFBF, sizeof(out_buffer)));
    assert(!setvbuf(stderr, err_buffer, _IOFBF, sizeof(err_buffer)));
    if (!strcmp(argv[1], "fast")) {
        FPUTS("stdout-without-newline", stdout);
        FPUTS("stderr-without-newline", stderr);
        signal_parent();
        assert(getchar() == 'x');
        process_exit_request(255);
    }
    if (!strcmp(argv[1], "error") || !strcmp(argv[1], "pipe-error")) {
        FPUTS("buffered", stdout);
        if (!strcmp(argv[1], "error"))
            CLOSE_FD(1);
        signal_parent();
        assert(getchar() == 'x');
        process_exit_request(77);
    }
    if (!strcmp(argv[1], "race")) {
        os_thread_t *threads[8];
        assert(lock_os_mutex(mutex) == OS_THREAD_OK);
        for (unsigned i = 0; i < 8; ++i)
            assert(create_os_thread(threads + i, race_exit, (void *)(uintptr_t)(i % 2 ? 7 : 0), 0)
                   == OS_THREAD_OK);
        signal_parent();
        assert(getchar() == 'x');
        start = true;
        broadcast_os_condition(changed);
        unlock_os_mutex(mutex);
        join_os_thread(threads[0], NULL);
        abort();
    }
    if (!strcmp(argv[1], "shared-budget")) {
        LOCK_STREAM(stdout);
        LOCK_STREAM(stderr);
        os_thread_t *thread;
        assert(create_os_thread(&thread, shared_exit, NULL, 0) == OS_THREAD_OK);
        /* Parent releases the first stream well into the one-second budget;
           stderr stays locked. A per-stream deadline would run too long. */
        assert(getchar() == 'x');
        UNLOCK_STREAM(stdout);
        process_exit_request(9);
    }
    if (!strcmp(argv[1], "lock-stdout") || !strcmp(argv[1], "lock-stderr")) {
        held = !strcmp(argv[1], "lock-stdout") ? stdout : stderr;
        os_thread_t *thread;
        assert(lock_os_mutex(mutex) == OS_THREAD_OK);
        assert(create_os_thread(&thread, hold_stream, NULL, 0) == OS_THREAD_OK);
        while (!ready)
            wait_os_condition(changed, mutex);
        unlock_os_mutex(mutex);
    } else {
        memset(payload, 'x', sizeof(payload));
        if (!strcmp(argv[1], "write-stdout") || !strcmp(argv[1], "write-both"))
            assert(FWRITE(payload, 1, sizeof(payload), stdout) == sizeof(payload));
        if (!strcmp(argv[1], "write-stderr") || !strcmp(argv[1], "write-both"))
            assert(FWRITE(payload, 1, sizeof(payload), stderr) == sizeof(payload));
    }
    signal_parent();
    assert(getchar() == 'x');
    process_exit_request(73);
}
