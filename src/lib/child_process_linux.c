/** @file child_process_linux.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Private subreaper supervises a tree even when its owner dies during launch.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "child_process_internal.h"
#ifdef __linux__
#include "allocate.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

typedef struct {
    int error, cleanup, code, signal;
} child_reply_t;

struct child_process_t {
    child_result_t result;
    child_deadline_t deadline;
    pid_t supervisor;
    int control, pipes[3];
    bool done, stopped;
};

uint64_t child_monotonic_ms(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t))
        abort();
    return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void close_fd(int *fd) {
    if (*fd >= 0)
        close(*fd);
    *fd = -1;
}

/* Keep internal descriptors separate from standard streams even when the caller closed them. */
static int high_fd(int fd) {
    if (fd < 0 || fd >= 3)
        return fd;
    int copy = fcntl(fd, F_DUPFD_CLOEXEC, 3);
    int error = errno;
    close(fd);
    errno = error;
    return copy;
}

/* No allocation or inherited libc locks in either fork child. */
static void close_except(const int *keep, size_t count, int maximum) {
    unsigned first = 3;
    for (;;) {
        unsigned next = (unsigned)maximum;
        for (size_t i = 0; i < count; i++)
            if (keep[i] >= (int)first && (unsigned)keep[i] < next)
                next = (unsigned)keep[i];
        if (next > first) {
#ifdef SYS_close_range
            if (syscall(SYS_close_range, first, next - 1, 0))
#endif
                for (unsigned fd = first; fd < next; fd++)
                    close((int)fd);
        }
        if (next == (unsigned)maximum)
            break;
        first = next + 1;
    }
}

/* Only unreaped direct children are signalled: their PIDs cannot have been reused.
 * The subreaper adopts orphans (including setsid descendants) as ancestors die. */
