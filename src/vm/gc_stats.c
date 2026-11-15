/** @file gc_stats.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Optional collection timing and counters.
 */
#ifndef _WIN32
#    define _POSIX_C_SOURCE 200809L
#endif
#include "gc_stats.h"

#include "model/process.h"
#ifdef _WIN32
#    include <windows.h>
#else
#    include <time.h>
#endif
bool gc_statistics_now(uint64_t *value) {
    if (!value)
        return false;
#ifdef _WIN32
    LARGE_INTEGER counter, frequency;
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0
        || !QueryPerformanceCounter(&counter) || counter.QuadPart < 0)
        return false;
    uint64_t ticks = (uint64_t)counter.QuadPart, rate = (uint64_t)frequency.QuadPart;
    uint64_t seconds = ticks / rate, remainder = ticks % rate;
    if (seconds > UINT64_MAX / 1000000000ULL || remainder > UINT64_MAX / 1000000000ULL)
        return false;
    uint64_t fraction = remainder * 1000000000ULL / rate;
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) || now.tv_sec < 0 || now.tv_nsec < 0
        || now.tv_nsec >= 1000000000L)
        return false;
    uint64_t seconds = (uint64_t)now.tv_sec, fraction = (uint64_t)now.tv_nsec;
    if (seconds > UINT64_MAX / 1000000000ULL)
        return false;
#endif
    uint64_t whole = seconds * 1000000000ULL;
    if (fraction > UINT64_MAX - whole)
        return false;
    *value = whole + fraction;
    return true;
}

bool configure_gc_statistics(process_t *proc, gc_stats_t *storage) {
    if (!proc || proc->gc_collecting || proc->destroying || proc->draining_releases)
        return false;
    if (storage)
        *storage = (gc_stats_t){0};
    proc->gc_statistics = storage;
    return true;
}

static uint64_t saturating_add(uint64_t total, uint64_t value) {
    return value > UINT64_MAX - total ? UINT64_MAX : total + value;
}

void gc_statistics_note_trim(gc_stats_t *stats, size_t bytes) {
    stats->trim_calls = saturating_add(stats->trim_calls, 1);
    stats->trimmed_bytes = saturating_add(stats->trimmed_bytes, bytes);
}

void gc_statistics_record(gc_stats_t *stats, gc_pass_sample_t sample) {
    stats->last = sample;
    stats->collections = saturating_add(stats->collections, 1);
    uint64_t *cause = sample.run_end ? &stats->run_end_collections : &stats->explicit_collections;
    *cause = saturating_add(*cause, 1);
    if (sample.clock_valid && sample.pause_ns > stats->max_pause_ns)
        stats->max_pause_ns = sample.pause_ns;
}
