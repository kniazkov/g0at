/** @file native_test_thread.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Uses a stack reservation on Windows; winpthreads sets only the commit size.
 */
#include "native_test_thread.h"

#include "lib/windows_target.h"
#ifdef _WIN32
#    include <process.h>
#    include <windows.h>

typedef struct {
    void *(*callback)(void *);
    void *data;
} stack_task_t;

static unsigned __stdcall worker(void *data) {
    stack_task_t *task = data;
    task->callback(task->data);
    return 0;
}

bool run_on_small_stack(void *(*callback)(void *), void *data) {
    stack_task_t task = {callback, data};
    HANDLE thread = (HANDLE)
        _beginthreadex(NULL, 128 * 1024, worker, &task, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (!thread)
        return false;
    bool done = WaitForSingleObject(thread, INFINITE) == WAIT_OBJECT_0;
    CloseHandle(thread);
    return done;
}
#else
#    include <pthread.h>

bool run_on_small_stack(void *(*callback)(void *), void *data) {
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes))
        return false;
    pthread_t thread;
    int status = pthread_attr_setstacksize(&attributes, 128 * 1024);
    if (!status)
        status = pthread_create(&thread, &attributes, callback, data);
    pthread_attr_destroy(&attributes);
    return !status && !pthread_join(thread, NULL);
}
#endif
