/** @file compiler.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Compiler substitutes for invalid and unrelated artifacts.
 */
#ifdef _WIN32
#    include "lib/memory/allocate.h"
#    include "lib/process/windows_command_line.h"

#    include <windows.h>
#else
#    include <unistd.h>
#endif
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int pass_probe(int argc, char **argv) {
    const char *real = getenv("GOAT_PIPELINE_REAL_COMPILER");
    if (!real)
        return 1;
    (void)argc;
    argv[0] = (char *)real;
#ifdef _WIN32
    char *command = create_windows_command_line((const char *const *)argv);
    if (!command)
        return 97;
    STARTUPINFOA startup = {.cb = sizeof(startup), .dwFlags = STARTF_USESTDHANDLES};
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process = {0};
    if (!CreateProcessA(real, command, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &process)) {
        FREE(command);
        return 98;
    }
    FREE(command);
    DWORD code = 99;
    WaitForSingleObject(process.hProcess, INFINITE);
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return (int)code;
#else
    (void)argc;
    execv(real, argv);
    return 99;
#endif
}

int main(int argc, char **argv) {
    bool user_build = false;
    for (int i = 1; i < argc; i++) {
        const char *base = strrchr(argv[i], '/');
        if (!base)
            base = strrchr(argv[i], '\\');
        base = base ? base + 1 : argv[i];
        if (!strcmp(base, "module.c"))
            user_build = true;
    }
    if (!user_build)
        return pass_probe(argc, argv);
    if (strstr(argv[0], "failing-compiler")) {
        fputs("intentional user C compilation failure\n", stderr);
        return 23;
    }

    const char *output = NULL;
    for (int i = 1; i + 1 < argc; i++)
        if (!strcmp(argv[i], "-o"))
            output = argv[i + 1];
    if (!output)
        return 1;
    FILE *target = fopen(output, "wb");
    if (!target)
        return 2;
    if (strstr(argv[0], "invalid-compiler")) {
        fputs("not a shared library", target);
    } else {
        const char *path = getenv("GOAT_PIPELINE_WRONG_LIBRARY");
        FILE *source = path ? fopen(path, "rb") : NULL;
        if (!source) {
            fclose(target);
            return 3;
        }
        int ch;
        while ((ch = fgetc(source)) != EOF)
            fputc(ch, target);
        fclose(source);
    }
    return fclose(target) ? 4 : 0;
}