static int kill_adopted(void) {
    int fd = open("/proc/thread-self/children", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return errno;
    char bytes[4096];
    pid_t pid = 0;
    ssize_t n;
    int error = 0;
    while ((n = read(fd, bytes, sizeof(bytes))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            if (bytes[i] >= '0' && bytes[i] <= '9')
                pid = pid * 10 + bytes[i] - '0';
            else if (pid) {
                if (kill(pid, SIGKILL) && errno != ESRCH)
                    error = errno;
                pid = 0;
            }
        }
    }
    if (n < 0)
        error = errno;
    if (pid && kill(pid, SIGKILL) && errno != ESRCH)
        error = errno;
    close(fd);
    return error;
}

static int cleanup_tree(pid_t root) {
    /* Root is still our unreaped child; the group ID is pinned until this signal. */
    if (kill(-root, SIGKILL) && errno != ESRCH)
        return errno;
    if (kill(root, SIGKILL) && errno != ESRCH)
        return errno;
    uint64_t end = child_monotonic_ms() + 5000;
    for (;;) {
        pid_t done = waitpid(root, NULL, WNOHANG);
        if (done == root || (done < 0 && errno == ECHILD))
            break;
        if (done < 0 && errno != EINTR)
            return errno;
        if (child_monotonic_ms() >= end)
            return ETIMEDOUT;
        poll(NULL, 0, 1);
    }
    for (;;) {
        pid_t reaped;
        do {
            reaped = waitpid(-1, NULL, WNOHANG);
        } while (reaped > 0 || (reaped < 0 && errno == EINTR));
        if (reaped < 0)
            return errno == ECHILD ? 0 : errno;
        int error = kill_adopted();
        if (error)
            return error;
        pid_t pid;
        do {
            pid = waitpid(-1, NULL, WNOHANG);
        } while (pid > 0 || (pid < 0 && errno == EINTR));
        if (pid < 0)
            return errno == ECHILD ? 0 : errno;
        if (child_monotonic_ms() >= end)
            return ETIMEDOUT;
        poll(NULL, 0, 1);
    }
}

static void supervise(int control,
                      const int outputs[3],
                      int input,
                      int watch,
                      const char *executable,
                      char *const *args,
                      int maximum,
                      bool survive_parent) {
    child_reply_t reply = {.code = -1};
    int errors[2] = {-1, -1};
    /* Terminal signals aimed at the owner must not kill its cleanup supervisor. */
    struct sigaction ignored = {.sa_handler = SIG_IGN};
    sigemptyset(&ignored.sa_mask);
    sigaction(SIGINT, &ignored, NULL);
    sigaction(SIGTERM, &ignored, NULL);
    sigaction(SIGHUP, &ignored, NULL);
    sigaction(SIGQUIT, &ignored, NULL);
    if (setsid() < 0 || prctl(PR_SET_CHILD_SUBREAPER, 1) || pipe2(errors, O_CLOEXEC | O_NONBLOCK)) {
        reply.error = errno;
        goto finish;
    }
    errors[0] = high_fd(errors[0]);
    errors[1] = high_fd(errors[1]);
    if (errors[0] < 0 || errors[1] < 0) {
        reply.error = errno;
        goto finish;
    }
    /* Reset inherited SIGCHLD handling/masks, including an owner's SIG_IGN. */
    struct sigaction action = {.sa_handler = SIG_DFL};
    sigemptyset(&action.sa_mask);
    sigaction(SIGCHLD, &action, NULL);
    sigset_t mask;
    sigemptyset(&mask);
    sigprocmask(SIG_SETMASK, &mask, NULL);
    pid_t root = fork();
    if (root == 0) {
        sigaction(SIGINT, &action, NULL);
        sigaction(SIGTERM, &action, NULL);
        sigaction(SIGHUP, &action, NULL);
        sigaction(SIGQUIT, &action, NULL);
        int error = 0;
        if (setpgid(0, 0))
            error = errno;
        if (!error && input >= 0 && dup2(input, STDIN_FILENO) < 0)
            error = errno;
        for (int i = 0; i < 2 && !error; i++)
            if (outputs[i] >= 0 && dup2(outputs[i], i + 1) < 0)
                error = errno;
        if (outputs[2] >= 0)
            fcntl(outputs[2], F_SETFD, 0);
        if (watch >= 0)
            fcntl(watch, F_SETFD, 0);
        int keep[] = {errors[1], outputs[2], watch};
        close_except(keep, 3, maximum);
        if (!error)
            execve(executable, args, environ);
        if (!error)
            error = errno;
        (void)write(errors[1], &error, sizeof(error));
        _exit(127);
    }
    if (root < 0) {
        reply.error = errno;
        goto finish;
    }
    int keep[] = {control, errors[0]};
    close_except(keep, 2, maximum);
    /* Do not retain the owner's standard pipes in the supervisor either. */
    close(0);
    close(1);
    close(2);
    for (;;) {
        siginfo_t info = {0};
        if (waitid(P_PID, root, &info, WEXITED | WNOHANG | WNOWAIT) < 0 && errno != EINTR) {
            reply.error = errno;
            break;
        }
        if (info.si_pid) {
            if (info.si_code == CLD_EXITED)
                reply.code = info.si_status;
            else
                reply.signal = info.si_status;
            break;
        }
        struct pollfd event = {.fd = control, .events = POLLIN};
        int n = poll(&event, 1, 10);
        if (n > 0) {
            char request;
            ssize_t received = recv(control, &request, 1, MSG_DONTWAIT);
            if (!received && survive_parent) {
                close(control);
                control = -1;
            } else
                break; /* Explicit stop, or death of an ordinary owner. */
        }
        if (n < 0 && errno != EINTR) {
            reply.error = errno;
            break;
        }
    }
    int launch_error = 0;
    if (read(errors[0], &launch_error, sizeof(launch_error)) == sizeof(launch_error))
        reply.error = launch_error;
    reply.cleanup = cleanup_tree(root);
finish:
    (void)send(control, &reply, sizeof(reply), MSG_NOSIGNAL);
    _exit(0);
}

static char *resolve_executable(const char *name) {
    if (!name)
        name = "/proc/self/exe";
    if (strchr(name, '/')) {
        char *copy = ALLOC(strlen(name) + 1);
        strcpy(copy, name);
        return copy;
    }
    const char *path = getenv("PATH");
    if (!path)
        path = "/bin:/usr/bin";
    char *candidate = ALLOC(strlen(path) + strlen(name) + 3);
    const char *part = path;
    for (;;) {
        const char *end = strchr(part, ':');
        size_t n = end ? (size_t)(end - part) : strlen(part);
        memcpy(candidate, part, n);
        if (n)
            candidate[n++] = '/';
        strcpy(candidate + n, name);
        if (!access(candidate, X_OK))
            return candidate;
        if (!end)
            break;
        part = end + 1;
    }
    FREE(candidate);
    errno = ENOENT;
    return NULL;
}

child_process_t *start_child_process(const child_options_t *options) {
    if (!options || !options->arguments || !options->arguments[0])
        return NULL;
    child_process_t *p = CALLOC(sizeof(*p));
    p->control = -1;
    for (int i = 0; i < 3; i++)
        p->pipes[i] = -1;
    p->deadline = options->deadline;
    p->result.exit_code = -1;
    int outputs[3] = {-1, -1, -1}, control[2] = {-1, -1}, input = -1, watch = -1;
    char *executable = resolve_executable(options->executable);
    char **args = NULL;
    int error = executable ? 0 : errno;
    struct rlimit limit;
    if (!error && getrlimit(RLIMIT_NOFILE, &limit))
        error = errno;
    if (!error && options->empty_stdin) {
        input = high_fd(open("/dev/null", O_RDONLY | O_CLOEXEC));
        if (input < 0)
            error = errno;
    }
    if (!error && options->parent_watch) {
#ifdef SYS_pidfd_open
        watch = high_fd((int)syscall(SYS_pidfd_open, getpid(), 0));
        if (watch < 0)
            error = errno;
#else
        error = ENOSYS;
#endif
    }
    for (int i = 0; i < 3 && !error; i++) {
        if (!(i == 2 ? options->result_channel : options->capture_output))
            continue;
        int pipe_fds[2];
        if (pipe2(pipe_fds, O_CLOEXEC)) {
            error = errno;
            break;
        }
        p->pipes[i] = high_fd(pipe_fds[0]);
        outputs[i] = high_fd(pipe_fds[1]);
        if (p->pipes[i] < 0 || outputs[i] < 0 || fcntl(p->pipes[i], F_SETFL, O_NONBLOCK))
            error = errno;
    }
    if (!error && socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, control))
        error = errno;
    if (!error) {
        control[0] = high_fd(control[0]);
        control[1] = high_fd(control[1]);
        if (control[0] < 0 || control[1] < 0)
            error = errno;
    }
    char result_arg[32], parent_arg[32];
    snprintf(result_arg, sizeof(result_arg), "%d", outputs[2]);
    snprintf(parent_arg, sizeof(parent_arg), "%d", watch);
    if (!error) {
        size_t count = 0;
        while (options->arguments[count])
            count++;
        args = ALLOC((count + 1) * sizeof(*args));
        for (size_t i = 0; i <= count; i++) {
            const char *arg = options->arguments[i];
            if (arg && options->result_channel && !strcmp(arg, CHILD_RESULT_ARGUMENT))
                arg = result_arg;
            if (arg && options->parent_watch && !strcmp(arg, CHILD_PARENT_ARGUMENT))
                arg = parent_arg;
            args[i] = (char *)arg;
        }
        if (p->deadline && child_monotonic_ms() >= p->deadline) {
            p->result.status = CHILD_TIMED_OUT;
            p->done = true;
        } else {
            sigset_t blocked, previous;
            sigfillset(&blocked);
            sigprocmask(SIG_BLOCK, &blocked, &previous);
            p->supervisor = fork();
            int fork_error = errno;
            if (!p->supervisor)
                supervise(control[1],
                          outputs,
                          input,
                          watch,
                          executable,
                          args,
                          limit.rlim_cur > INT_MAX ? INT_MAX : (int)limit.rlim_cur,
                          options->survive_parent);
            sigprocmask(SIG_SETMASK, &previous, NULL);
            if (p->supervisor < 0)
                error = fork_error;
            else {
                p->control = control[0];
                control[0] = -1;
            }
        }
    }
    if (error) {
        p->result.status = CHILD_START_ERROR;
        p->result.system_error = error;
        p->done = true;
    }
    close_fd(&control[0]);
    close_fd(&control[1]);
    close_fd(&input);
    close_fd(&watch);
    for (int i = 0; i < 3; i++)
        close_fd(&outputs[i]);
    FREE(args);
    FREE(executable);
    return p;
}

