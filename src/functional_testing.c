/**
 * @file functional_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief A program for performing functional tests on the project's output.
 */

#include "test/test_output.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Trims leading and trailing whitespace characters from a string. */
static char *trim(char *s) {
    while (isspace((unsigned char)*s))
        s++;
    size_t length = strlen(s);
    while (length && isspace((unsigned char)s[length - 1]))
        length--;
    s[length] = '\0';
    return s;
}

/** @brief Returns the correct path separator based on the operating system. */
static const char path_separator() {
#ifdef _WIN32
    return '\\';
#else
    return '/';
#endif
}

/** @brief Replaces all path separators in a given path with the platform-specific separator. */
static void fix_path_separator(char *path) {
    while (*path) {
        if (*path == '\\' || *path == '/') {
            *path = path_separator();
        }
        path++;
    }
}

/** @brief Compares the contents of two files, ignoring carriage return characters. */
static int compare_files(FILE *actual, FILE *expected) {
    while (!feof(actual)) {
        int a = fgetc(actual);
        while (a == '\r')
            a = fgetc(actual);

        int e = fgetc(expected);
        while (e == '\r')
            e = fgetc(expected);

        if (a != e)
            return 0;
    }
    return feof(expected);
}

/** @brief Calculates the size of a file in bytes. */
static int get_file_size(FILE *file) {
    fseek(file, 0, SEEK_END);
    int result = ftell(file);
    rewind(file);
    return result;
}

/** @brief Executes a test by running the project's binary and comparing its output with expected
 * results. */
int do_test(char *interpreter, char *test_name, const char *optimization, const char *native) {
    int result = 0;

    char cmd[1024], path_actual_output[256], path_expected_output[256], path_actual_error[256],
        path_expected_error[256], path_input[256];
    snprintf(path_actual_output,
             256,
             "%s%cactual_output_%s_%s.txt",
             test_name,
             path_separator(),
             optimization,
             native);
    snprintf(path_expected_output, 256, "%s%cexpected_output.txt", test_name, path_separator());
    snprintf(path_actual_error,
             256,
             "%s%cactual_error_%s_%s.txt",
             test_name,
             path_separator(),
             optimization,
             native);
    snprintf(path_expected_error, 256, "%s%cexpected_error.txt", test_name, path_separator());
    snprintf(path_input, sizeof(path_input), "%s%cinput.txt", test_name, path_separator());
    FILE *input = fopen(path_input, "r");
    if (input)
        fclose(input);
    else {
#ifdef _WIN32
        strcpy(path_input, "NUL");
#else
        strcpy(path_input, "/dev/null");
#endif
    }
#ifdef _WIN32
    const char *command_format = "\"\"%s\" --lang en --optimize %s --native %s "
                                 "\"%s%cprogram.goat\" < \"%s\" 1> \"%s\" 2> \"%s\"\"";
#else
    const char *command_format = "\"%s\" --lang en --optimize %s --native %s \"%s%cprogram.goat\" "
                                 "< \"%s\" 1> \"%s\" 2> \"%s\"";
#endif
    int length = snprintf(cmd,
                          sizeof(cmd),
                          command_format,
                          interpreter,
                          optimization,
                          native,
                          test_name,
                          path_separator(),
                          path_input,
                          path_actual_output,
                          path_actual_error);

    if (length < 0 || (size_t)length >= sizeof(cmd))
        return 0;
    int status = system(cmd);

    FILE *actual_output = NULL, *expected_output = NULL, *actual_error = NULL,
         *expected_error = NULL;
    actual_output = fopen(path_actual_output, "r");
    if (!actual_output)
        goto cleanup;
    expected_output = fopen(path_expected_output, "r");
    if (!expected_output) {
        if (get_file_size(actual_output) > 0)
            goto cleanup;
    } else {
        if (!compare_files(actual_output, expected_output))
            goto cleanup;
    }
    actual_error = fopen(path_actual_error, "r");
    if (!actual_error)
        goto cleanup;
    expected_error = fopen(path_expected_error, "r");
    if (!expected_error) {
        if (get_file_size(actual_error) > 0)
            goto cleanup;
    } else {
        if (!compare_files(actual_error, expected_error))
            goto cleanup;
    }

    if (status == -1 || ((status != 0) != (expected_error != NULL)))
        goto cleanup;
    result = 1;

cleanup:
    if (actual_output)
        fclose(actual_output);
    if (expected_output)
        fclose(expected_output);
    if (actual_error)
        fclose(actual_error);
    if (expected_error)
        fclose(expected_error);
    if (result) {
        remove(path_actual_output);
        remove(path_actual_error);
    }

    return result;
}

/**
 * @brief Entry point.
 * @return 0 if all tests passed, non-zero if any test failed.
 */
int main(int argc, char **argv) {
    if (argc < 3 || argc > 4
        || (argc == 4 && strcmp(argv[3], "off") && strcmp(argv[3], "auto")
            && strcmp(argv[3], "required"))) {
        printf("Usage: functional_testing <interpreter> <list of tests> [off|auto|required]\n");
        return -1;
    }
    const char *native = argc == 4 ? argv[3] : "off";
    test_output_start("functional");
    fix_path_separator(argv[1]);
    FILE *list = fopen(argv[2], "r");
    if (!list) {
        fprintf(stderr, "Could not open '%s'\n", argv[2]);
        test_output_case(false, "test list (unreadable)");
        test_output_summary("Functional", 0, 1);
        return -1;
    }

    int passed = 0;
    int failed = 0;
    char test_name[128];
    while (fgets(test_name, sizeof(test_name), list)) {
        char *test_name_trim = trim(test_name);
        if (strlen(test_name_trim) > 0 && test_name_trim[0] != '#') {
            const char *levels[] = {"none", "all"};
            for (size_t level = strcmp(native, "off") ? 1 : 0; level < 2; level++) {
                int result = do_test(argv[1], test_name_trim, levels[level], native);
                if (result) {
                    passed++;
                } else {
                    failed++;
                }
                test_output_case(result,
                                 "%s (optimize=%s, native=%s)",
                                 test_name_trim,
                                 levels[level],
                                 native);
            }
        }
    }
    if (ferror(list)) {
        test_output_case(false, "test list (unreadable)");
        failed++;
    }
    test_output_summary("Functional", passed, failed);

    fclose(list);
    if (failed > 0)
        return -1;
    return 0;
}
