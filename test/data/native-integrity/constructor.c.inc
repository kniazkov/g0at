
#include <stdio.h>
#include <stdlib.h>
__attribute__((constructor)) static void marker(void) {
    const char *path = getenv("GOAT_INTEGRITY_MARKER");
    if (path) { FILE *f = fopen(path, "wb"); if (f) { fputs("loaded", f); fclose(f); } }
}
