/** @file compiler_fixture.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Native executable wrapper for Windows process and failure tests.
 */
#include "lib/allocate.h"
#include "lib/windows_command_line.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static DWORD execute(const char *const *args) {
    char *command = create_windows_command_line(args);
    if (!command)
        return 98;
    STARTUPINFOA startup = {.cb = sizeof(startup), .dwFlags = STARTF_USESTDHANDLES};
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process = {0};
    if (!CreateProcessA(args[0], command, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &process)) {
        FREE(command);
        return 99;
    }
    FREE(command);
    DWORD code = 99;
    WaitForSingleObject(process.hProcess, INFINITE);
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return code;
}

int main(int argc, char **argv) {
    if (argc == 5 && !strcmp(argv[1], "--locked")) {
        HMODULE library = LoadLibraryA(argv[2]);
        if (!library)
            return 90;
        const char *args[] = {argv[3], "--lang", "en", "--save-library", argv[4], NULL};
        DWORD code = execute(args);
        FreeLibrary(library);
        return code == 0 || code == 99;
    }
    const char *mode = getenv("FIXTURE_MODE");
    if (!mode)
        mode = "pass";
    if (!strcmp(mode, "absent"))
        return 0;
    if (!strcmp(mode, "exit")) {
        puts("compiler stdout");
        fputs("compiler stderr\n", stderr);
        return 23;
    }
    if (!strcmp(mode, "exception"))
        ExitProcess(0xc0000005UL);
    if (!strcmp(mode, "verbose")) {
        for (unsigned i = 0; i < 100000; i++)
            fputc('x', stderr);
        return 2;
    }
    const char *output = NULL, *source = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc)
            output = argv[i + 1];
        size_t n = strlen(argv[i]);
        if (n > 2 && !strcmp(argv[i] + n - 2, ".c"))
            source = argv[i];
    }
    if (!source || !output)
        return 91;
    if (!strcmp(mode, "empty")) {
        FILE *file = fopen(output, "wb");
        if (!file)
            return 92;
        return fclose(file) != 0;
    }
    if (!strcmp(mode, "invalid") || !strcmp(mode, "unresolved")) {
        FILE *file = fopen(source, "wb");
        if (!file)
            return 93;
        fputs(
            !strcmp(mode, "invalid")
                ? "invalid C source\n"
                : "__declspec(dllexport) int f(void){extern int missing(void);return missing();}\n",
            file);
        fclose(file);
    }
    if (!strcmp(mode, "warning")) {
        if (getchar() != EOF)
            return 94;
        puts("compiler warning on success");
        fflush(stdout);
    }
    const char **args = calloc((size_t)argc + 2, sizeof(*args));
    args[0] = getenv("REAL_CC");
    if (!args[0])
        return 95;
    for (int i = 1; i < argc; i++)
        args[i] = argv[i];
    if (!strcmp(mode, "fast"))
        args[argc] = "-ffast-math";
    DWORD code = execute(args);
    free(args);
    ExitProcess(code);
}
