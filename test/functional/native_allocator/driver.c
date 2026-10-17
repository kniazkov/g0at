/** @file driver.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Allocation lifetime, laziness, alignment and independent thread ownership.
 */
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static atomic_size_t live_blocks;
static atomic_size_t allocations;
static int fail_allocation;

static void *tracked_malloc(size_t size) {
    if (fail_allocation)
        return NULL;
    void *p = malloc(size);
    if (p) {
        atomic_fetch_add(&live_blocks, 1);
        atomic_fetch_add(&allocations, 1);
    }
    return p;
}

static void tracked_free(void *p) {
    if (p)
        atomic_fetch_sub(&live_blocks, 1);
    free(p);
}

#define malloc tracked_malloc
#define free tracked_free
#include <goat/native_allocator.h>
#undef malloc
#undef free
#ifdef _WIN32
#    include <windows.h>
#else
#    include <pthread.h>
#endif

static void exercise(void) {
    goat_native_allocator_state_t state = {0};
    goat_native_allocator_v2_t a = goat_native_allocator_init(&state);
    for (size_t i = 0; i < 1000; i++) {
        unsigned char *first = a.allocate(a.context, 71);
        void *middle = a.allocate(a.context, 0);
        void *last = a.allocate(a.context, sizeof(goat_native_alignment_t));
        assert(first && middle && last && first != middle && middle != last);
        assert((uintptr_t)first % _Alignof(goat_native_alignment_t) == 0);
        assert((uintptr_t)last % _Alignof(goat_native_alignment_t) == 0);
        memset(first, 0xA5, 71);
        a.deallocate(a.context, middle);
        assert(first[70] == 0xA5);
        a.deallocate(a.context, first);
        a.deallocate_all(a.context);
        assert(!state.first);
        a.deallocate_all(a.context);
        a.deallocate(a.context, NULL);
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
    goat_native_allocator_state_t state = {0};
    goat_native_allocator_v2_t a = goat_native_allocator_init(&state);
    assert(!state.first && !atomic_load(&allocations));
    a.deallocate_all(a.context);
    a.deallocate(a.context, NULL);
    assert(!atomic_load(&allocations));
    if (argc > 1) {
        fail_allocation = !strcmp(argv[1], "oom");
        a.allocate(a.context, fail_allocation ? 1 : SIZE_MAX);
        return 99;
    }
    goat_native_allocator_state_t other = {0};
    goat_native_allocator_v2_t b = goat_native_allocator_init(&other);
    unsigned char *keep = b.allocate(b.context, 8);
    memset(keep, 42, 8);
    a.allocate(a.context, 32);
    a.deallocate_all(a.context);
    assert(keep[7] == 42 && atomic_load(&live_blocks) == 1);
    b.deallocate_all(b.context);
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
    assert(!atomic_load(&live_blocks));
    return 0;
}
