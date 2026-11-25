/** @file gc_cli_stats.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Observe every real pass without requesting a collection.
 */
#define gc_statistics_record gc_original_statistics_record
#include "vm/gc_stats.c"
#undef gc_statistics_record
#ifdef NDEBUG
#    undef NDEBUG
#endif
#include <assert.h>
size_t gc_cli_passes;

void gc_statistics_record(gc_stats_t *stats, gc_pass_sample_t sample) {
    gc_original_statistics_record(stats, sample);
    if (!sample.run_end) {
        ++gc_cli_passes;
        assert(sample.before.active_bytes < 1024 * 1024);
        assert(sample.after.active_bytes < 256 * 1024);
        assert(sample.after.objects < 2048);
    }
}