static void drain(child_process_t *p) {
    child_output_t *streams[] = {&p->result.out, &p->result.err, &p->result.result};
    /* A bounded turn guarantees a noisy child cannot starve deadline/status checks. */
    for (int i = 0; i < 3; i++) {
        for (int turn = 0; p->pipes[i] >= 0 && turn < 16; turn++) {
            unsigned char data[4096];
            ssize_t n = read(p->pipes[i], data, sizeof(data));
            if (n > 0)
                child_capture(streams[i], data, (size_t)n);
            else {
                if (!n)
                    close_fd(&p->pipes[i]);
                else if (errno != EAGAIN && errno != EINTR) {
                    p->result.system_error = errno;
                    close_fd(&p->pipes[i]);
                }
                break;
            }
        }
    }
}

static const child_result_t *wait_process(child_process_t *p, bool stop) {
    if (p->done)
        return &p->result;
    uint64_t cleanup_deadline = 0;
    for (;;) {
        if (!p->stopped && (stop || (p->deadline && child_monotonic_ms() >= p->deadline))) {
            p->result.status = stop ? CHILD_STOPPED : CHILD_TIMED_OUT;
            p->stopped = true;
            (void)send(p->control, "s", 1, MSG_NOSIGNAL);
            cleanup_deadline = child_monotonic_ms() + 6000;
        }
        drain(p);
        child_reply_t reply;
        ssize_t n = recv(p->control, &reply, sizeof(reply), MSG_DONTWAIT);
        if (n == sizeof(reply)) {
            p->result.exit_code = reply.code;
            p->result.signal_number = reply.signal;
            p->result.cleanup_error = reply.cleanup;
            if (!p->stopped) {
                p->result.status = reply.error    ? CHILD_START_ERROR
                                   : reply.signal ? CHILD_SIGNALED
                                                  : CHILD_EXITED;
                if (p->deadline && child_monotonic_ms() >= p->deadline)
                    p->result.status = CHILD_TIMED_OUT;
            }
            if (reply.error)
                p->result.system_error = reply.error;
            /* Tree is gone: pipes have a finite buffered tail, never wait for foreign EOF. */
            drain(p);
            int status;
            while (waitpid(p->supervisor, &status, 0) < 0 && errno == EINTR) {
            }
            p->supervisor = 0;
            break;
        }
        if (!n || (n < 0 && errno != EAGAIN && errno != EINTR)
            || (cleanup_deadline && child_monotonic_ms() >= cleanup_deadline)) {
            p->result.cleanup_error = EIO;
            if (!p->stopped)
                p->result.status = CHILD_IO_ERROR;
            break;
        }
        struct pollfd fds[4] = {{.fd = p->control, .events = POLLIN}};
        for (int i = 0; i < 3; i++)
            fds[i + 1] = (struct pollfd){.fd = p->pipes[i], .events = POLLIN};
        poll(fds, 4, 5);
    }
    p->done = true;
    return &p->result;
}

