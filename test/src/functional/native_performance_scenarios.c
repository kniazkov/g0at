/** @file native_performance_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Precompiled call-tree benchmark, independent oracle and rejection controls (#387).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/language_execution.h"
#include "test/support/native_runner_policy.h"
#include "test/support/registry.h"
#include "yyjson.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(x) scenario_require(r, (x), #x)
#define ARGS(...) ((const char *const[]){__VA_ARGS__, NULL})
#define DEPTH 12
#define TREES 1024
#define SAMPLES 5
#define CALLS_PER_TREE ((1U << DEPTH) - 1)

typedef struct {
    const char *goat;
    unsigned control;
} config_t;

typedef struct {
    child_options_t options;
    scenario_command_result_t result;
    uint64_t elapsed;
} command_t;

static const char *string(scenario_run_t *r, const char *format, ...) {
    va_list a, b;
    va_start(a, format);
    va_copy(b, a);
    int n = vsnprintf(NULL, 0, format, b);
    va_end(b);
    REQUIRE(n >= 0);
    char *out = language_owned(r, ALLOC((size_t)n + 1));
    vsnprintf(out, (size_t)n + 1, format, a);
    va_end(a);
    return out;
}

/* Fixed bounds keep the independent binomial sum below 2^40 on i686 as well. */
static uint64_t expected_result(void) {
    uint64_t sum = 0;
    for (unsigned start = 0; start < TREES; start++) {
        uint64_t choose = 1;
        for (unsigned twos = 0; twos < DEPTH; twos++) {
            uint64_t n = start + DEPTH - 1 + twos;
            sum += choose * (n * n + 3 * n + 7);
            if (twos + 1 < DEPTH)
                choose = choose * (DEPTH - 1 - twos) / (twos + 1);
        }
    }
    return sum;
}

static void
append(scenario_run_t *r, char *out, size_t capacity, size_t *size, const char *format, ...) {
    REQUIRE(*size < capacity);
    va_list args;
    va_start(args, format);
    int n = vsnprintf(out + *size, capacity - *size, format, args);
    va_end(args);
    REQUIRE(n >= 0 && (size_t)n < capacity - *size);
    *size += (size_t)n;
}

static const char *program(scenario_run_t *r, size_t *size) {
    size_t capacity = 65536;
    char *out = language_owned(r, ALLOC(capacity));
    *size = 0;
    append(r, out, capacity, size, "const leaf = func(n) { return n * n + 3 * n + 7; }\n");
    for (unsigned level = 1; level < DEPTH; level++) {
        const char *previous = level == 1 ? "leaf" : string(r, "level%u", level - 1);
        append(r,
               out,
               capacity,
               size,
               "const level%u = func(n) { return %s(n + 1) + %s(n + 2); }\n",
               level,
               previous,
               previous);
    }
    append(r, out, capacity, size, "var total = 0\n");
    for (unsigned index = 0; index < TREES; index++)
        append(r, out, capacity, size, "total = total + level%u(%u)\n", DEPTH - 1, index);
    append(r, out, capacity, size, "print(total); print(\"\\n\");\n");
    return out;
}

static const char *checked_program(scenario_run_t *r, size_t *size) {
    const char *source = program(r, size);
    size_t frozen_size;
    const char *frozen =
        language_read(r,
                      language_join(r,
                                    scenario_data_directory(scenario_run_context(r)),
                                    "native-performance/call_tree.goat"),
                      &frozen_size);
    scenario_require_bytes(r, "original call tree", frozen, frozen_size, source, *size, true);
    REQUIRE(expected_result() == UINT64_C(771246522368));
    REQUIRE(*size == 30417);
    return source;
}

