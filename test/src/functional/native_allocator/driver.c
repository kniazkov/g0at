/** @file driver.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Native services use interpreter accounting, alignment and per-thread lifetimes.
 */
#include "lib/memory/alignment.h"
#include "lib/memory/allocate.h"
#include "model/native_services.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>
#ifdef _WIN32
#    include <windows.h>
#else
#    include <pthread.h>
#endif

static void exercise(void) {
    native_memory_t state = {0};
    goat_native_context_v2_t a = init_native_context(&state);
    for (size_t i = 0; i < 1000; i++) {
        unsigned char *first = goat_native_allocate(&a, 71);
        void *middle = goat_native_allocate(&a, 0);
        void *last = goat_native_allocate(&a, sizeof(memory_alignment_t));
        assert(first && middle && last && first != middle && middle != last);
        assert((uintptr_t)first % _Alignof(memory_alignment_t) == 0);
        assert((uintptr_t)last % _Alignof(memory_alignment_t) == 0);
        memset(first, 0xA5, 71);
        goat_native_deallocate(&a, middle);
        assert(first[70] == 0xA5);
        goat_native_deallocate(&a, first);
        goat_native_deallocate_all(&a);
        assert(!state.first);
        goat_native_deallocate_all(&a);
        goat_native_deallocate(&a, NULL);
        /* Mix ordinary interpreter allocations with native service allocations. */
        void *ordinary = ALLOC(27);
        FREE(ordinary);
    }
}
#ifdef _WIN32
static DWORD WINAPI worker(void *unused) {
#else
static void *worker(void *unused) {
#endif
    (void)unused;
    exercise();
    return 0;
}

int main(int argc, char **argv) {
    size_t baseline = get_allocated_memory_size();
    native_memory_t state = {0};
    goat_native_context_v2_t a = init_native_context(&state);
    assert(!state.first && get_allocated_memory_size() == baseline);
    goat_native_deallocate_all(&a);
    goat_native_deallocate(&a, NULL);
    assert(get_allocated_memory_size() == baseline);
    if (argc > 1) {
        if (!strcmp(argv[1], "overflow"))
            goat_native_allocate(&a, SIZE_MAX);
        else if (!strcmp(argv[1], "corrupt")) {
            unsigned char *p = goat_native_allocate(&a, 8);
            p[8] = 0; /* Must hit the standard MEMORY_DEBUG trailer. */
            goat_native_deallocate_all(&a);
        } else {
            goat_native_allocate(&a, 42);
            print_list_of_memory_blocks();
            goat_native_deallocate_all(&a);
            return 0;
        }
        return 99;
    }
    unsigned char *first = goat_native_allocate(&a, 41);
    size_t one = get_allocated_memory_size();
    assert(one >= baseline + 41);
    void *second = goat_native_allocate(&a, 53);
    assert(get_allocated_memory_size() >= one + 53);
    goat_native_deallocate(&a, second);
    assert(get_allocated_memory_size() == one);
    native_memory_t other = {0};
    goat_native_context_v2_t b = init_native_context(&other);
    assert(a.services == b.services && a.state != b.state);
    memset(first, 42, 41);
    goat_native_allocate(&b, 32);
    goat_native_deallocate_all(&b);
    assert(first[40] == 42 && get_allocated_memory_size() == one);
    goat_native_deallocate_all(&a);
    assert(get_allocated_memory_size() == baseline);
#ifdef _WIN32
    HANDLE threads[4];
    for (int i = 0; i < 4; i++) {
        threads[i] = CreateThread(NULL, 0, worker, NULL, 0, NULL);
        assert(threads[i]);
    }
    for (int i = 0; i < 4; i++) {
        assert(WaitForSingleObject(threads[i], INFINITE) == WAIT_OBJECT_0);
        CloseHandle(threads[i]);
    }
#else
    pthread_t threads[4];
    for (int i = 0; i < 4; i++)
        assert(!pthread_create(&threads[i], NULL, worker, NULL));
    for (int i = 0; i < 4; i++)
        assert(!pthread_join(threads[i], NULL));
#endif
    assert(get_allocated_memory_size() == baseline);
    return 0;
}
