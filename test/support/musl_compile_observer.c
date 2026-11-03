/** @file musl_compile_observer.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Read-only resource samples for intermittent musl CI compiler timeouts.
 */
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stopped;

static void stop(int signal_number) {
    (void)signal_number;
    stopped = 1;
}

static bool wanted_line(const char *line, const char *const *keys) {
    if (!keys)
        return true;
    for (size_t i = 0; keys[i]; ++i)
        if (!strncmp(line, keys[i], strlen(keys[i])))
            return true;
    return false;
}

static int copy_fields(FILE *out, const char *path, const char *const *keys) {
    FILE *in = fopen(path, "rb");
    if (!in) {
        int error = errno;
        fprintf(out, "file=%s errno=%d\n", path, error);
        return error;
    }
    fprintf(out, "file=%s\n", path);
    char line[4096];
    while (fgets(line, sizeof(line), in))
        if (wanted_line(line, keys))
            fputs(line, out);
    int error = ferror(in) ? (errno ? errno : EIO) : 0;
    if (error)
        fprintf(out, "read-error errno=%d\n", error);
    if (fclose(in)) {
        int close_error = errno ? errno : EIO;
        fprintf(out, "close-error errno=%d\n", close_error);
        if (!error || close_error == EACCES || close_error == EPERM)
            error = close_error;
    }
    return error;
}

static bool compiler(const char *name) {
    return !strcmp(name, "gcc") || !strcmp(name, "cc1") || !strcmp(name, "collect2")
           || !strcmp(name, "ld") || !strcmp(name, "ld.bfd") || !strcmp(name, "ld.gold");
}

/* Maps identify ranges, not allocation call sites. Keep only an aggregate and
 * a change fingerprint; do not publish raw addresses or mapped-file paths. */
static int mapping_summary(FILE *out, const char *path) {
    FILE *in = fopen(path, "rb");
    if (!in) {
        int error = errno;
        fprintf(out, "mapping-summary=%s errno=%d\n", path, error);
        return error;
    }
    size_t mappings = 0, anonymous = 0, malformed = 0;
    uint64_t bytes = 0, anonymous_bytes = 0, fingerprint = UINT64_C(14695981039346656037);
    char line[4096];
    while (fgets(line, sizeof(line), in)) {
        unsigned long long first, last, offset, inode;
        unsigned major, minor;
        char permissions[5];
        int consumed = 0;
        int fields = sscanf(line,
                            "%llx-%llx %4s %llx %x:%x %llu %n",
                            &first,
                            &last,
                            permissions,
                            &offset,
                            &major,
                            &minor,
                            &inode,
                            &consumed);
        if (fields != 7 || consumed <= 0 || last < first) {
            ++malformed;
        } else {
            ++mappings;
            bytes += (uint64_t)(last - first);
            const char *name = line + consumed;
            if (!*name || *name == '\n' || !strncmp(name, "[heap]", 6)
                || !strncmp(name, "[anon:", 6)) {
                ++anonymous;
                anonymous_bytes += (uint64_t)(last - first);
                /* Unsigned arithmetic is intentional. This is a change hint,
                 * not a cryptographic identity or proof of no mapping churn. */
                fingerprint = (fingerprint ^ (uint64_t)first) * UINT64_C(1099511628211);
                fingerprint = (fingerprint ^ (uint64_t)last) * UINT64_C(1099511628211);
                for (size_t i = 0; permissions[i]; ++i)
                    fingerprint =
                        (fingerprint ^ (unsigned char)permissions[i]) * UINT64_C(1099511628211);
            }
        }
        /* A long mapped-file path is irrelevant to the prefix, but must not
         * be counted as another mapping on the next iteration. */
        if (!strchr(line, '\n')) {
            int ch;
            while ((ch = fgetc(in)) != '\n' && ch != EOF) {
            }
        }
    }
    int error = ferror(in) ? (errno ? errno : EIO) : 0;
    fprintf(out,
            "mapping-summary=%s count=%zu bytes=%" PRIu64 " anonymous-count=%zu "
            "anonymous-bytes=%" PRIu64 " anonymous-fingerprint=%016" PRIx64 " malformed=%zu\n",
            path,
            mappings,
            bytes,
            anonymous,
            anonymous_bytes,
            fingerprint,
            malformed);
    if (error)
        fprintf(out, "maps-read-error errno=%d\n", error);
    if (fclose(in)) {
        int close_error = errno ? errno : EIO;
        fprintf(out, "maps-close-error errno=%d\n", close_error);
        if (!error || close_error == EACCES || close_error == EPERM)
            error = close_error;
    }
    return error;
}

