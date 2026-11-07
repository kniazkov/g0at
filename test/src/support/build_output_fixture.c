/* Copyright 2026 Ivan Kniazkov
 * Distributed under the MIT license; see LICENSE.txt. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <windows.h>
#else
#    include <unistd.h>
#endif

static void pause_job(void) {
#ifdef _WIN32
    Sleep(10);
#else
    usleep(10000);
#endif
}

/* Separate stdout/stderr writes model the progress/color/diagnostic fragments
 * that used to interleave. The rendezvous fails if the build becomes serial. */
int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        const char *version = getenv("BUILD_OUTPUT_MAKE_VERSION");
        printf("GNU Make %s\n", version ? version : "3.82");
        return 0;
    }
    if (argc != 3)
        return 2;
    const char *id = argv[1];
    FILE *ready = fopen(id, "wb");
    if (!ready)
        return 3;
    fclose(ready);
    int joined = 0;
    for (int attempt = 0; attempt < 3000; ++attempt) {
        FILE *peer = fopen(argv[2], "rb");
        if (peer) {
            fclose(peer);
            joined = 1;
            break;
        }
        pause_job();
    }
    if (!joined) {
        fprintf(stderr, "parallel rendezvous failed: %s\n", id);
        return 4;
    }
    for (int line = 0; line < 4; ++line) {
        fprintf(stdout, "stdout-%s-%d:", id, line);
        fflush(stdout);
        pause_job();
        fprintf(stdout, "complete\n");
        fflush(stdout);
        fprintf(stderr, "stderr-%s-%d:", id, line);
        fflush(stderr);
        pause_job();
        fprintf(stderr, "complete\n");
        fflush(stderr);
    }
    fprintf(stderr, "warning: complete warning from %s\n", id);
    const char *control = getenv("BUILD_OUTPUT_CONTROL");
    if (control && strcmp(control, "fail") == 0 && strcmp(id, "first") == 0) {
        fprintf(stderr, "fatal error: deliberate fixture failure (first)\n");
        return 23;
    }
    return 0;
}
