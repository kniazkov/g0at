/** @file child_process_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Controlled process fixtures; the Python harness supplies an external watchdog.
 */
#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif
#include "codegen/native_compiler.h"
#include "lib/allocate.h"
#include "lib/child_process.h"
#include "lib/windows_target.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#    include <windows.h>
#else
#    include <sys/wait.h>
#    include <unistd.h>
#endif
static void sleep_ms(unsigned ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

static void forever(void) {
    for (;;)
        sleep_ms(100);
}

static unsigned long identity(void) {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return (unsigned long)getpid();
#endif
}

static void report_pid(uintptr_t channel) {
#ifdef _WIN32
    if (!channel)
        channel = (uintptr_t)GetStdHandle(STD_OUTPUT_HANDLE);
#endif
    char text[64];
    int n = snprintf(text, sizeof(text), "%lu\n", identity());
    child_write_result(channel, text, (size_t)n);
}

/* Raw OS spawn deliberately bypasses the tested API: it must own even these descendants. */
static void descendant(uintptr_t result, uintptr_t ready, const char *level) {
#ifdef _WIN32
    char executable[32768], command[33000];
    GetModuleFileNameA(NULL, executable, sizeof(executable));
    snprintf(command,
             sizeof(command),
             "\"%s\" --worker %s %llu %llu",
             executable,
             level,
             (unsigned long long)result,
             (unsigned long long)ready);
    STARTUPINFOA startup = {.cb = sizeof(startup)};
    PROCESS_INFORMATION process = {0};
    if (!CreateProcessA(executable,
                        command,
                        NULL,
                        NULL,
                        TRUE,
                        CREATE_NO_WINDOW,
                        NULL,
                        NULL,
                        &startup,
                        &process))
        exit(80);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
#else
    pid_t pid = fork();
    if (pid < 0)
        exit(80);
    if (!pid) {
        setsid(); /* Exercise adoption outside the original process group. */
        report_pid(result);
        if (!strcmp(level, "branch"))
            descendant(result, ready, "leaf");
        child_write_result(ready, "r", 1);
        forever();
    }
#endif
}

static int worker(int argc, char **argv) {
    const char *mode = argv[2];
    uintptr_t channel = argc > 3 ? (uintptr_t)strtoull(argv[3], NULL, 10) : 0;
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    if (!strcmp(mode, "exit"))
        return 23;
    if (!strcmp(mode, "crash")) {
        report_pid(channel);
        raise(SIGABRT);
        return 99;
    }
    if (!strcmp(mode, "stdin")) {
        int value = getchar();
        return value == EOF && feof(stdin) && !ferror(stdin) ? 0 : 90;
    }
    if (!strcmp(mode, "inherit"))
        return getchar() == 'Q' ? 0 : 91;
    if (!strcmp(mode, "args")) {
        for (int i = 4; i < argc; i++)
            child_write_result(channel, argv[i], strlen(argv[i]) + 1);
        return 0;
    }
    if (!strcmp(mode, "watch")) {
        puts("WATCH_READY");
        fflush(stdout);
        if (!child_wait_parent(channel, child_deadline_after(5000)))
            return 93;
        puts("PARENT_GONE");
        fflush(stdout);
        child_close_channel(channel);
        return 0;
    }
    if (!strcmp(mode, "parent")) {
        bool ended = child_wait_parent(channel, child_deadline_after(30));
        child_close_channel(channel);
        return ended ? 92 : 0;
    }
    if (!strcmp(mode, "delay")) {
        sleep_ms(200);
        return 0;
    }
    if (!strcmp(mode, "leaf") || !strcmp(mode, "branch")) {
        uintptr_t ready = (uintptr_t)strtoull(argv[4], NULL, 10);
        report_pid(channel);
        if (!strcmp(mode, "branch"))
            descendant(channel, ready, "leaf");
        child_write_result(ready, "r", 1);
        forever();
    }
    if (!strncmp(mode, "tree", 4)) {
        uintptr_t read_ready, write_ready;
#ifdef _WIN32
        HANDLE r, w;
        SECURITY_ATTRIBUTES sa = {.nLength = sizeof(sa), .bInheritHandle = TRUE};
        if (!CreatePipe(&r, &w, &sa, 0))
            return 81;
        read_ready = (uintptr_t)r;
        write_ready = (uintptr_t)w;
#else
        int ready[2];
        if (pipe(ready))
            return 81;
        read_ready = ready[0];
        write_ready = ready[1];
#endif
        report_pid(channel);
        descendant(channel, write_ready, "branch");
        for (int i = 0; i < 2; i++) {
            char byte;
#ifdef _WIN32
            DWORD got;
            if (!ReadFile((HANDLE)read_ready, &byte, 1, &got, NULL) || got != 1)
                return 82;
#else
            if (read((int)read_ready, &byte, 1) != 1)
                return 82;
#endif
        }
        child_close_channel(read_ready);
        child_close_channel(write_ready);
        if (!strcmp(mode, "tree-exit"))
            return 0;
        if (!strcmp(mode, "tree-fail"))
            return 23;
        if (!strcmp(mode, "tree-crash")) {
            raise(SIGABRT);
            return 99;
        }
        /* Explicit readiness for the external owner-death test, after both descendants exist. */
        puts("READY");
        fflush(stdout);
        forever();
    }
    if (!strcmp(mode, "pause")) {
        report_pid(channel);
        forever();
    }
    size_t count = !strcmp(mode, "empty")   ? 0
                   : !strcmp(mode, "exact") ? 65536
                   : !strcmp(mode, "over")  ? 65537
                   : !strcmp(mode, "large") ? 1048576
                                            : SIZE_MAX;
    unsigned char block[4096];
    for (size_t i = 0; i < sizeof(block); i++)
        block[i] = (unsigned char)i;
    size_t sent = 0;
    while (sent < count) {
        size_t amount = count - sent < sizeof(block) ? count - sent : sizeof(block);
        if (fwrite(block, 1, amount, stdout) != amount
            || fwrite(block, 1, amount, stderr) != amount)
            return 83;
        fflush(stdout);
        fflush(stderr);
        sent += amount;
    }
    return 0;
}

static void print_hex(const child_output_t *out) {
    for (size_t i = 0; i < out->size; i++)
        printf("%02x", out->data[i]);
}

static int fixture_main(int argc, char **argv) {
    if (argc > 2 && !strcmp(argv[1], "--worker"))
        return worker(argc, argv);
    if (argc < 2)
        return 2;
    if (!strcmp(argv[1], "compiler-probe")) {
        const char *commands[] = {argv[0], "--worker", "continuous", NULL};
        native_compile_result_t result = {.exit_code = -1};
        native_compiler_run(&result, commands, child_deadline_after(150));
        bool ok = result.status == NATIVE_COMPILE_TIMED_OUT && result.diagnostics_length
                  && !result.cleanup_error && !result.windows_cleanup_error;
        destroy_native_compile_result(&result);
        commands[2] = "delay";
        native_compiler_run(&result, commands, 0);
        ok = ok && result.status == NATIVE_COMPILE_OK && !result.exit_code;
        destroy_native_compile_result(&result);
        return ok && !get_allocated_memory_size() ? 0 : 94;
    }
    const char *mode = argv[1];
    const char *args[] = {"process fixture",
                          "--worker",
                          mode,
                          CHILD_RESULT_ARGUMENT,
                          "",
                          "a b",
                          "a\"b",
                          "tail\\",
                          "&%x%!",
                          "Привет😀",
                          NULL};
    child_options_t options = {.arguments = args,
                               .empty_stdin = true,
                               .capture_output = true,
                               .result_channel = true,
                               .deadline = child_deadline_after(5000)};
    if (!strcmp(mode, "missing"))
        options.executable = "goat-process-executable-does-not-exist";
    if (!strcmp(mode, "parent")) {
        args[3] = CHILD_PARENT_ARGUMENT;
        options.parent_watch = true;
    }
    if (!strcmp(mode, "inherit"))
        options.empty_stdin = false;
    if (!strcmp(mode, "closed-input")) {
        args[2] = "stdin";
#ifdef _WIN32
        _close(0);
        SetStdHandle(STD_INPUT_HANDLE, NULL);
#else
        close(0);
#endif
    }
    if (!strcmp(mode, "continuous") || !strcmp(mode, "tree") || !strcmp(mode, "pause"))
        options.deadline = child_deadline_after(150);
    if (!strcmp(mode, "expired"))
        options.deadline = 1;
    if (!strcmp(mode, "owner") || !strcmp(mode, "launch-owner")) {
        args[2] = "tree";
        options.deadline = 0;
        options.capture_output = false;
        /* Descendant IDs go to stdout, allowing the harness to observe before killing owner. */
        options.result_channel = false;
#ifdef _WIN32
        /* The API duplicates stdout, so use workers' actual stdout instead of inherited numeric
         * value. */
        args[3] = "0";
#else
        args[3] = "1";
#endif
    }
    if (!strcmp(mode, "watch-owner")) {
        args[2] = "watch";
        args[3] = CHILD_PARENT_ARGUMENT;
        options.parent_watch = true;
        options.survive_parent = true;
        options.capture_output = false;
        options.deadline = 0;
    }
    if (!strcmp(mode, "stop")) {
        args[2] = "pause";
        options.deadline = 0;
    }
    if (!strcmp(mode, "budget")) {
        args[2] = "delay";
        options.deadline = child_deadline_after(350);
    }
    uint64_t start = child_monotonic_ms();
    if (!strcmp(mode, "launch-owner")) {
        puts("LAUNCHING");
        fflush(stdout);
    }
    child_process_t *child = start_child_process(&options);
    if (!child)
        return 3;
    if (!strcmp(mode, "owner") || !strcmp(mode, "launch-owner") || !strcmp(mode, "watch-owner"))
        forever();
    const child_result_t *result =
        !strcmp(mode, "stop") ? stop_child_process(child) : wait_child_process(child);
    if (!strcmp(mode, "budget")) {
        if (result->status != CHILD_EXITED || result->exit_code)
            return 4;
        destroy_child_process(child);
        child = start_child_process(&options);
        result = wait_child_process(child);
    }
    int parent_input = !strcmp(mode, "stdin") ? getchar() : -1;
    printf("{\"parentInput\":%d,", parent_input);
    printf("\"status\":%d,\"code\":%lld,\"signal\":%d,\"error\":%lu,\"cleanup\":%lu,\"ms\":%llu,",
           result->status,
           (long long)result->exit_code,
           result->signal_number,
           result->system_error,
           result->cleanup_error,
           (unsigned long long)(child_monotonic_ms() - start));
    printf("\"outTruncated\":%s,\"errTruncated\":%s,\"out\":\"",
           result->out.truncated ? "true" : "false",
           result->err.truncated ? "true" : "false");
    print_hex(&result->out);
    printf("\",\"err\":\"");
    print_hex(&result->err);
    printf("\",\"result\":\"");
    print_hex(&result->result);
    puts("\"}");
    destroy_child_process(child);
    return get_allocated_memory_size() ? 5 : 0;
}

#ifdef _WIN32
/* The fixture observes UTF-16 arguments without the CRT's lossy ANSI argv conversion. */
int wmain(int argc, wchar_t **wide_args) {
    char **args = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ((size_t)argc + 1) * sizeof(*args));
    if (!args)
        return 95;
    for (int i = 0; i < argc; i++) {
        int size = WideCharToMultiByte(CP_UTF8,
                                       WC_ERR_INVALID_CHARS,
                                       wide_args[i],
                                       -1,
                                       NULL,
                                       0,
                                       NULL,
                                       NULL);
        if (!size)
            return 96;
        args[i] = HeapAlloc(GetProcessHeap(), 0, size);
        if (!args[i]
            || !WideCharToMultiByte(CP_UTF8,
                                    WC_ERR_INVALID_CHARS,
                                    wide_args[i],
                                    -1,
                                    args[i],
                                    size,
                                    NULL,
                                    NULL))
            return 97;
    }
    int result = fixture_main(argc, args);
    for (int i = 0; i < argc; i++)
        HeapFree(GetProcessHeap(), 0, args[i]);
    HeapFree(GetProcessHeap(), 0, args);
    return result;
}
#else
int main(int argc, char **argv) {
    return fixture_main(argc, argv);
}
#endif
