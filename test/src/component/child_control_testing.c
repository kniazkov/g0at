/** @file child_control_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Deterministic supervisor reply faults, after the child tree was cleaned.
 */
#ifdef NDEBUG
#    undef NDEBUG /* Fixture operations and checks must execute in Release too. */
#endif
#include "lib/memory/allocate.h"
#include "lib/process/child_process.h"
#include "lib/process/cleanup_service.h"

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *mode;

/* Linked only into the dedicated fixture, never enabled via production environment. */
ssize_t test_control_send(int fd, const void *data, size_t size, int flags) {
    static unsigned calls;
    ++calls;
    if (!strcmp(mode, "eintr") && calls == 1) {
        errno = EINTR;
        return -1;
    }
    if (!strcmp(mode, "send-error")) {
        errno = EIO;
        return -1;
    }
    if (!strcmp(mode, "eof"))
        _exit(23);
    if (!strcmp(mode, "signal")) {
        kill(getpid(), SIGKILL);
        _exit(24);
    }
    if (!strcmp(mode, "short"))
        return send(fd, data, 1, flags);
    return send(fd, data, size, flags);
}

ssize_t test_control_recv(int fd, void *data, size_t size, int flags) {
    ssize_t received = recv(fd, data, size, flags);
    if (received > 0 && !strcmp(mode, "receive-error")) {
        errno = ECONNRESET;
        return -1;
    }
    return received;
}

int main(int argc, char **argv) {
    mode = "ok";
    if (argc > 1 && !strcmp(argv[1], CLEANUP_SERVICE_ARGUMENT)) {
        mode = getenv("GOAT_FIXTURE_CONTROL_MODE");
        assert(mode);
        int code;
        assert(cleanup_service_main(argc, argv, &code));
        return code;
    }
    assert(argc == 2 || (argc == 3 && !strcmp(argv[2], "service")));
    bool through_service = argc == 3;
    mode = argv[1];
    size_t before = get_allocated_memory_size();
    const char *args[] = {"/bin/true", NULL};
    child_options_t options = {
        .executable = !strcmp(mode, "launch-error") ? "/no-such-goat-test-program" : args[0],
        .arguments = args,
        .capture_output = true,
        .deadline = child_deadline_after(5000)};
    child_process_t *process = NULL;
    cleanup_service_t *service = NULL;
    child_result_t service_result;
    const child_result_t *result;
    if (through_service) {
        assert(!setenv("GOAT_FIXTURE_CONTROL_MODE", mode, 1));
        mode = "ok"; // The supervisor launching the service itself must remain healthy.
        service = create_cleanup_service(NULL);
        assert(service);
        uint64_t started = child_monotonic_ms();
        cleanup_status_t startup = ensure_cleanup_service(service);
        if (startup != CLEANUP_OK)
            fprintf(stderr,
                    "cleanup startup failed: mode=%s status=%d elapsed_ms=%llu\n",
                    argv[1],
                    (int)startup,
                    (unsigned long long)(child_monotonic_ms() - started));
        assert(startup == CLEANUP_OK);
        assert(!unsetenv("GOAT_FIXTURE_CONTROL_MODE"));
        options.capture_output = false; // Service captures output by contract.
        assert(run_cleanup_process(service, &options, &service_result) == CLEANUP_OK);
        result = &service_result;
        mode = argv[1];
    } else {
        process = start_child_process(&options);
        assert(process);
        result = wait_child_process(process);
    }
    printf("mode=%s status=%d control=%lu cleanup=%lu reaped=%d supervisor=%d\n",
           mode,
           result->status,
           result->control_error,
           result->cleanup_error,
           result->supervisor_reaped,
           result->supervisor_status);
    if (!strcmp(mode, "ok") || !strcmp(mode, "eintr")) {
        assert(result->status == CHILD_EXITED && result->exit_code == 0);
        assert(!result->control_error && !result->cleanup_error);
        assert(result->supervisor_reaped && WIFEXITED(result->supervisor_status));
        assert(WEXITSTATUS(result->supervisor_status) == 0);
    } else if (!strcmp(mode, "launch-error")) {
        assert(result->status == CHILD_START_ERROR && result->system_error == ENOENT);
        assert(!result->control_error && !result->cleanup_error);
    } else {
        assert(result->status == CHILD_IO_ERROR && result->cleanup_error == EIO);
        unsigned long expected = !strcmp(mode, "short")           ? EPROTO
                                 : !strcmp(mode, "receive-error") ? ECONNRESET
                                                                  : EPIPE;
        assert(result->control_error == expected);
        // A nonblocking diagnostic sample may precede actual supervisor exit.
        if (result->supervisor_reaped && !strcmp(mode, "signal"))
            assert(WIFSIGNALED(result->supervisor_status)
                   && WTERMSIG(result->supervisor_status) == SIGKILL);
        if (result->supervisor_reaped && !strcmp(mode, "eof"))
            assert(WIFEXITED(result->supervisor_status)
                   && WEXITSTATUS(result->supervisor_status) == 23);
    }
    if (through_service) {
        mode = "ok";
        cleanup_result_t finished = finish_cleanup_service(service);
        assert(finished.status == (result->cleanup_error ? CLEANUP_TREE_FAILED : CLEANUP_OK));
        assert(finish_cleanup_service(service).status == finished.status);
        destroy_cleanup_service(service);
    } else {
        assert(wait_child_process(process) == result); // stable failure, no replay
        destroy_child_process(process);
    }
    assert(get_allocated_memory_size() == before);
    return 0;
}