const child_result_t *wait_child_process(child_process_t *p) {
    return wait_process(p, false);
}

const child_result_t *stop_child_process(child_process_t *p) {
    return wait_process(p, true);
}

void destroy_child_process(child_process_t *p) {
    if (!p)
        return;
    stop_child_process(p);
    close_fd(&p->control);
    for (int i = 0; i < 3; i++)
        close_fd(&p->pipes[i]);
    if (p->supervisor > 0)
        waitpid(p->supervisor, NULL, WNOHANG);
    FREE(p);
}

bool child_write_result(uintptr_t handle, const void *data, size_t size) {
    const unsigned char *p = data;
    while (size) {
        ssize_t n = write((int)handle, p, size > 4096 ? 4096 : size);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return false;
        p += n;
        size -= (size_t)n;
    }
    return true;
}

bool child_wait_parent(uintptr_t handle, child_deadline_t deadline) {
    struct pollfd fd = {.fd = (int)handle, .events = POLLIN};
    for (;;) {
        if (deadline && child_monotonic_ms() >= deadline)
            return false;
        int result = poll(&fd, 1, deadline ? 5 : -1);
        if (result > 0)
            return (fd.revents & POLLIN) != 0;
        if (result < 0 && errno != EINTR)
            return false;
    }
}

void child_close_channel(uintptr_t handle) {
    close((int)handle);
}
#endif
