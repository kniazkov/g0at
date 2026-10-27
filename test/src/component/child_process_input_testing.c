/** @file child_process_input_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent byte-stream fixtures for child environment and finite stdin.
 */
#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "lib/platform/windows_target.h"
#include "lib/process/child_process.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#    include <windows.h>
#else
#    include <unistd.h>
#endif

#define INPUT_SIZE (1024 * 1024 + 317)
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "Failed line %d: %s\n", __LINE__, #condition);                         \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static void pause_ms(unsigned ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

static bool environment_is(const char *value) {
#ifdef _WIN32
    wchar_t text[128];
    DWORD size = GetEnvironmentVariableW(L"GOAT_INPUT_TEST", text, 128);
    if (!value)
        return !size && GetLastError() == ERROR_ENVVAR_NOT_FOUND;
    if (!strcmp(value, "parent"))
        return size == 6 && !wcscmp(text, L"parent");
    return size && !wcscmp(text, L"\x041f\x0440\x0438\x0432\x0435\x0442 value");
#else
    const char *text = getenv("GOAT_INPUT_TEST");
    return value ? text && !strcmp(text, value) : !text;
#endif
}

static int worker(int argc, char **argv) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
    const char *mode = argv[2];
    if (!strcmp(mode, "env")) {
        CHECK(argc == 5 && !strcmp(argv[4], "Привет 😀 with spaces"));
        CHECK(environment_is(!strcmp(argv[3], "absent") ? NULL : argv[3]));
        if (!strcmp(argv[3], "Привет value")) {
#ifdef _WIN32
            wchar_t value[64];
            CHECK(GetEnvironmentVariableW(L"\x043a\x043b\x044e\x0447", value, 64) == 5);
            CHECK(!wcscmp(value, L"value"));
            SetLastError(ERROR_SUCCESS);
            CHECK(!GetEnvironmentVariableW(L"EMPTY", value, 64)
                  && GetLastError() != ERROR_ENVVAR_NOT_FOUND);
#else
            CHECK(getenv("ключ") && !strcmp(getenv("ключ"), "value"));
            CHECK(getenv("EMPTY") && !*getenv("EMPTY"));
#endif
        }

        return 0;
    }
    if (!strcmp(mode, "close")) {
        fclose(stdin);
        puts("closed");
        return 0;
    }
    if (!strcmp(mode, "ignore")) {
        pause_ms(10000);
        return 0;
    }
    if (!strcmp(mode, "empty")) {
        CHECK(getchar() == EOF && feof(stdin) && !ferror(stdin));
        return 0;
    }
    unsigned char block[4096];
    memset(block, 0xa5, sizeof(block));
    /* Fill both output pipes before reading input: sequential write-then-read deadlocks. */
    for (int n = 0; n < 64; n++) {
        CHECK(fwrite(block, 1, sizeof(block), stdout) == sizeof(block));
        CHECK(fwrite(block, 1, sizeof(block), stderr) == sizeof(block));
    }
    CHECK(!fflush(stdout) && !fflush(stderr));
    size_t offset = 0, count;
    while ((count = fread(block, 1, sizeof(block), stdin))) {
        for (size_t i = 0; i < count; i++)
            CHECK(block[i] == (unsigned char)((offset + i) * 37 + 11));
        offset += count;
    }
    CHECK(!ferror(stdin) && offset == INPUT_SIZE);
    return 0;
}

