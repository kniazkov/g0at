/** @file module_json_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Frozen Python metadata cases replayed against the production reader.
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"
#include "yyjson.h"

#include <stdlib.h>
#include <string.h>

static bool release_doc(void *pointer) {
    yyjson_doc_free(pointer);
    return true;
}

static bool release_malloc(void *pointer) {
    free(pointer);
    return true;
}

static yyjson_doc *parse(scenario_run_t *run, const void *data, size_t size) {
    yyjson_doc *doc = yyjson_read(data, size, 0);
    scenario_require(run, doc != NULL, "invalid JSON fixture or successful reader output");
    scenario_defer(run, release_doc, doc);
    return doc;
}

/* No floating-point conversion: adjacent int64 values above 2^53 must remain
 * different. String and key comparisons retain embedded NUL and Unicode bytes.
 * Bound recursive inspection and reject duplicate keys before deep comparison. */
static bool equal_json(yyjson_val *a, yyjson_val *b, unsigned depth) {
    if (!a || !b || depth > 128 || yyjson_get_type(a) != yyjson_get_type(b))
        return false;
    if (yyjson_is_arr(a)) {
        if (yyjson_arr_size(a) != yyjson_arr_size(b))
            return false;
        yyjson_arr_iter ai = yyjson_arr_iter_with(a), bi = yyjson_arr_iter_with(b);
        yyjson_val *value;
        while ((value = yyjson_arr_iter_next(&ai)))
            if (!equal_json(value, yyjson_arr_iter_next(&bi), depth + 1))
                return false;
        return true;
    }
    if (yyjson_is_obj(a)) {
        if (yyjson_obj_size(a) != yyjson_obj_size(b))
            return false;
        yyjson_obj_iter ai = yyjson_obj_iter_with(a);
        yyjson_val *key;
        while ((key = yyjson_obj_iter_next(&ai))) {
            size_t matches_a = 0, matches_b = 0;
            yyjson_val *other, *found = NULL;
            yyjson_obj_iter scan = yyjson_obj_iter_with(a);
            while ((other = yyjson_obj_iter_next(&scan)))
                if (yyjson_equals_strn(other, yyjson_get_str(key), yyjson_get_len(key)))
                    matches_a++;
            scan = yyjson_obj_iter_with(b);
            while ((other = yyjson_obj_iter_next(&scan)))
                if (yyjson_equals_strn(other, yyjson_get_str(key), yyjson_get_len(key))) {
                    matches_b++;
                    found = yyjson_obj_iter_get_val(other);
                }
            if (matches_a != 1 || matches_b != 1
                || !equal_json(yyjson_obj_iter_get_val(key), found, depth + 1))
                return false;
        }
        return true;
    }
    if (yyjson_is_num(a) && yyjson_get_subtype(a) != yyjson_get_subtype(b)) {
        /* JSON -0 and 0 normalize to the same integer; compare mixed signed
         * and unsigned values only after excluding negative signed values. */
        if (yyjson_is_sint(a) && yyjson_is_uint(b))
            return yyjson_get_sint(a) >= 0 && (uint64_t)yyjson_get_sint(a) == yyjson_get_uint(b);
        if (yyjson_is_uint(a) && yyjson_is_sint(b))
            return yyjson_get_sint(b) >= 0 && yyjson_get_uint(a) == (uint64_t)yyjson_get_sint(b);
        return false;
    }
    return yyjson_equals(a, b);
}

static unsigned hex_digit(scenario_run_t *run, unsigned char value) {
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    scenario_require(run, false, "invalid fixture inputHex digit");
    return 0;
}

static bool contains(const void *data, size_t size, const char *text, size_t length) {
    if (!length || length > size)
        return false;
    for (size_t i = 0; i <= size - length; ++i)
        if (!memcmp((const unsigned char *)data + i, text, length))
            return true;
    return false;
}

static void
check_output(scenario_run_t *run, yyjson_val *expected, scenario_command_result_t result) {
    yyjson_doc *actual = parse(run, result.out, result.out_size);
    if (!equal_json(expected, yyjson_doc_get_root(actual), 0)) {
        size_t size;
        char *text = yyjson_val_write(expected, YYJSON_WRITE_PRETTY, &size);
        scenario_require(run, text != NULL, "cannot serialize expected JSON diagnostic");
        scenario_defer(run, release_malloc, text);
        language_write(
            run,
            language_join(run, scenario_work_directory(scenario_run_context(run)), "expected.json"),
            text,
            size);
        scenario_require(run, false, "normalized JSON differs from frozen expectation");
    }
#ifndef _WIN32
    /* Windows trims trailing spaces in ordinary Win32 paths. As in the Python
     * test, the actual filesystem check applies only to POSIX; JSON preservation
     * itself is checked on all platforms by the frozen cases. */
    yyjson_val *main = yyjson_obj_get(yyjson_doc_get_root(actual), "main");
    if (yyjson_equals_str(main, " main.goat ")) {
        const char *directory = scenario_work_directory(scenario_run_context(run));
        language_write(run, language_join(run, directory, "main.goat"), "wrong", 5);
        language_write(run, language_join(run, directory, " main.goat "), "right", 5);
        size_t size;
        const char *value =
            language_read(run, language_join(run, directory, yyjson_get_str(main)), &size);
        scenario_require_bytes(run, "untrimmed source path", "right", 5, value, size, false);
    }
#endif
}