/* Match Path.write_text on Windows; the frozen corpus always remains read-only. */
static void write_text(scenario_run_t *r, const char *path, const char *data, size_t size) {
#ifdef _WIN32
    char *out = language_owned(r, ALLOC(size * 2 + 1));
    size_t n = 0;
    for (size_t i = 0; i < size; i++) {
        if (data[i] == '\n')
            out[n++] = '\r';
        out[n++] = data[i];
    }
    language_write(r, path, out, n);
#else
    language_write(r, path, data, size);
#endif
}

static void command_body(scenario_run_t *r, void *user) {
    command_t *command = user;
    command->result = scenario_command_capture_timed(r, &command->options, &command->elapsed);
}

static command_t
command(scenario_run_t *r, const char *const *args, const char *const *environment) {
    command_t result = {.options = {.executable = args[0],
                                    .arguments = args,
                                    .environment = environment,
                                    .empty_stdin = true}};
    /* Every original subprocess.run used 180 seconds, in addition to the shared budget. */
    scenario_with_timeout(r, 180000, command_body, &result);
    return result;
}

static bool execution_ok(scenario_command_result_t result, const char *expected) {
    return result.exit_code == 0 && result.err_size == 0
           && nr_output_equal(result.out, result.out_size, expected, strlen(expected));
}

static unsigned mode_at(unsigned sample, unsigned position) {
    return (sample + position) % 2;
}

static uint64_t median(const uint64_t samples[SAMPLES]) {
    uint64_t sorted[SAMPLES];
    memcpy(sorted, samples, sizeof(sorted));
    for (unsigned i = 1; i < SAMPLES; i++) {
        uint64_t next = sorted[i];
        unsigned j = i;
        while (j && sorted[j - 1] > next) {
            sorted[j] = sorted[j - 1];
            j--;
        }
        sorted[j] = next;
    }
    return sorted[SAMPLES / 2];
}

static bool timings_ok(const char *bytes, size_t size) {
    yyjson_doc *doc = yyjson_read(bytes, size, 0);
    yyjson_val *root = yyjson_doc_get_root(doc), *samples = yyjson_obj_get(root, "samples_ns");
    bool ok = yyjson_is_obj(root) && yyjson_is_obj(samples);
    const char *keys[] = {"depth", "trees", "calls_per_tree", "total_calls", "expected_result"};
    uint64_t values[] = {DEPTH, TREES, CALLS_PER_TREE, TREES * CALLS_PER_TREE, expected_result()};
    for (unsigned i = 0; i < 5; i++) {
        yyjson_val *v = yyjson_obj_get(root, keys[i]);
        ok = ok && yyjson_is_uint(v) && yyjson_get_uint(v) == values[i];
    }
    uint64_t medians[2] = {0};
    const char *names[] = {"vm", "native"}, *median_keys[] = {"vm_median_ns", "native_median_ns"};
    for (unsigned mode = 0; mode < 2; mode++) {
        yyjson_val *array = yyjson_obj_get(samples, names[mode]);
        uint64_t times[SAMPLES] = {0};
        ok = ok && yyjson_is_arr(array) && yyjson_arr_size(array) == SAMPLES;
        for (unsigned i = 0; i < SAMPLES; i++) {
            yyjson_val *v = yyjson_arr_get(array, i);
            times[i] = yyjson_get_uint(v);
            ok = ok && yyjson_is_uint(v) && times[i] > 0;
        }
        medians[mode] = median(times);
        yyjson_val *v = yyjson_obj_get(root, median_keys[mode]);
        ok = ok && yyjson_is_uint(v) && yyjson_get_uint(v) == medians[mode];
    }
    yyjson_val *passed = yyjson_obj_get(root, "passed"), *speedup = yyjson_obj_get(root, "speedup");
    ok = ok && medians[1] > 0 && yyjson_is_bool(passed)
         && yyjson_get_bool(passed) == (medians[1] < medians[0]) && yyjson_is_num(speedup)
         && yyjson_get_num(speedup) == (double)medians[0] / (double)medians[1];
    yyjson_doc_free(doc);
    return ok;
}