static int run_case(const char *name) {
    size_t baseline = get_allocated_memory_size();
#ifdef _WIN32
    CHECK(SetEnvironmentVariableW(L"GOAT_INPUT_TEST", L"parent"));
#else
    CHECK(!setenv("GOAT_INPUT_TEST", "parent", 1));
#endif
    const char *args[] = {"process input fixture", "--worker", "binary", NULL, NULL, NULL};
    const char *environment[] = {"GOAT_INPUT_TEST=Привет value", "EMPTY=", "ключ=value", NULL};
    const char *empty_environment[] = {NULL};
    child_options_t options = {.arguments = args,
                               .capture_output = true,
                               .deadline = child_deadline_after(10000)};
    if (!strcmp(name, "invalid")) {
        options.stdin_size = 1;
        CHECK(!start_child_process(&options));
        options.stdin_size = 0;
        const char *missing_equal[] = {"BAD", NULL};
        const char *empty_name[] = {"=value", NULL};
        const char *duplicate[] = {"A=1", "A=2", NULL};
#ifdef _WIN32
        const char *case_duplicate[] = {"Mixed=1", "MIXED=2", NULL};
        options.environment = case_duplicate;
        child_process_t *rejected = start_child_process(&options);
        if (rejected) {
            CHECK(wait_child_process(rejected)->status == CHILD_START_ERROR);
            destroy_child_process(rejected);
        }
#endif
        const char *const *invalid[] = {missing_equal, empty_name, duplicate};
        for (size_t i = 0; i < 3; i++) {
            options.environment = invalid[i];
            CHECK(!start_child_process(&options));
        }
        CHECK(get_allocated_memory_size() == baseline);
        return 0;
    }
    unsigned char *input = malloc(INPUT_SIZE);
    CHECK(input);
    for (size_t i = 0; i < INPUT_SIZE; i++)
        input[i] = (unsigned char)(i * 37 + 11);
    options.stdin_data = input;
    options.stdin_size = INPUT_SIZE;
    if (!strcmp(name, "env") || !strcmp(name, "inherit") || !strcmp(name, "empty-env")) {
        args[2] = "env";
        args[3] = !strcmp(name, "env")       ? "Привет value"
                  : !strcmp(name, "inherit") ? "parent"
                                             : "absent";
        args[4] = "Привет 😀 with spaces";
        options.environment = !strcmp(name, "env")         ? environment
                              : !strcmp(name, "empty-env") ? empty_environment
                                                           : NULL;
        options.stdin_size = 0;
    } else if (!strcmp(name, "empty")) {
        args[2] = "empty";
        options.stdin_size = 0;
    } else if (!strcmp(name, "early-close")) {
        args[2] = "close";
    } else if (!strcmp(name, "timeout") || !strcmp(name, "stop")) {
        args[2] = "ignore";
        options.deadline = !strcmp(name, "stop") ? 0 : child_deadline_after(150);
    } else if (!strcmp(name, "start-error")) {
        options.executable = "goat-definitely-missing-input-fixture";
    } else if (strcmp(name, "binary") && strcmp(name, "poll")) {
        free(input);
        return 2;
    }
    uint64_t start = child_monotonic_ms();
    child_process_t *child = start_child_process(&options);
    CHECK(child);
    /* start owns a snapshot, not the caller's storage. */
    memset(input, 0, INPUT_SIZE);
    free(input);
    const child_result_t *result;
    if (!strcmp(name, "poll")) {
        do {
            result = poll_child_process(child);
            if (result->status == CHILD_RUNNING)
                pause_ms(1);
        } while (result->status == CHILD_RUNNING);
    } else if (!strcmp(name, "stop")) {
        poll_child_process(child);
        result = stop_child_process(child);
    } else
        result = wait_child_process(child);
    bool ok;
    if (!strcmp(name, "stop"))
        ok = result->status == CHILD_STOPPED && child_monotonic_ms() - start < 8000;
    else if (!strcmp(name, "timeout"))
        ok = result->status == CHILD_TIMED_OUT && child_monotonic_ms() - start < 8000;
    else if (!strcmp(name, "start-error"))
        ok = result->status == CHILD_START_ERROR && result->system_error;
    else
        ok = result->status == CHILD_EXITED && !result->exit_code && !result->system_error;
    if (!strcmp(name, "binary") || !strcmp(name, "poll"))
        ok = ok && result->out.truncated && result->err.truncated
             && result->out.size == CHILD_OUTPUT_LIMIT && result->err.size == CHILD_OUTPUT_LIMIT;
    if (result->cleanup_error)
        ok = false;
    if (!ok)
        fprintf(stderr,
                "%s: status=%d exit=%lld error=%lu cleanup=%lu\n",
                name,
                result->status,
                (long long)result->exit_code,
                result->system_error,
                result->cleanup_error);
    if (!ok && result->err.size)
        fwrite(result->err.data, 1, result->err.size, stderr);
    destroy_child_process(child);
    CHECK(ok && environment_is("parent") && get_allocated_memory_size() == baseline);
    return 0;
}

int main(int argc, char **argv) {
    fs_error_t error = {0};
    if (!fs_arguments(&argc, &argv, &error))
        return 2;
    int code = argc > 2 && !strcmp(argv[1], "--worker") ? worker(argc, argv)
               : argc == 2                              ? run_case(argv[1])
                                                        : 2;
    free_fs_arguments(argc, argv);
    clear_fs_error(&error);
    return code;
}