static void metadata_body(scenario_run_t *run, void *user) {
    const char *exe = user;
    size_t size;
    const char *data =
        language_read(run,
                      language_join(run,
                                    scenario_data_directory(scenario_run_context(run)),
                                    "module-json/cases.json"),
                      &size);
    yyjson_doc *doc = parse(run, data, size);
    yyjson_val *cases = yyjson_doc_get_root(doc);
    scenario_require(run,
                     yyjson_is_arr(cases) && yyjson_arr_size(cases) == 440,
                     "missing or truncated metadata case corpus");
    size_t index, count, accepted = 0, rejected = 0;
    yyjson_val *item;
    yyjson_arr_foreach(cases, index, count, item) {
        scenario_checkpoint(run);
        yyjson_val *kind = yyjson_obj_get(item, "kind");
        yyjson_val *hex = yyjson_obj_get(item, "inputHex");
        yyjson_val *status = yyjson_obj_get(item, "exitCode");
        yyjson_val *expected = yyjson_obj_get(item, "expected");
        yyjson_val *error = yyjson_obj_get(item, "error");
        bool gmod = yyjson_equals_str(kind, "gmod");
        scenario_require(run, gmod || yyjson_equals_str(kind, "module"), "invalid fixture kind");
        scenario_require(run,
                         yyjson_is_str(hex) && !(yyjson_get_len(hex) % 2),
                         "invalid fixture inputHex");
        scenario_require(run,
                         yyjson_is_uint(status) && yyjson_get_uint(status) <= 1,
                         "invalid fixture exitCode");
        bool valid = yyjson_get_uint(status) == 0;
        scenario_require(run,
                         valid ? yyjson_is_obj(expected) : !expected,
                         "invalid fixture expectation");
        scenario_require(run,
                         !error || (!valid && yyjson_is_str(error) && yyjson_get_len(error)),
                         "invalid fixture error diagnostic");
        size_t input_size = yyjson_get_len(hex) / 2;
        unsigned char *input = language_owned(run, ALLOC(input_size + 1));
        const char *source = yyjson_get_str(hex);
        for (size_t i = 0; i < input_size; ++i)
            input[i] = (unsigned char)((hex_digit(run, source[2 * i]) << 4)
                                       | hex_digit(run, source[2 * i + 1]));
        printf("Module JSON case %zu (%s, expected %u)\n",
               index + 1,
               gmod ? "gmod" : "module",
               valid ? 0u : 1u);
        const char *arguments[] = {exe, gmod ? "--gmod" : "--module", NULL};
        child_options_t options = {.executable = exe,
                                   .arguments = arguments,
                                   .stdin_data = input,
                                   .stdin_size = input_size};
        scenario_command_result_t result = scenario_command_capture(run, &options);
        scenario_require(run,
                         result.exit_code == (valid ? 0 : 1),
                         "metadata reader acceptance mismatch");
        if (valid) {
            check_output(run, expected, result);
            accepted++;
        } else {
            if (error)
                scenario_require(run,
                                 contains(result.err,
                                          result.err_size,
                                          yyjson_get_str(error),
                                          yyjson_get_len(error)),
                                 "missing expected metadata diagnostic");
            rejected++;
        }
    }
    scenario_require(run,
                     accepted == 93 && rejected == 347,
                     "metadata corpus coverage count mismatch");
    printf("[ ok ] Module JSON black-box checks passed: %zu (%zu accepted, %zu rejected)\n",
           count,
           accepted,
           rejected);
}

int main(int argc, char **argv) {
#ifdef _WIN32
    fs_error_t arguments_error = {0};
    if (!fs_arguments(&argc, &argv, &arguments_error)) {
        clear_fs_error(&arguments_error);
        return 1;
    }
#endif
    int result = 1;
    if (argc == 2 && !strcmp(argv[1], "--list")) {
        puts("native.module_json");
        result = 0;
    } else if (argc == 5 || argc == 6) {
        fs_error_t error = {0};
        char *exe = absolute_fs_path(argv[2], &error);
        clear_fs_error(&error);
        if (exe) {
            scenario_definition_t definition = {"native.module_json",
                                                180000,
                                                metadata_body,
                                                exe,
                                                NULL};
            scenario_options_t options = {.build_directory = argv[3], .data_directory = argv[4]};
            const char *selection[] = {argv[1], argc == 6 ? argv[5] : NULL};
            result = scenario_registry_main(&definition, 1, argc == 6 ? 2 : 1, selection, &options);
            FREE(exe);
        }
    } else
        fputs("usage: module_json_scenarios native.module_json HOST BUILD DATA [--keep-work]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
