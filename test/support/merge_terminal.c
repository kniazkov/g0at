/** @file merge_terminal.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Bounded real-terminal transport for merge integration scenarios.
 */
#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif
#include "merge_terminal.h"

#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#    include <errno.h>
#    include <fcntl.h>
#    include <poll.h>
#    include <signal.h>
#    include <sys/wait.h>
#    include <termios.h>
#    include <unistd.h>

typedef struct {
    scenario_run_t *run;
    pid_t pid;
    int master, slave, pipes[2][2], status;
    bool reaped;
    unsigned char *bytes[2];
    size_t sizes[2];
    FILE *files[2], *log;
    char *directory;
} terminal_t;

#    define REQUIRE(t, condition) scenario_require((t)->run, (condition), #condition)

static bool close_fd(int *fd) {
    if (*fd < 0)
        return true;
    int value = *fd;
    *fd = -1;
    return close(value) == 0;
}

static bool cleanup(void *argument) {
    terminal_t *t = argument;
    bool ok = true;
    if (t->pid > 0 && !t->reaped) {
        /* The child owns a fresh process group; never signal a reaped PID. */
        if (kill(-t->pid, SIGKILL) && errno != ESRCH)
            ok = false;
        if (kill(t->pid, SIGKILL) && errno != ESRCH)
            ok = false;
        uint64_t end = child_deadline_after(2000);
        while (!t->reaped && child_monotonic_ms() < end) {
            pid_t result = waitpid(t->pid, &t->status, WNOHANG);
            if (result == t->pid)
                t->reaped = true;
            else if (result < 0 && errno != EINTR)
                break;
            else
                poll(NULL, 0, 10);
        }
        ok = t->reaped && ok;
    }
    ok = close_fd(&t->master) && ok;
    ok = close_fd(&t->slave) && ok;
    for (size_t i = 0; i < 2; i++) {
        for (size_t j = 0; j < 2; j++)
            ok = close_fd(&t->pipes[i][j]) && ok;
        if (t->files[i])
            ok = fclose(t->files[i]) == 0 && ok;
        free(t->bytes[i]);
    }
    if (t->log) {
        if (fprintf(t->log, "reaped=%d wait_status=%d cleanup=%d\n", t->reaped, t->status, ok) < 0)
            ok = false;
        ok = fclose(t->log) == 0 && ok;
    }
    free(t->directory);
    free(t);
    return ok;
}

static FILE *artifact(terminal_t *t, const char *name) {
    size_t length = strlen(t->directory) + strlen(name) + 2;
    char *path = malloc(length);
    REQUIRE(t, path);
    snprintf(path, length, "%s/%s", t->directory, name);
    FILE *file = fopen(path, "wb");
    free(path);
    REQUIRE(t, file);
    /* Artifacts must not keep pipe endpoints or the PTY alive in the child. */
    if (fcntl(fileno(file), F_SETFD, FD_CLOEXEC) < 0) {
        fclose(file);
        REQUIRE(t, false);
    }
    return file;
}

static void pump(terminal_t *t) {
    for (size_t i = 0; i < 2; i++) {
        if (t->pipes[i][0] < 0)
            continue;
        unsigned char buffer[8192];
        /* Bound each iteration so a flooding child cannot starve deadlines. */
        for (unsigned turn = 0; turn < 16; turn++) {
            ssize_t count = read(t->pipes[i][0], buffer, sizeof(buffer));
            if (!count) {
                REQUIRE(t, close_fd(&t->pipes[i][0]));
                break;
            }
            if (count < 0) {
                if (errno == EINTR)
                    continue;
                REQUIRE(t, errno == EAGAIN || errno == EWOULDBLOCK);
                break;
            }
            REQUIRE(t, (size_t)count < SIZE_MAX - t->sizes[i]);
            unsigned char *bytes = realloc(t->bytes[i], t->sizes[i] + (size_t)count + 1);
            REQUIRE(t, bytes);
            t->bytes[i] = bytes;
            memcpy(bytes + t->sizes[i], buffer, (size_t)count);
            t->sizes[i] += (size_t)count;
            bytes[t->sizes[i]] = 0;
            REQUIRE(t, fwrite(buffer, 1, (size_t)count, t->files[i]) == (size_t)count);
            REQUIRE(t, fflush(t->files[i]) == 0);
        }
    }
    if (!t->reaped) {
        pid_t result = waitpid(t->pid, &t->status, WNOHANG);
        REQUIRE(t, result >= 0 || errno == EINTR);
        if (result == t->pid)
            t->reaped = true;
    }
}

static bool prompted(terminal_t *t) {
    static const char prompt[] = "Choose author order";
    for (size_t i = 0; i + sizeof(prompt) - 1 <= t->sizes[1]; i++)
        if (!memcmp(t->bytes[1] + i, prompt, sizeof(prompt) - 1))
            return true;
    return false;
}

