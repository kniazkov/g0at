/** @file inspect_probe_library.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Included by generated real-library inspection fixtures, never production code.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#    include <windows.h>
#else
#    include <sys/select.h>
#    include <sys/types.h>
#    include <unistd.h>
#endif

static void mark(const char *event) {
    const char *root = getenv("GOAT_PROBE_TEST_MARKERS");
    char path[4096];
    if (!root || snprintf(path, sizeof(path), "%s/events", root) >= (int)sizeof(path))
        abort();
    FILE *file = fopen(path, "ab");
    if (!file || fprintf(file, "%s\n", event) < 0 || fclose(file))
        abort();
}

static int mode_is(const char *expected) {
    const char *mode = getenv("GOAT_PROBE_TEST_MODE");
    return mode && !strcmp(mode, expected);
}

static void hang(void) {
#ifdef _WIN32
    Sleep(INFINITE);
#else
    for (;;)
        pause();
#endif
}

static void spawn_descendant(void) {
    const char *worker = getenv("GOAT_PROBE_TEST_WORKER");
    if (!worker)
        abort();
#ifdef _WIN32
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, worker, -1, NULL, 0);
    wchar_t *path = calloc((size_t)count, sizeof(wchar_t));
    if (!count || !path
        || !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, worker, -1, path, count))
        abort();
    STARTUPINFOW startup = {0};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process = {0};
    if (!CreateProcessW(path, NULL, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &process))
        abort();
    free(path);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
#else
    pid_t pid = fork();
    if (pid < 0)
        abort();
    if (!pid) {
        execl(worker, worker, (char *)NULL);
        _exit(91);
    }
#endif
    /* Child readiness is a filesystem barrier; no guessed sleep proves launch. */
    const char *root = getenv("GOAT_PROBE_TEST_MARKERS");
    char marker[4096];
    snprintf(marker, sizeof(marker), "%s/descendant-pinned", root);
    for (;;) {
        FILE *file = fopen(marker, "rb");
        if (file) {
            fclose(file);
            break;
        }
#ifdef _WIN32
        Sleep(1);
#else
        /* A bounded parent deadline handles a worker that cannot start. */
        struct timeval interval = {0, 1000};
        select(0, NULL, NULL, NULL, &interval);
#endif
    }
}

__attribute__((constructor)) static void loaded(void) {
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    mark("loaded");
    FILE *relative = fopen("relative-write", "wb");
    if (!relative || fputs("probe private working directory", relative) < 0 || fclose(relative))
        abort();
    if (mode_is("output")) {
        size_t size = (size_t)strtoul(getenv("GOAT_PROBE_TEST_BYTES"), NULL, 10);
        for (size_t i = 0; i < size; i++) {
            if (fputc((int)(i % 256), stdout) == EOF || fputc((int)((i + 97) % 256), stderr) == EOF)
                abort();
        }
        fflush(stdout);
        fflush(stderr);
    }
    if (mode_is("output-hang")) {
        for (size_t i = 0;; i++) {
            if (fputc((int)(i % 256), stdout) == EOF || fputc((int)((i + 97) % 256), stderr) == EOF)
                abort();
            if (!(i % 4096)) {
                fflush(stdout);
                fflush(stderr);
            }
        }
    }
    if (mode_is("stdin")) {
        if (fgetc(stdin) != EOF || ferror(stdin))
            abort();
        char path[4096];
        snprintf(path, sizeof(path), "%s/eof", getenv("GOAT_PROBE_TEST_MARKERS"));
        FILE *file = fopen(path, "wb");
        if (!file || fclose(file))
            abort();
    }
    if (mode_is("tree") || mode_is("tree-crash") || mode_is("tree-hang"))
        spawn_descendant();
    if (mode_is("load-crash") || mode_is("tree-crash"))
        abort();
    if (mode_is("load-hang") || mode_is("tree-hang"))
        hang();
    if (mode_is("load-exit-zero"))
        _Exit(0);
}

__attribute__((destructor)) static void unloaded(void) {
    mark("unloaded");
    if (mode_is("unload-crash"))
        abort();
    if (mode_is("unload-hang"))
        hang();
}