static const char *timings(scenario_run_t *r, uint64_t times[2][SAMPLES]) {
    uint64_t vm = median(times[0]), native = median(times[1]);
    REQUIRE(vm > 0 && native > 0);
    char *out = language_owned(r, ALLOC(4096));
    size_t size = 0;
    append(r,
           out,
           4096,
           &size,
           "{\n  \"depth\": %u, \"trees\": %u, \"calls_per_tree\": %u,\n"
           "  \"total_calls\": %u, \"expected_result\": %" PRIu64 ",\n  \"samples_ns\": {\n",
           DEPTH,
           TREES,
           CALLS_PER_TREE,
           TREES * CALLS_PER_TREE,
           expected_result());
    for (unsigned mode = 0; mode < 2; mode++) {
        append(r, out, 4096, &size, "    \"%s\": [", mode ? "native" : "vm");
        for (unsigned i = 0; i < SAMPLES; i++)
            append(r, out, 4096, &size, "%s%" PRIu64, i ? ", " : "", times[mode][i]);
        append(r, out, 4096, &size, "]%s\n", mode ? "" : ",");
    }
    append(r,
           out,
           4096,
           &size,
           "  },\n  \"vm_median_ns\": %" PRIu64 ", \"native_median_ns\": %" PRIu64 ",\n"
           "  \"speedup\": %.17g, \"passed\": %s\n}\n",
           vm,
           native,
           (double)vm / (double)native,
           native < vm ? "true" : "false");
    REQUIRE(timings_ok(out, size));
    return out;
}

static uint64_t run_mode(scenario_run_t *r,
                         config_t *config,
                         const char *work,
                         const char *const *environment,
                         unsigned mode,
                         const char *label) {
    const char *name = mode ? "native" : "vm", *policy = mode ? "required" : "off";
    const char *report = language_join(r, work, string(r, "%s-%s.report", name, label));
    const char *binary = language_join(r, work, string(r, "%s.gbin", name));
    command_t result = command(r,
                               ARGS(config->goat,
                                    "--lang",
                                    "en",
                                    "--run",
                                    "--native",
                                    policy,
                                    "--save-native",
                                    report,
                                    binary),
                               environment);
    const char *expected = string(r, "%" PRIu64 "\n", expected_result());
    scenario_require_bytes(r,
                           "call-tree result",
                           expected,
                           strlen(expected),
                           result.result.out,
                           result.result.out_size,
                           true);
    REQUIRE(execution_ok(result.result, expected));
    size_t size;
    const char *bytes = language_read(r, report, &size);
    REQUIRE(nr_check_performance_report(bytes, size, mode != 0, TREES));
    REQUIRE(result.elapsed > 0);
    return result.elapsed;
}

static void benchmark(scenario_run_t *r, void *user) {
    config_t *config = user;
    const char *work = scenario_work_directory(scenario_run_context(r));
    size_t size;
    const char *source = checked_program(r, &size);
    for (unsigned mode = 0; mode < 2; mode++) {
        const char *path = language_join(r, work, mode ? "native.goat" : "vm.goat");
        write_text(r, path, source, size);
        command_t result = command(r,
                                   ARGS(config->goat,
                                        "--lang",
                                        "en",
                                        "--compile",
                                        "--native",
                                        mode ? "required" : "off",
                                        path),
                                   NULL);
        REQUIRE(execution_ok(result.result, ""));
    }
    for (unsigned mode = 0; mode < 2; mode++) {
        const char *path = language_join(r, work, mode ? "native.goat" : "vm.goat");
        language_remove(r, path);
        REQUIRE(!language_exists(r, path));
    }
    write_text(r, language_join(r, work, "call_tree.goat"), source, size);
    const char *const *environment =
        language_environment_override(r, "CC", "goat-no-such-compiler");
    run_mode(r, config, work, environment, 0, "warmup");
    run_mode(r, config, work, environment, 1, "warmup");
    uint64_t times[2][SAMPLES] = {{0}};
    for (unsigned sample = 0; sample < SAMPLES; sample++)
        for (unsigned position = 0; position < 2; position++) {
            unsigned mode = mode_at(sample, position);
            times[mode][sample] =
                run_mode(r, config, work, environment, mode, string(r, "%u", sample + 1));
        }
    const char *report = timings(r, times);
    const char *path = language_join(r, work, "timings.json");
    write_text(r, path, report, strlen(report));
    size_t persisted_size;
    const char *persisted = language_read(r, path, &persisted_size);
    REQUIRE(timings_ok(persisted, persisted_size));
    uint64_t vm = median(times[0]), native = median(times[1]);
    printf("VM median: %.6f s; native median: %.6f s; speedup: %.2fx\n",
           vm / 1e9,
           native / 1e9,
           (double)vm / (double)native);
    /* The report survives this failure, exactly as in the original. */
    scenario_require(r, native < vm, "native execution is not faster than VM execution");
}

