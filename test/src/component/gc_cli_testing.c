/** @file gc_cli_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Ordinary CLI and persistent REPL policy admission and heap bounds.
 */
#ifdef NDEBUG
#    undef NDEBUG
#endif
#include "cli/launcher.h"
#include "lib/files/io.h"
#include "lib/process/cleanup_service.h"
#include "model/module_files.h"
#include "model/process.h"
#include "model/thread.h"
#include "resources/messages.h"
#include "vm/gc_stats.h"
#include "vm/vm.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
extern size_t gc_cli_passes;
static unsigned runs;
static bool native;

static void begin(process_t *p, gc_stats_t *stats) {
    assert(p->gc_policy.enabled && !p->gc_force_boundaries);
    assert(p->gc_policy.minimum_bytes == GC_POLICY_DEFAULT_MIN_BYTES);
    assert(configure_gc_statistics(p, stats));
}

static void end(process_t *p) {
    assert(!p->execution && !p->gc_session && !p->active_threads);
    assert(!p->gc_policy.pending && !p->gc_policy.allocated_bytes);
    assert(configure_gc_statistics(p, NULL));
    ++runs;
}

int gc_checked_run(process_t *p, bytecode_t *code) {
    gc_stats_t stats;
    begin(p, &stats);
    int result = run(p, code);
    end(p);
    return result;
}

int gc_checked_run_module_file(process_t *p, module_file_t *file) {
    gc_stats_t stats;
    begin(p, &stats);
    int result = run_module_file(p, file);
    if (native) {
        size_t successes = 0;
        thread_t *t = p->main_thread;
        do {
            successes += t->native_successes;
            t = t->next;
        } while (t != p->main_thread);
        assert(successes > 0);
    }
    end(p);
    return result;
}

int main(int argc, char **argv) {
    int cleanup_code;
    if (cleanup_service_main(argc, argv, &cleanup_code))
        return cleanup_code;
    assert(argc == 3 || argc == 4);
    init_unicode_locale();
    init_messages();
    assert(init_io());
    native = !strcmp(argv[1], "native");
    bool repl = !strcmp(argv[1], "repl");
    if (repl)
        assert(freopen(argv[2], "r", stdin));
    char *args[] = {argv[0],
                    repl ? "--interactive" : argv[2],
                    "--lang",
                    "en",
                    "--native",
                    "required",
                    "--cc",
                    argc == 4 ? argv[3] : NULL,
                    NULL};
    options_t *options = parse_options(native ? 8 : 4, args);
    assert(options);
    assert(go(options) == 0);
    destroy_options(options);
    assert(runs >= (repl ? 4u : 1u));
    assert(gc_cli_passes > 10);
    printf("GC CLI runs=%u in-run collections=%zu\n", runs, gc_cli_passes);
    return 0;
}
