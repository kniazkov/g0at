/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

char * trim (char *s) {
    int i;
    while (isspace (*s)) s++;
    for (i = strlen (s) - 1; (isspace (s[i])); i--) ;
    s[i + 1] = '\0';
    return s;
}

char path_separator() {
#ifdef _WIN32
    return '\\';
#else
    return '/';
#endif
}

void fix_path_separator(char *path) {
    while(*path) {
        if (*path == '\\' || *path == '/') {
            *path = path_separator();
        }
        path++;
    }
}

int compare_files(FILE *actual, FILE *expected) {
    while(!feof(actual)) {
        int a = fgetc(actual);
        while(a == '\r') a = fgetc(actual);

        int e = fgetc(expected);
        while(e == '\r') e = fgetc(expected);
        
        if (a != e) return 0;
    }
    return feof(expected);
}

int do_test(char *interpreter, char *test_name) {
    int result = 0;
    
    char cmd[256],
        path_actual_output[128],
        path_expected_output[128];
    snprintf(path_actual_output, 128, "%s%cactual_output.txt", test_name, path_separator());
    snprintf(path_expected_output, 128, "%s%cexpected_output.txt", test_name, path_separator());
    snprintf(cmd, 256, "%s %s%cprogram.goat > %s",
        interpreter, test_name, path_separator(), path_actual_output);

    system(cmd);
    
    FILE *actual_output = NULL, 
        *expected_output = NULL;
    actual_output = fopen(path_actual_output, "r");
    if (!actual_output) return 0;
    expected_output = fopen(path_expected_output, "r");
    if (!expected_output) goto cleanup;
    
    result = compare_files(actual_output, expected_output);

cleanup:
    if (actual_output) fclose(actual_output);
    if (expected_output) fclose(expected_output);
    if (result) {
        remove(path_actual_output);
    }

    return result;
}

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
            if (strlen(test_name_trim) > 0) {
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
