/** @file process_channel_windows.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Overlapped duplex pipes, with bounded send as well as receive.
 */
#include "process_channel.h"
#include "windows_target.h"
#ifdef _WIN32
#    include <bcrypt.h>
#    include <stdio.h>
#    include <windows.h>

bool create_process_channel(uintptr_t pair[2]) {
    pair[0] = pair[1] = 0;
    unsigned char random[16];
    if (BCryptGenRandom(NULL, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG))
        return false;
    wchar_t name[100];
    wchar_t *p = name;
    const wchar_t *prefix = L"\\\\.\\pipe\\goat-service-";
    while (*prefix)
        *p++ = *prefix++;
    for (unsigned i = 0; i < sizeof(random); i++) {
        *p++ = L"0123456789abcdef"[random[i] >> 4];
        *p++ = L"0123456789abcdef"[random[i] & 15];
    }
    *p = 0;
    HANDLE server = CreateNamedPipeW(
        name,
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1,
        4096,
        4096,
        0,
        NULL);
    if (server == INVALID_HANDLE_VALUE)
        return false;
    HANDLE client = CreateFileW(name,
                                GENERIC_READ | GENERIC_WRITE,
                                0,
                                NULL,
                                OPEN_EXISTING,
                                FILE_FLAG_OVERLAPPED,
                                NULL);
    if (client == INVALID_HANDLE_VALUE) {
        CloseHandle(server);
        return false;
    }
    OVERLAPPED connect = {0};
    connect.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!connect.hEvent) {
        CloseHandle(client);
        CloseHandle(server);
        return false;
    }
    BOOL connected = ConnectNamedPipe(server, &connect);
    DWORD error = connected ? 0 : GetLastError();
    if (!connected && error == ERROR_IO_PENDING) {
        CancelIoEx(server, &connect);
        DWORD ignored;
        GetOverlappedResult(server, &connect, &ignored, TRUE);
    }
    CloseHandle(connect.hEvent);
    if (!connected && error != ERROR_PIPE_CONNECTED) {
        CloseHandle(client);
        CloseHandle(server);
        return false;
    }
    pair[0] = (uintptr_t)server;
    pair[1] = (uintptr_t)client;
    return true;
}

static channel_status_t pipe_error(DWORD error) {
    return error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED ? CHANNEL_CLOSED
                                                                           : CHANNEL_ERROR;
}

channel_status_t wait_process_channel(uintptr_t channel, child_deadline_t deadline) {
    for (;;) {
        if (deadline && child_monotonic_ms() >= deadline)
            return CHANNEL_TIMEOUT;
        DWORD available;
        if (!PeekNamedPipe((HANDLE)channel, NULL, 0, NULL, &available, NULL))
            return pipe_error(GetLastError());
        if (available)
            return CHANNEL_OK;
        Sleep(1);
    }
}

static channel_status_t
transfer(uintptr_t channel, void *data, size_t size, child_deadline_t deadline, bool writing) {
    unsigned char *bytes = data;
    while (size) {
        uint64_t now = child_monotonic_ms();
        if (deadline && now >= deadline)
            return CHANNEL_TIMEOUT;
        OVERLAPPED op = {0};
        op.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (!op.hEvent)
            return CHANNEL_ERROR;
        DWORD n = 0, amount = size > 4096 ? 4096 : (DWORD)size;
        BOOL ok = writing ? WriteFile((HANDLE)channel, bytes, amount, &n, &op)
                          : ReadFile((HANDLE)channel, bytes, amount, &n, &op);
        DWORD error = ok ? 0 : GetLastError();
        bool timed_out = false;
        if (!ok && error == ERROR_IO_PENDING) {
            for (;;) {
                now = child_monotonic_ms();
                if (deadline && now >= deadline) {
                    timed_out = true;
                    break;
                }
                DWORD timeout = !deadline               ? INFINITE
                                : deadline - now > 1000 ? 1000
                                                        : (DWORD)(deadline - now);
                DWORD state = WaitForSingleObject(op.hEvent, timeout);
                if (state == WAIT_OBJECT_0)
                    break;
                if (state == WAIT_FAILED) {
                    error = GetLastError();
                    break;
                }
            }
            if (timed_out || error != ERROR_IO_PENDING)
                CancelIoEx((HANDLE)channel, &op);
            ok = GetOverlappedResult((HANDLE)channel, &op, &n, TRUE);
            error = ok ? 0 : GetLastError();
        }
        CloseHandle(op.hEvent);
        if (timed_out)
            return CHANNEL_TIMEOUT;
        if (!ok)
            return pipe_error(error);
        if (!n)
            return CHANNEL_CLOSED;
        bytes += n;
        size -= n;
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
        CloseHandle((HANDLE)channel);
}

void process_pause(unsigned milliseconds) {
    Sleep(milliseconds);
}

int child_parent_state(uintptr_t watch) {
    DWORD state = WaitForSingleObject((HANDLE)watch, 0);
    return state == WAIT_OBJECT_0 ? 1 : state == WAIT_TIMEOUT ? 0 : -1;
}
#endif
