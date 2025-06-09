/**
 * @file functional_testing.c
 * @copyright 2025 Ivan Kniazkov
 * @brief A program for performing functional tests on the project's output.
 *
 * This program executes functional tests by running the project's binary with a set of 
 * test files. The program compares the output of the executed binary with expected output 
 * files and reports the test results (pass or fail). It uses the system's shell to run the 
 * binary and captures both the standard output and error output to verify the program's behavior.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/**
 * @brief Trims leading and trailing whitespace characters from a string.
 *
 * This function removes any spaces, tabs, or other whitespace characters from the 
 * beginning and end of the string, leaving only the core content of the string.
 * The trimming is done in-place, meaning the original string is modified.
 *
 * @param s The string to be trimmed.
 * @return A pointer to the trimmed string.
 */
static char * trim (char *s) {
    int i;
    while (isspace (*s)) s++;
    for (i = strlen (s) - 1; (isspace (s[i])); i--) ;
    s[i + 1] = '\0';
    return s;
}

/**
 * @brief Returns the correct path separator based on the operating system.
 *
 * This function determines the appropriate file path separator based on the platform.
 * It returns the backslash ('\') for Windows systems and forward slash ('/') for 
 * Unix-based systems (including Linux and macOS).
 *
 * @return The appropriate path separator character for the current platform.
 */
static const char path_separator() {
#ifdef _WIN32
    return '\\';
#else
    return '/';
#endif
}

/**
 * @brief Replaces all path separators in a given path with the platform-specific separator.
 *
 * This function takes a file path and ensures that all path separators are consistent 
 * with the current platform's requirements. It iterates through the string and 
 * replaces any backslashes or forward slashes with the correct separator.
 *
 * @param path The file path to modify.
 */
static void fix_path_separator(char *path) {
    while(*path) {
        if (*path == '\\' || *path == '/') {
            *path = path_separator();
        }
        path++;
    }
}

/**
 * @brief Compares the contents of two files, ignoring carriage return characters.
 *
 * This function compares two files byte by byte, skipping over carriage return characters (`\r`) 
 * that might appear in text files, especially when comparing files across different
 * operating systems. The comparison stops when either the end of one file is reached
 * or any mismatch is found.
 *
 * @param actual The file containing the actual output.
 * @param expected The file containing the expected output.
 * @return 1 if the files are identical (ignoring carriage returns), 0 if they are different.
 */
static int compare_files(FILE *actual, FILE *expected) {
    while(!feof(actual)) {
        int a = fgetc(actual);
        while(a == '\r') a = fgetc(actual);

        int e = fgetc(expected);
        while(e == '\r') e = fgetc(expected);
        
        if (a != e) return 0;
    }
    return feof(expected);
}

/**
 * @brief Calculates the size of a file in bytes.
 *
 * This function determines the size of a file by seeking to the end and using 
 * `ftell()` to obtain the byte offset. It then rewinds the file pointer to the 
 * beginning so the file can be used again later.
 *
 * @param file The file pointer to calculate the size of.
 * @return The size of the file in bytes.
 */
static int get_file_size(FILE *file) {
    fseek(file, 0, SEEK_END);
    int result = ftell(file);
    rewind(file);
    return result;
}

/**
 * @brief Executes a test by running the project's binary and comparing its output with
 * expected results.
 *
 * This function constructs the paths for actual and expected output/error files, 
 * runs the test by executing the program using the specified interpreter, 
 * and then compares the generated output and error files with the expected ones.
 * If the comparison passes, the test is considered successful; otherwise, it fails.
 *
 * @param interpreter The interpreter to use to run the project binary.
 * @param test_name The name of the test case, used to locate the expected output/error files.
 * @return 1 if the test passed, 0 if it failed.
 */
int do_test(char *interpreter, char *test_name) {
    int result = 0;
    
    char cmd[1024],
        path_actual_output[256],
        path_expected_output[256],
        path_actual_error[256],
        path_expected_error[256];
    snprintf(path_actual_output, 256, "%s%cactual_output.txt", test_name, path_separator());
    snprintf(path_expected_output, 256, "%s%cexpected_output.txt", test_name, path_separator());
    snprintf(path_actual_error, 256, "%s%cactual_error.txt", test_name, path_separator());
    snprintf(path_expected_error, 256, "%s%cexpected_error.txt", test_name, path_separator());
    snprintf(cmd, 1024, "%s --lang en %s%cprogram.goat 1> %s 2> %s",
        interpreter, test_name, path_separator(), path_actual_output, path_actual_error);

    system(cmd);
    
    FILE *actual_output = NULL, 
        *expected_output = NULL,
        *actual_error = NULL,
        *expected_error = NULL;
    actual_output = fopen(path_actual_output, "r");
    if (!actual_output) goto cleanup;
    expected_output = fopen(path_expected_output, "r");
    if (!expected_output) {
        if (get_file_size(actual_output) > 0) goto cleanup;
    } else {
        if (!compare_files(actual_output, expected_output)) goto cleanup;
    }
    actual_error = fopen(path_actual_error, "r");
    if (!actual_error) goto cleanup;
    expected_error = fopen(path_expected_error, "r");
    if (!expected_error) {
        if (get_file_size(actual_error) > 0) goto cleanup;
    } else {
        if (!compare_files(actual_error, expected_error)) goto cleanup;
    }
    
    result = 1;

cleanup:
    if (actual_output) fclose(actual_output);
    if (expected_output) fclose(expected_output);
    if (actual_error) fclose(actual_error);
    if (expected_error) fclose(expected_error);
    if (result) {
        remove(path_actual_output);
        remove(path_actual_error);        
    }

    return result;
}

/**
 * @brief Entry point.
 *
 * This function parses the command-line arguments, fixes file path separators, 
 * and runs the tests listed in the provided file. It reports the number of tests 
 * passed and failed, and returns a non-zero value if any test failed.
 *
 * @param argc The number of command-line arguments.
 * @param argv The command-line arguments, where:
 *        - argv[1] is the interpreter to use (e.g., the name of the binary).
 *        - argv[2] is the path to the list of test cases.
 * @return 0 if all tests passed, non-zero if any test failed.
 */
int main(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: functional_testing <interpreter> <list of tests>\n");
        return -1;
    }
    fix_path_separator(argv[1]);
    FILE *list = fopen(argv[2], "r");
    if (!list) {
        printf("Could not open '%s'\n", argv[2]);
        return -1;
    }

    int passed = 0;
    int failed = 0;
    char test_name[128];
    while (!feof(list)) {
        if (fgets(test_name, 128, list)) {
            char *test_name_trim = trim(test_name);
            if (strlen(test_name_trim) > 0 && test_name_trim[0] != '#') {
            int result = do_test(argv[1], test_name_trim);
                if (result) {
                    printf("[ ok ]");
                    passed++;
                }
                else {
                    printf("[fail]");
                    failed++;
                }
                printf(" %s\n", test_name_trim);
            }
        }
    }
    printf("\nFunctional testing done; total: %d, passed: %d, failed: %d.", passed + failed, passed, failed);

    fclose(list);
    if (failed > 0) return -1;
    return 0;
}
