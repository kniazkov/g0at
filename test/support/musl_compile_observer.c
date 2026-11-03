/** @file musl_compile_observer.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Read-only resource samples for intermittent musl CI compiler timeouts.
 */
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <errno.h>
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

static void copy_fields(FILE *out, const char *path, const char *const *keys) {
    FILE *in = fopen(path, "rb");
    if (!in) {
        fprintf(out, "file=%s errno=%d\n", path, errno);
        return;
    }
    fprintf(out, "file=%s\n", path);
    char line[4096];
    while (fgets(line, sizeof(line), in))
        if (wanted_line(line, keys))
            fputs(line, out);
    if (ferror(in))
        fprintf(out, "read-error errno=%d\n", errno);
    if (fclose(in))
        fprintf(out, "close-error errno=%d\n", errno);
}

static bool compiler(const char *name) {
    return !strcmp(name, "gcc") || !strcmp(name, "cc1") || !strcmp(name, "collect2")
           || !strcmp(name, "ld") || !strcmp(name, "ld.bfd") || !strcmp(name, "ld.gold");
}

static void processes(FILE *out) {
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
        copy_fields(out, path, keys);
    }
    if (closedir(directory))
        fprintf(out, "proc-close errno=%d\n", errno);
}

static bool sample(FILE *out, unsigned long sequence) {
    struct timespec wall, mono;
    if (clock_gettime(CLOCK_REALTIME, &wall) || clock_gettime(CLOCK_MONOTONIC, &mono))
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
    processes(out);
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
    if (uname(&system) == 0)
        fprintf(out,
                "system=%s release=%s machine=%s CLK_TCK=%ld online_cpus=%ld\n",
                system.sysname,
                system.release,
                system.machine,
                sysconf(_SC_CLK_TCK),
                sysconf(_SC_NPROCESSORS_ONLN));
    else
        fprintf(out, "uname errno=%d\n", errno);
    static const char *const cpu_keys[] = {"model name",
                                           "CPU implementer",
                                           "CPU architecture",
                                           "CPU part",
                                           NULL};
    copy_fields(out, "/proc/cpuinfo", cpu_keys);
    bool ok = true;
    unsigned long sequence = 0;
    while (!stopped) {
        if (!sample(out, sequence++)) {
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
