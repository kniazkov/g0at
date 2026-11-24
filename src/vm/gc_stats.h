/** @file gc_stats.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Opt-in per-process snapshots of existing tracing passes and pool trims.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct process_t process_t;

/** @brief Managed payload measured under the model lock. */
typedef struct {
    size_t objects;         /**< Number of active managed objects. */
    size_t active_bytes;    /**< Direct owned active-object payload, not shared resources. */
    size_t pooled_bytes;    /**< Retained inactive shell payload. */
    size_t auxiliary_bytes; /**< Root registration and standalone string-cache tables. */
} gc_memory_sample_t;

/** @brief Latest tracing pass; a trim never modifies this record. */
typedef struct {
    gc_memory_sample_t before, after;
    size_t vertices; /**< Distinct managed objects marked, including scalar leaves. */
    size_t edges;    /**< Immediate edge callbacks from marked graph objects, including weak/static
                        edges. */
    size_t freed_bytes; /**< Decrease in accounted active + pooled payload. */
    uint64_t pause_ns;  /**< Instrumented duration; includes optional byte-accounting overhead. */
    bool clock_valid;   /**< False when the monotonic clock was unavailable. */
    bool run_end;       /**< False for an explicit host collection. */
} gc_pass_sample_t;

/** @brief Caller-owned telemetry storage; bind to at most one process at a time. */
typedef struct gc_stats_t {
    gc_pass_sample_t last;
    uint64_t collections;
    uint64_t explicit_collections;
    uint64_t run_end_collections;
    uint64_t max_pause_ns;
    uint64_t trim_calls;
    uint64_t trimmed_bytes;
} gc_stats_t;

/**
 * @brief Attach and reset caller-owned storage, or disable with NULL.
 * Caller must keep storage alive until detaching or destroying the process.
 * Requires the execution lock or exclusive process ownership; read it under
 * the same rules. During collection, last-owner release drain or teardown, this
 * function returns false. The runtime never prints or allocates for these snapshots.
 */
bool configure_gc_statistics(process_t *proc, gc_stats_t *storage);

/** @brief Internal per-process trim observation, excluding shutdown. */
void gc_statistics_note_trim(gc_stats_t *stats, size_t bytes);

/** @brief Internal pass accounting; called only for an already existing collection. */
void gc_statistics_record(gc_stats_t *stats, gc_pass_sample_t sample);

/** @brief Internal monotonic nanosecond timestamp, false if unavailable. */
bool gc_statistics_now(uint64_t *value);

/** @brief Saturating owned-payload sample under stable model ownership; no collection. */
gc_memory_sample_t gc_measure_memory(const process_t *process);