static void tick(terminal_t *t, uint64_t deadline) {
    scenario_checkpoint(t->run);
    REQUIRE(t, child_monotonic_ms() < deadline);
    struct pollfd descriptors[2] = {{t->pipes[0][0], POLLIN, 0}, {t->pipes[1][0], POLLIN, 0}};
    int result = poll(descriptors, 2, 10);
    REQUIRE(t, result >= 0 || errno == EINTR);
}
#endif

scenario_command_result_t merge_terminal(scenario_run_t *run,
                                         const char *const *argv,
                                         scenario_body_t action,
                                         void *user,
                                         const void *response,
                                         size_t response_size) {
#ifdef _WIN32
    (void)argv;
    (void)action;
    (void)user;
    (void)response;
    (void)response_size;
    scenario_require(run, false, "real terminal scenarios require POSIX");
    return (scenario_command_result_t){0};
#else
    scenario_require(run, argv && argv[0] && (response || !response_size), "terminal arguments");
    terminal_t *t = calloc(1, sizeof(*t));
    scenario_require(run, t != NULL, "terminal allocation");
    t->run = run;
    t->master = t->slave = -1;
    for (size_t i = 0; i < 2; i++)
        t->pipes[i][0] = t->pipes[i][1] = -1;
    scenario_defer(run, cleanup, t);
    const char *work = scenario_work_directory(scenario_run_context(run));
    size_t length = strlen(work) + sizeof("/terminal-XXXXXX");
    t->directory = malloc(length);
    REQUIRE(t, t->directory);
    snprintf(t->directory, length, "%s/terminal-XXXXXX", work);
    REQUIRE(t, mkdtemp(t->directory));
    t->files[0] = artifact(t, "stdout.bin");
    t->files[1] = artifact(t, "stderr.bin");
    t->log = artifact(t, "command.log");
    for (size_t i = 0; argv[i]; i++)
        REQUIRE(t, fprintf(t->log, "argv[%zu]=%s\n", i, argv[i]) >= 0);
    REQUIRE(t, fflush(t->log) == 0);
    t->master = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    REQUIRE(t, t->master >= 0);
    REQUIRE(t, grantpt(t->master) == 0 && unlockpt(t->master) == 0);
    char *name = ptsname(t->master);
    REQUIRE(t, name);
    t->slave = open(name, O_RDWR | O_NOCTTY | O_CLOEXEC);
    REQUIRE(t, t->slave >= 0);
    struct termios attributes;
    REQUIRE(t, tcgetattr(t->slave, &attributes) == 0);
    attributes.c_lflag &= ~ECHO;
    REQUIRE(t, tcsetattr(t->slave, TCSANOW, &attributes) == 0);
    for (size_t i = 0; i < 2; i++) {
        REQUIRE(t, pipe(t->pipes[i]) == 0);
        REQUIRE(t, fcntl(t->pipes[i][0], F_SETFL, O_NONBLOCK) == 0);
        REQUIRE(t, fcntl(t->pipes[i][0], F_SETFD, FD_CLOEXEC) == 0);
        REQUIRE(t, fcntl(t->pipes[i][1], F_SETFD, FD_CLOEXEC) == 0);
    }
    t->pid = fork();
    REQUIRE(t, t->pid >= 0);
    if (!t->pid) {
        if (setpgid(0, 0) || chdir(work) || dup2(t->slave, STDIN_FILENO) < 0
            || dup2(t->pipes[0][1], STDOUT_FILENO) < 0 || dup2(t->pipes[1][1], STDERR_FILENO) < 0)
            _exit(126);
        close(t->master);
        close(t->slave);
        for (size_t i = 0; i < 2; i++) {
            close(t->pipes[i][0]);
            close(t->pipes[i][1]);
        }
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    REQUIRE(t, close_fd(&t->slave));
    for (size_t i = 0; i < 2; i++)
        REQUIRE(t, close_fd(&t->pipes[i][1]));
    uint64_t deadline = child_deadline_after(10000);
    while (!prompted(t)) {
        pump(t);
        if (prompted(t))
            break;
        REQUIRE(t, !t->reaped && t->pipes[1][0] >= 0);
        tick(t, deadline);
    }
    scenario_checkpoint(run);
    if (action)
        action(run, user);
    scenario_checkpoint(run);
    deadline = child_deadline_after(10000);
    REQUIRE(t, fcntl(t->master, F_SETFL, O_NONBLOCK) == 0);
    size_t sent = 0;
    while (sent < response_size || !t->reaped || t->pipes[0][0] >= 0 || t->pipes[1][0] >= 0) {
        pump(t);
        if (sent < response_size) {
            ssize_t count =
                write(t->master, (const unsigned char *)response + sent, response_size - sent);
            if (count > 0)
                sent += (size_t)count;
            else
                REQUIRE(t,
                        count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK));
        }
        if (sent == response_size && t->reaped && t->pipes[0][0] < 0 && t->pipes[1][0] < 0)
            break;
        tick(t, deadline);
    }
    REQUIRE(t, WIFEXITED(t->status));
    return (scenario_command_result_t){WEXITSTATUS(t->status),
                                       t->bytes[0],
                                       t->bytes[1],
                                       t->sizes[0],
                                       t->sizes[1]};
#endif
}
