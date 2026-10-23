/**
 * @file unit_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief A program for running unit tests.
 */

#include "lib/allocate.h"
#include "lib/file_system.h"
#include "lib/string_ext.h"
#include "unit_test/test_c_emission.h"
#include "unit_test/test_file_system.h"
#include "unit_test/test_list.h"
#include "unit_test/test_native_call.h"
#include "unit_test/test_native_pipeline.h"
#include "unit_test/test_os_thread.h"
#include "unit_test/test_output.h"
#include "unit_test/test_stack_underflow.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Executes the registered unit tests and reports the results.
 * @return `true` if all tests passed, `false` if any test failed.
 */
static bool unit_testing() {
    test_output_start("unit");
    int passed = 0;
    int failed = 0;
    int count = get_number_of_tests();
    const test_description_t *tests = get_tests();
    for (int i = 0; i < count; i++) {
        size_t memory_size = get_allocated_memory_size();
        bool result = tests[i].test();
        if (result) {
            size_t diff = get_allocated_memory_size() - memory_size;
            if (diff > 0) {
                FPRINTF(stderr, "Test '%s' leaked %zu bytes\n", tests[i].name, diff);
                result = false;
            }
        }
        test_output_case(result, "%s", tests[i].name);
        if (result)
            passed++;
        else
            failed++;
    }
    test_output_summary("Unit", passed, failed);
    return failed == 0;
}

/** @brief Entry point. */
int main(int argc, char **argv) {
    init_unicode_locale();
    if (argc == 2 && !strcmp(argv[1], "--file-system-tests"))
        return run_file_system_tests() ? EXIT_SUCCESS : EXIT_FAILURE;
    if (argc == 2 && !strcmp(argv[1], "--os-thread-tests"))
        return run_os_thread_tests() ? EXIT_SUCCESS : EXIT_FAILURE;
    if (argc == 6 && !strcmp(argv[1], "--native-pipeline-tests"))
        return test_native_pipeline(argv[2], argv[3], argv[4], argv[5]) ? EXIT_SUCCESS
                                                                        : EXIT_FAILURE;
    if (argc == 5 && !strcmp(argv[1], "--native-call-tests"))
        return test_native_calls(argv[2], argv[3], argv[4]) ? EXIT_SUCCESS : EXIT_FAILURE;
    if (argc == 3 && !strcmp(argv[1], "--emit-c-tests"))
        return write_c_emission_tests(argv[2]) ? EXIT_SUCCESS : EXIT_FAILURE;
    if (argc == 3 && !strcmp(argv[1], "--stack-underflow-case")) {
        run_stack_underflow_case(atoi(argv[2]));
        return 0;
    }
    bool success = unit_testing();
    if (success)
        success = test_stack_underflow(argv[0]);
    if (!success) {
        exit(EXIT_FAILURE);
    }
    return 0;
}