static void processes(FILE *out, bool memory_detail, unsigned long sequence) {
    static bool mapping_access = true;
    DIR *directory = opendir("/proc");
    if (!directory) {
        fprintf(out, "proc-open errno=%d\n", errno);
        return;
    }
    struct dirent *entry;
    static const char *const keys[] = {"Name:",
                                       "State:",
                                       "Pid:",
                                       "PPid:",
                                       "VmRSS:",
                                       "VmHWM:",
                                       "Threads:",
                                       "voluntary_ctxt_switches:",
                                       "nonvoluntary_ctxt_switches:",
                                       NULL};
    static const char *const memory_keys[] = {"Name:",
                                              "State:",
                                              "Pid:",
                                              "PPid:",
                                              "VmPeak:",
                                              "VmSize:",
                                              "VmHWM:",
                                              "VmRSS:",
                                              "RssAnon:",
                                              "RssFile:",
                                              "RssShmem:",
                                              "VmData:",
                                              "VmPTE:",
                                              "VmSwap:",
                                              "HugetlbPages:",
                                              "THP_enabled:",
                                              "Threads:",
                                              "voluntary_ctxt_switches:",
                                              "nonvoluntary_ctxt_switches:",
                                              NULL};
    static const char *const rollup_keys[] = {"Rss:",
                                              "Pss:",
                                              "Pss_Anon:",
                                              "Pss_File:",
                                              "Pss_Shmem:",
                                              "Private_Clean:",
                                              "Private_Dirty:",
                                              "Anonymous:",
                                              "LazyFree:",
                                              "AnonHugePages:",
                                              "Swap:",
                                              "SwapPss:",
                                              NULL};
    for (;;) {
        errno = 0;
        entry = readdir(directory);
        if (!entry) {
            if (errno)
                fprintf(out, "proc-readdir errno=%d\n", errno);
            break;
        }
        const char *p = entry->d_name;
        if (!*p)
            continue;
        while (*p >= '0' && *p <= '9')
            ++p;
        if (*p)
            continue;
        char path[512], name[128];
        snprintf(path, sizeof(path), "/proc/%s/comm", entry->d_name);
        FILE *comm = fopen(path, "rb");
        if (!comm)
            continue; /* Normal process-exit races; no target known yet. */
        bool read_ok = fgets(name, sizeof(name), comm) != NULL;
        fclose(comm);
        if (!read_ok)
            continue;
        name[strcspn(name, "\r\n")] = 0;
        if (!compiler(name))
            continue;
        fprintf(out, "compiler pid=%s comm=%s\n", entry->d_name, name);
        snprintf(path, sizeof(path), "/proc/%s/stat", entry->d_name);
        copy_fields(out, path, NULL);
        snprintf(path, sizeof(path), "/proc/%s/status", entry->d_name);
        copy_fields(out, path, memory_detail ? memory_keys : keys);
        if (memory_detail && mapping_access && !strcmp(name, "cc1") && sequence % 6 == 0) {
            snprintf(path, sizeof(path), "/proc/%s/maps", entry->d_name);
            int error = mapping_summary(out, path);
            if (error != EACCES && error != EPERM) {
                snprintf(path, sizeof(path), "/proc/%s/smaps_rollup", entry->d_name);
                error = copy_fields(out, path, rollup_keys);
            }
            if (error == EACCES || error == EPERM) {
                mapping_access = false;
                fputs("mapping detail denied; further maps/smaps reads disabled\n", out);
            }
        }
    }
    if (closedir(directory))
        fprintf(out, "proc-close errno=%d\n", errno);
}