static void controls(scenario_run_t *r, void *user) {
    config_t *config = user;
    if (config->control == 0) {
        const char *expected = "771246522368\n";
        scenario_command_result_t result = {.out = expected, .out_size = strlen(expected)};
        REQUIRE(execution_ok(result, expected));
        result.out = "771246522368\r\n";
        result.out_size++;
        REQUIRE(execution_ok(result, expected));
        result.out = "771246522369\n";
        result.out_size = strlen(expected);
        REQUIRE(!execution_ok(result, expected));
        result.out = expected;
        result.exit_code = 1;
        REQUIRE(!execution_ok(result, expected));
        result.exit_code = 0;
        result.err_size = 1;
        REQUIRE(!execution_ok(result, expected));
        result.err_size = 0;
        result.out_size--;
        REQUIRE(!execution_ok(result, expected));
        REQUIRE(
            !nr_output_equal("771246522368\n\0", strlen(expected) + 1, expected, strlen(expected)));
        result = (scenario_command_result_t){.out = "", .out_size = 0};
        REQUIRE(execution_ok(result, ""));
        result.out = "unexpected compilation output";
        result.out_size = strlen(result.out);
        REQUIRE(!execution_ok(result, ""));
    } else if (config->control == 1) {
        const char *good[] = {"preparation=disabled\nattempts=0\nsucceeded=0\nretries=0\n",
                              "preparation=ready\nattempts=1024\nsucceeded=1024\nretries=0\n"};
        for (unsigned mode = 0; mode < 2; mode++) {
            REQUIRE(nr_check_performance_report(good[mode], strlen(good[mode]), mode != 0, TREES));
            REQUIRE(!nr_check_performance_report(good[mode], strlen(good[mode]), mode == 0, TREES));
            const char *fields[] = {"preparation", "attempts", "succeeded", "retries"};
            for (unsigned i = 0; i < 4; i++) {
                const char *bad = string(r, "%s%s=wrong\n", good[mode], fields[i]);
                REQUIRE(!nr_check_performance_report(bad, strlen(bad), mode != 0, TREES));
                char *missing = language_owned(r, ALLOC(strlen(good[mode]) + 1));
                strcpy(missing, good[mode]);
                char *field = strstr(missing, fields[i]);
                REQUIRE(field != NULL);
                *field = 'X';
                REQUIRE(!nr_check_performance_report(missing, strlen(missing), mode != 0, TREES));
                bad = string(r, "%s\n", good[mode]);
                REQUIRE(!nr_check_performance_report(bad, strlen(bad), mode != 0, TREES));
            }
        }
        const char *accepted =
            "unknown=x=y\rpreparation=wrong\rpreparation=ready\r\nattempts=1024\v"
            "succeeded=1024\x1c"
            "retries=0\xe2\x80\xa8";
        REQUIRE(nr_check_performance_report(accepted, strlen(accepted), true, TREES));
        const char *bad[] = {"",
                             "preparation=ready\nattempts=1024\nsucceeded=1024\n",
                             "preparation=ready\nattempts=1024\nsucceeded=1024\nretries=0\nbroken",
                             "preparation=ready\nattempts=1024\nsucceeded=1024\nretries=0\n\xff"};
        for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
            REQUIRE(!nr_check_performance_report(bad[i], strlen(bad[i]), true, TREES));
    } else if (config->control == 2) {
        uint64_t samples[2][SAMPLES] = {{9, 5, 1, 7, 3}, {4, 2, 5, 1, 3}};
        REQUIRE(median(samples[0]) == 5 && median(samples[1]) == 3);
        const char *report = timings(r, samples);
        REQUIRE(timings_ok(report, strlen(report)));
        for (unsigned i = 0; i < SAMPLES; i++)
            REQUIRE(mode_at(i, 0) == i % 2 && mode_at(i, 1) == 1 - i % 2);
        const char *keys[] = {"\"vm_median_ns\": 5",
                              "\"native_median_ns\": 3",
                              "\"depth\": 12",
                              "\"passed\": true",
                              "\"speedup\":"};
        for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
            char *corrupt = language_owned(r, ALLOC(strlen(report) + 1));
            strcpy(corrupt, report);
            char *at = strstr(corrupt, keys[i]);
            REQUIRE(at != NULL);
            at[1] = 'X';
            REQUIRE(!timings_ok(corrupt, strlen(corrupt)));
        }
        /* Equal and slower native are correctly serialized as failed, not discarded. */
        memcpy(samples[1], samples[0], sizeof(samples[0]));
        report = timings(r, samples);
        REQUIRE(strstr(report, "\"passed\": false") != NULL);
        samples[1][0] = samples[1][1] = samples[1][2] = 100;
        report = timings(r, samples);
        REQUIRE(strstr(report, "\"passed\": false") != NULL);
    } else {
        size_t size;
        const char *source = checked_program(r, &size);
        char *corrupt = language_owned(r, ALLOC(size));
        memcpy(corrupt, source, size);
        corrupt[0] ^= 1;
        REQUIRE(!nr_output_equal(corrupt, size, source, size));
        REQUIRE(!nr_output_equal(source, size - 1, source, size));
    }
}

