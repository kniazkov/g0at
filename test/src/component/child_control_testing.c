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
#include "lib/process/process_channel.h"

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

static void print_startup(cleanup_service_t *service) {
    char evidence[384];
    cleanup_service_format_startup(service, evidence, sizeof(evidence));
    fprintf(stderr, "startup %s\n", evidence);
}

static int startup_child(const char *fault, char **argv) {
    uintptr_t channel = (uintptr_t)strtoull(argv[3], NULL, 10);
    child_deadline_t deadline = strtoull(argv[4], NULL, 10);
    if (!strcmp(fault, "timeout")) {
        /* Keep the channel open until owner death; exiting at the deadline races EOF
         * against timeout and would not deterministically test CHANNEL_TIMEOUT. */
        uintptr_t watch = (uintptr_t)strtoull(argv[2], NULL, 10);
        assert(child_wait_parent(watch, 0));
        return 0;
    }
    if (!strcmp(fault, "exit"))
        return 19;
    uint32_t frame[] = {0x47544331u, 1, 6};
    const char *body = "/test";
    if (!strcmp(fault, "magic"))
        frame[0] = 0;
    else if (!strcmp(fault, "type"))
        frame[1] = 99;
    else if (!strcmp(fault, "path"))
        body = "/a\0bc";
    else
        assert(!strcmp(fault, "body"));
    assert(write_process_channel(channel, frame, sizeof(frame), deadline) == CHANNEL_OK);
    if (strcmp(fault, "body") && strcmp(fault, "magic"))
        assert(write_process_channel(channel, body, 6, deadline) == CHANNEL_OK);
    close_process_channel(channel);
    return 0;
}

static int startup_test(const char *fault) {
    size_t before = get_allocated_memory_size();
    assert(!setenv("GOAT_FIXTURE_STARTUP", fault, 1));
    cleanup_service_t *service =
        create_cleanup_service(!strcmp(fault, "missing") ? "/no-such-goat-test-program" : NULL);
    assert(service);
    cleanup_start_diagnostic_t d = {0}, again = {0};
    assert(!cleanup_service_startup_diagnostic(service, &d));
    cleanup_status_t status = ensure_cleanup_service(service);
    assert(!unsetenv("GOAT_FIXTURE_STARTUP"));
    print_startup(service);
    assert(cleanup_service_startup_diagnostic(service, &d));
    assert(d.status == status);
    if (!strcmp(fault, "timeout") || !strcmp(fault, "ok"))
        assert(d.process.status == CHILD_RUNNING && d.process.exit_code == -1);
    if (!strcmp(fault, "missing") && d.process.status != CHILD_RUNNING)
        assert(d.process.status == CHILD_START_ERROR && d.process.system_error == ENOENT);
    if (!strcmp(fault, "exit") && d.process.status != CHILD_RUNNING)
        assert(d.process.status == CHILD_EXITED && d.process.exit_code == 19);
    if (!strcmp(fault, "ok")) {
        assert(status == CLEANUP_OK && d.stage == CLEANUP_START_READY);
        assert(d.transport == CHANNEL_OK);
        assert(finish_cleanup_service(service).status == CLEANUP_OK);
    } else {
        assert(status == (!strcmp(fault, "timeout") ? CLEANUP_TIMEOUT : CLEANUP_START_FAILED));
        cleanup_start_stage_t stage = !strcmp(fault, "body") ? CLEANUP_START_BODY
                                      : (!strcmp(fault, "type") || !strcmp(fault, "path"))
                                          ? CLEANUP_START_VALIDATE
                                          : CLEANUP_START_HEADER;
        int transport = !strcmp(fault, "timeout")         ? CHANNEL_TIMEOUT
                        : !strcmp(fault, "magic")         ? CHANNEL_ERROR
                        : stage == CLEANUP_START_VALIDATE ? CHANNEL_OK
                                                          : CHANNEL_CLOSED;
        assert(d.stage == stage && d.transport == transport);
        if (stage == CLEANUP_START_BODY || stage == CLEANUP_START_VALIDATE)
            assert(d.reply_size == 6 && d.reply_type == (!strcmp(fault, "type") ? 99u : 1u));
        assert(ensure_cleanup_service(service) == status);
    }
    assert(cleanup_service_startup_diagnostic(service, &again));
    assert(d.status == again.status && d.stage == again.stage && d.transport == again.transport
           && d.elapsed_ms == again.elapsed_ms && d.reply_type == again.reply_type
           && d.reply_size == again.reply_size);
    assert(d.process.status == again.process.status
           && d.process.exit_code == again.process.exit_code
           && d.process.signal_number == again.process.signal_number
           && d.process.system_error == again.process.system_error
           && d.process.control_error == again.process.control_error);
    destroy_cleanup_service(service);
    assert(get_allocated_memory_size() == before);
    return 0;
}