static bool sample(FILE *out, unsigned long sequence, bool memory_detail) {
    struct timespec wall, mono, cpu;
    if (clock_gettime(CLOCK_REALTIME, &wall) || clock_gettime(CLOCK_MONOTONIC, &mono)
        || clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu))
        return false;
    fprintf(out,
            "sample=%lu wall=%lld.%09ld monotonic=%lld.%09ld\n",
            sequence,
            (long long)wall.tv_sec,
            wall.tv_nsec,
            (long long)mono.tv_sec,
            mono.tv_nsec);
    static const char *const stat_keys[] = {"cpu ",
                                            "procs_running ",
                                            "procs_blocked ",
                                            "ctxt ",
                                            NULL};
    static const char *const mem_keys[] = {"MemTotal:",
                                           "MemFree:",
                                           "MemAvailable:",
                                           "Buffers:",
                                           "Cached:",
                                           "SwapTotal:",
                                           "SwapFree:",
                                           "Dirty:",
                                           "Writeback:",
                                           "Slab:",
                                           NULL};
    copy_fields(out, "/proc/stat", stat_keys);
    copy_fields(out, "/proc/meminfo", mem_keys);
    static const char *const paths[] = {"/proc/pressure/cpu",
                                        "/proc/pressure/memory",
                                        "/proc/pressure/io",
                                        "/sys/fs/cgroup/cpu.stat",
                                        "/sys/fs/cgroup/cpu.max",
                                        "/sys/fs/cgroup/memory.events",
                                        "/sys/fs/cgroup/memory.current",
                                        "/sys/fs/cgroup/memory.max",
                                        NULL};
    for (size_t i = 0; paths[i]; i++)
        copy_fields(out, paths[i], NULL);
    if (memory_detail) {
        static const char *const vm_keys[] = {"pgfault ",
                                              "pgmajfault ",
                                              "pgalloc_",
                                              "pgfree ",
                                              "pgactivate ",
                                              "pgdeactivate ",
                                              "pgscan_",
                                              "pgsteal_",
                                              "allocstall_",
                                              "compact_",
                                              "thp_",
                                              "numa_",
                                              "workingset_refault_",
                                              "pswpin ",
                                              "pswpout ",
                                              NULL};
        copy_fields(out, "/proc/vmstat", vm_keys);
    }
    processes(out, memory_detail, sequence);
    struct timespec end, cpu_end;
    if (clock_gettime(CLOCK_MONOTONIC, &end) || clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu_end))
        return false;
    fprintf(out,
            "observer-sample-cost wall-ns=%lld cpu-ns=%lld\n",
            (long long)(end.tv_sec - mono.tv_sec) * 1000000000 + end.tv_nsec - mono.tv_nsec,
            (long long)(cpu_end.tv_sec - cpu.tv_sec) * 1000000000 + cpu_end.tv_nsec - cpu.tv_nsec);
    fputc('\n', out);
    return !ferror(out) && fflush(out) == 0;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fputs("usage: native_compile_observer LOG_PATH\n", stderr);
        return 2;
    }
    struct sigaction action = {0};
    action.sa_handler = stop;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGTERM, &action, NULL) || sigaction(SIGINT, &action, NULL)) {
        perror("sigaction");
        return 1;
    }
    FILE *out = fopen(argv[1], "wb");
    if (!out) {
        perror("observer log");
        return 1;
    }
    struct utsname system;
    bool memory_detail = false;
    if (uname(&system) == 0) {
        memory_detail = !strcmp(system.machine, "aarch64");
        fprintf(out,
                "system=%s release=%s machine=%s CLK_TCK=%ld online_cpus=%ld PAGE_SIZE=%ld "
                "memory-detail=%d\n",
                system.sysname,
                system.release,
                system.machine,
                sysconf(_SC_CLK_TCK),
                sysconf(_SC_NPROCESSORS_ONLN),
                sysconf(_SC_PAGESIZE),
                (int)memory_detail);
    } else
        fprintf(out, "uname errno=%d\n", errno);
    static const char *const cpu_keys[] = {"model name",
                                           "CPU implementer",
                                           "CPU architecture",
                                           "CPU part",
                                           NULL};
    copy_fields(out, "/proc/cpuinfo", cpu_keys);
    if (memory_detail) {
        copy_fields(out, "/sys/kernel/mm/transparent_hugepage/enabled", NULL);
        copy_fields(out, "/sys/kernel/mm/transparent_hugepage/defrag", NULL);
        copy_fields(out, "/sys/kernel/mm/transparent_hugepage/hpage_pmd_size", NULL);
    }
    bool ok = true;
    unsigned long sequence = 0;
    while (!stopped) {
        if (!sample(out, sequence++, memory_detail)) {
            ok = false;
            break;
        }
        struct timespec delay = {5, 0}, remaining;
        while (!stopped && nanosleep(&delay, &remaining)) {
            if (errno != EINTR) {
                ok = false;
                stopped = 1;
                break;
            }
            delay = remaining;
        }
    }
    fprintf(out, "stopped=%d samples=%lu\n", (int)stopped, sequence);
    if (ferror(out) || fflush(out))
        ok = false;
    if (fclose(out))
        ok = false;
    if (!ok)
        fputs("observer failed: clock, sleep, or log write error\n", stderr);
    return ok ? 0 : 1;
}
