/** @file process_channel_linux.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Nonblocking local sockets; no SIGPIPE and no inherited unrelated descriptors.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "process_channel.h"
#ifdef __linux__
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

bool create_process_channel(uintptr_t pair[2]) {
    pair[0] = pair[1] = 0;
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, fds))
        return false;
    for (int i = 0; i < 2; i++) {
        if (fds[i] < 3) {
            int copy = fcntl(fds[i], F_DUPFD_CLOEXEC, 3);
            close(fds[i]);
            fds[i] = copy;
        }
    }
    if (fds[0] < 0 || fds[1] < 0) {
        if (fds[0] >= 0)
            close(fds[0]);
        if (fds[1] >= 0)
            close(fds[1]);
        return false;
    }
    pair[0] = (uintptr_t)fds[0];
    pair[1] = (uintptr_t)fds[1];
    return true;
}

static channel_status_t ready(uintptr_t channel, short events, child_deadline_t deadline) {
    if (!channel)
        return CHANNEL_ERROR;
    for (;;) {
        uint64_t now = child_monotonic_ms();
        if (deadline && now >= deadline)
            return CHANNEL_TIMEOUT;
        int timeout = !deadline ? -1 : deadline - now > 1000 ? 1000 : (int)(deadline - now);
        struct pollfd fd = {.fd = (int)channel, .events = events};
        int n = poll(&fd, 1, timeout);
        if (n > 0) {
            if (fd.revents & events)
                return CHANNEL_OK;
            return fd.revents & POLLHUP ? CHANNEL_CLOSED : CHANNEL_ERROR;
        }
        if (n < 0 && errno != EINTR)
            return CHANNEL_ERROR;
    }
}

channel_status_t wait_process_channel(uintptr_t channel, child_deadline_t deadline) {
    return ready(channel, POLLIN, deadline);
}

static channel_status_t
transfer(uintptr_t channel, void *data, size_t size, child_deadline_t deadline, bool writing) {
    unsigned char *bytes = data;
    while (size) {
        channel_status_t status = ready(channel, writing ? POLLOUT : POLLIN, deadline);
        if (status != CHANNEL_OK)
            return status;
        ssize_t n = writing ? send((int)channel, bytes, size, MSG_NOSIGNAL)
                            : recv((int)channel, bytes, size, 0);
        if (n < 0 && (errno == EINTR || errno == EAGAIN))
            continue;
        if (n <= 0)
            return n == 0 || errno == EPIPE || errno == ECONNRESET ? CHANNEL_CLOSED : CHANNEL_ERROR;
        bytes += n;
        size -= (size_t)n;
    }
    return CHANNEL_OK;
}

channel_status_t
read_process_channel(uintptr_t channel, void *data, size_t size, child_deadline_t deadline) {
    return transfer(channel, data, size, deadline, false);
}

channel_status_t
write_process_channel(uintptr_t channel, const void *data, size_t size, child_deadline_t deadline) {
    return transfer(channel, (void *)data, size, deadline, true);
}

void close_process_channel(uintptr_t channel) {
    if (channel)
        close((int)channel);
}

void process_pause(unsigned milliseconds) {
    poll(NULL, 0, (int)milliseconds);
}

int child_parent_state(uintptr_t watch) {
    struct pollfd fd = {.fd = (int)watch, .events = POLLIN};
    int n;
    do {
        n = poll(&fd, 1, 0);
    } while (n < 0 && errno == EINTR);
    if (n < 0 || fd.revents & (POLLNVAL | POLLERR))
        return -1;
    return (fd.revents & POLLIN) ? 1 : 0;
}
#endif