int main(int argc, char **argv) {
    mode = "ok";
    if (argc == 2 && !strcmp(argv[1], "fixture-signal")) {
        raise(SIGTERM);
        return 99;
    }
    if (argc > 1 && !strcmp(argv[1], CLEANUP_SERVICE_ARGUMENT)) {
        const char *startup = getenv("GOAT_FIXTURE_STARTUP");
        if (startup && strcmp(startup, "ok"))
            return startup_child(startup, argv);
        if (startup) {
            int code;
            assert(cleanup_service_main(argc, argv, &code));
            return code;
        }
        mode = getenv("GOAT_FIXTURE_CONTROL_MODE");
        assert(mode);
        int code;
        assert(cleanup_service_main(argc, argv, &code));
        return code;
    }
    if (argc == 3 && !strcmp(argv[2], "startup"))
        return startup_test(argv[1]);
    assert(argc == 2 || (argc == 3 && !strcmp(argv[2], "service")));
    bool through_service = argc == 3;
    mode = argv[1];
    size_t before = get_allocated_memory_size();
    bool child_signal = !strcmp(mode, "child-signal");
    const char *args[] = {"/bin/true", child_signal ? "fixture-signal" : NULL, NULL};
    child_options_t options = {.executable = child_signal ? NULL
                                             : !strcmp(mode, "launch-error")
                                                 ? "/no-such-goat-test-program"
                                                 : args[0],
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
        if (startup != CLEANUP_OK)
            print_startup(service);
        assert(startup == CLEANUP_OK);
        assert(!unsetenv("GOAT_FIXTURE_CONTROL_MODE"));
        options.capture_output = false; // Service captures output by contract.
        assert(run_cleanup_process(service, &options, &service_result) == CLEANUP_OK);
        result = &service_result;
        mode = argv[1];
    } else {
        process = start_child_process(&options);
        assert(process);
        child_observation_t observed;
        child_deadline_t end = child_deadline_after(5000);
        do {
            observed = observe_child_process(process);
            if (observed.status != CHILD_RUNNING)
                break;
            process_pause(1);
        } while (child_monotonic_ms() < end);
        assert(observed.status != CHILD_RUNNING);
        child_observation_t repeated = observe_child_process(process);
        assert(repeated.status == observed.status && repeated.exit_code == observed.exit_code
               && repeated.signal_number == observed.signal_number
               && repeated.system_error == observed.system_error
               && repeated.control_error == observed.control_error);
        result = wait_child_process(process);
        if (!strcmp(mode, "receive-error")) {
            // The receive-error hook affects consuming recv only; peek sees the intact reply.
            assert(observed.status == CHILD_EXITED && observed.exit_code == 0);
        } else {
            assert(result->status == observed.status && result->exit_code == observed.exit_code
                   && result->signal_number == observed.signal_number
                   && result->system_error == observed.system_error
                   && result->control_error == observed.control_error);
        }
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
    } else if (child_signal) {
        assert(result->status == CHILD_SIGNALED && result->signal_number == SIGTERM);
        assert(!result->cleanup_error && !result->control_error);
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