int main(int argc, char **argv) {
#ifdef _WIN32
    if (!fs_arguments(&argc, &argv, NULL))
        return 1;
#endif
    int result = 1;
    if (argc == 5 || (argc == 6 && scenario_keep_work_argument(argv[5]))) {
        fs_error_t error = {0};
        char *goat = absolute_fs_path(argv[2], &error);
        clear_fs_error(&error);
        if (goat) {
            config_t configs[5] = {{goat, 0}, {goat, 1}, {goat, 2}, {goat, 3}, {goat, 0}};
            scenario_definition_t defs[] = {
                {"integration.native_performance.control-output", 30000, controls, configs, NULL},
                {"integration.native_performance.control-report",
                 30000,
                 controls,
                 configs + 1,
                 NULL},
                {"integration.native_performance.control-timings",
                 30000,
                 controls,
                 configs + 2,
                 NULL},
                {"integration.native_performance.corpus", 30000, controls, configs + 3, NULL},
                {"integration.native_performance.benchmark",
                 2520000,
                 benchmark,
                 configs + 4,
                 NULL}};
            scenario_options_t options = {.build_directory = argv[3], .data_directory = argv[4]};
            const char *selection[] = {argv[1], argc == 6 ? argv[5] : NULL};
            result = scenario_registry_main(defs,
                                            sizeof(defs) / sizeof(defs[0]),
                                            argc == 6 ? 2 : 1,
                                            selection,
                                            &options);
        }
        FREE(goat);
    } else
        fputs("usage: native_performance_scenarios ID|--list GOAT BUILD DATA [--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
