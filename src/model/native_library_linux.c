/** @file native_library_linux.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Explicit-path Linux loading with immediate, local symbol resolution.
 */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#    define _GNU_SOURCE
#endif
#if defined(__linux__) && !defined(_XOPEN_SOURCE)
#    define _XOPEN_SOURCE 700
#endif
#include "native_library.h"

#ifdef __linux__
#    include <dlfcn.h>
#    include <errno.h>
#    include <inttypes.h>
#    include <pthread.h>
#    include <stdio.h>
#    include <stdlib.h>
#    include <string.h>
#    include <sys/resource.h>
#    include <sys/syscall.h>
#    include <unistd.h>

void *open_native_library_handle(const char *path, char **diagnostic) {
    char *absolute = realpath(path, NULL);
    if (!absolute) {
        *diagnostic = copy_native_library_diagnostic(strerror(errno));
        return NULL;
    }
    void *handle = dlopen(absolute, RTLD_NOW | RTLD_LOCAL);
    if (!handle)
        *diagnostic = copy_native_library_diagnostic(dlerror());
    free(absolute);
    return handle;
}

bool get_native_library_query(void *handle, goat_native_query_v2_t *query, char **diagnostic) {
    dlerror();
    void *address = dlsym(handle, "goat_native_query_v2");
    const char *error = dlerror();
    if (error || !address) {
        *diagnostic = copy_native_library_diagnostic(error ? error : "Missing native query");
        return false;
    }
    _Static_assert(sizeof(*query) == sizeof(address), "POSIX function pointer representation");
    memcpy(query, &address, sizeof(*query));
    return true;
}

bool close_native_library_handle_checked(void *handle, char **diagnostic) {
    if (!dlclose(handle))
        return true;
    if (diagnostic)
        *diagnostic = copy_native_library_diagnostic(dlerror());
    return false;
}

void close_native_library_handle(void *handle) {
    (void)close_native_library_handle_checked(handle, NULL);
}

#    ifndef __GLIBC__
/* Linux expresses stack_guard_gap in pages. Use the actual page size and the
 * boot override, not a hard-coded x86 byte count. Unreadable/oversized or malformed
 * command lines fail closed; this is capability detection, not an assumption. */
static bool main_stack_guard_gap(uintptr_t *gap, uintptr_t *page_size) {
    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0)
        return false;
    FILE *file = fopen("/proc/cmdline", "r");
    if (!file)
        return false;
    char line[4096];
    bool ok = fgets(line, sizeof(line), file) && strchr(line, '\n') && fgetc(file) == EOF
              && !ferror(file);
    fclose(file);
    if (!ok)
        return false;
    uintptr_t pages = 256;
    char *save = NULL;
    for (char *word = strtok_r(line, " \t\r\n", &save); word;
         word = strtok_r(NULL, " \t\r\n", &save)) {
        static const char prefix[] = "stack_guard_gap=";
        if (strncmp(word, prefix, sizeof(prefix) - 1))
            continue;
        const char *digit = word + sizeof(prefix) - 1;
        if (!*digit)
            return false;
        pages = 0;
        do {
            if (*digit < '0' || *digit > '9'
                || pages > (UINTPTR_MAX - (unsigned)(*digit - '0')) / 10)
                return false;
            pages = pages * 10 + (unsigned)(*digit++ - '0');
        } while (*digit);
    }
    if (pages > UINTPTR_MAX / (uintptr_t)page)
        return false;
    *gap = pages * (uintptr_t)page;
    *page_size = (uintptr_t)page;
    return true;
}

/* musl reports only currently mapped main-stack pages. Extend the capability
 * only for the original main thread's kernel-marked grow-down [stack] mapping,
 * within its finite RLIMIT_STACK and actual kernel guard gap. Custom fixed
 * stacks, missing proc evidence, and uncertain policies remain unsupported. */
static bool expandable_main_stack(uintptr_t here, size_t reserve) {
    uintptr_t gap, page;
    if (syscall(SYS_gettid) != getpid() || here < reserve || !main_stack_guard_gap(&gap, &page))
        return false;
    struct rlimit limit;
    if (getrlimit(RLIMIT_STACK, &limit) || limit.rlim_cur == RLIM_INFINITY
        || limit.rlim_cur > UINTPTR_MAX)
        return false;
    FILE *maps = fopen("/proc/self/smaps", "r");
    if (!maps)
        return false;
    char line[4096];
    uintptr_t previous = 0;
    bool candidate = false, result = false;
    while (fgets(line, sizeof(line), maps)) {
        if (!strchr(line, '\n'))
            break;
        uintptr_t begin, end;
        char permissions[5];
        int path = 0;
        if (sscanf(line,
                   "%" SCNxPTR "-%" SCNxPTR " %4s %*s %*s %*s %n",
                   &begin,
                   &end,
                   permissions,
                   &path)
                == 3
            && path) {
            if (candidate)
                break;
            if (begin >= end)
                break;
            if (begin <= here && here < end) {
                uintptr_t desired = here - reserve;
                desired -= desired % page;
                candidate = !strcmp(line + path, "[stack]\n") && !strcmp(permissions, "rw-p")
                            && end - desired <= limit.rlim_cur && desired >= previous
                            && desired - previous >= gap;
                if (!candidate)
                    break;
            }
            if (end <= here && end > previous)
                previous = end;
        } else if (candidate && !strncmp(line, "VmFlags:", 8)) {
            char *save = NULL;
            for (char *flag = strtok_r(line + 8, " \t\r\n", &save); flag;
                 flag = strtok_r(NULL, " \t\r\n", &save))
                if (!strcmp(flag, "gd"))
                    result = true;
            break;
        }
    }
    result = result && !ferror(maps);
    fclose(maps);
    return result;
}
#    endif

bool native_stack_has_headroom(void) {
    pthread_attr_t attributes;
    if (pthread_getattr_np(pthread_self(), &attributes))
        return false;
    void *base = NULL;
    size_t size = 0;
    int status = pthread_attr_getstack(&attributes, &base, &size);
    pthread_attr_destroy(&attributes);
    void *position = __builtin_frame_address(0);
    uintptr_t here = (uintptr_t)position, low = (uintptr_t)base;
    if (status || here < low || here - low >= size)
        return false;
    if (here - low >= 512 * 1024)
        return true;
#    ifndef __GLIBC__
    return expandable_main_stack(here, 512 * 1024);
#    else
    return false;
#    endif
}
#endif
