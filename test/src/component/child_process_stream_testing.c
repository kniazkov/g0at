/** @file child_process_stream_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent interactive and full-output fixtures for child processes.
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
#define TOTAL (1024 * 1024 + 317)
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "Failed line %d: %s\n", __LINE__, #x);                                 \
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

static unsigned char expected(size_t offset, bool err) {
    return (unsigned char)(offset * (err ? 53 : 37) + (err ? 19 : 11));
}

static int worker(const char *mode) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
    if (!strcmp(mode, "idle")) {
        pause_ms(10000);
        return 0;
    }
    if (!strcmp(mode, "dialog")) {
        CHECK(fwrite("READY\0\n", 1, 7, stdout) == 7 && !fflush(stdout));
        unsigned char reply[6];
        CHECK(fread(reply, 1, sizeof(reply), stdin) == sizeof(reply));
        CHECK(!memcmp(reply, "OK\0YES", sizeof(reply)));
        CHECK(getchar() == EOF && !ferror(stdin));
        CHECK(fwrite("DONE\0", 1, 5, stdout) == 5 && !fflush(stdout));
        return 0;
    }
    if (!strcmp(mode, "roundtrip")) {
        unsigned char data[4096];
        size_t offset = 0, count;
        while ((count = fread(data, 1, sizeof(data), stdin))) {
            for (size_t i = 0; i < count; i++)
                CHECK(data[i] == expected(offset + i, false));
            CHECK(fwrite(data, 1, count, stdout) == count && !fflush(stdout));
            for (size_t i = 0; i < count; i++)
                data[i] = expected(offset + i, true);
            CHECK(fwrite(data, 1, count, stderr) == count && !fflush(stderr));
            offset += count;
        }
        CHECK(!ferror(stdin) && offset == TOTAL);
        return 0;
    }
    for (size_t offset = 0; offset < TOTAL;) {
        unsigned char out[4096], err[4096];
        size_t n = TOTAL - offset;
        if (n > sizeof(out))
            n = sizeof(out);
        for (size_t i = 0; i < n; i++) {
            out[i] = expected(offset + i, false);
            err[i] = expected(offset + i, true);
        }
        CHECK(fwrite(out, 1, n, stdout) == n && !fflush(stdout));
        CHECK(fwrite(err, 1, n, stderr) == n && !fflush(stderr));
        offset += n;
    }
    return 0;
}

typedef struct {
    size_t out, err, input_calls, output_calls;
    bool invalid, ready, supplied, fail_output, fail_input, invalid_size;
    unsigned char dialog[12];
    bool interactive, roundtrip;
    size_t input_offset;
    FILE *out_file, *err_file;
} context_t;

static bool accept_stream_data(context_t *ctx, const unsigned char *data, size_t size, bool err) {
    ctx->output_calls++;
    if (ctx->fail_output)
        return false;
    size_t *offset = err ? &ctx->err : &ctx->out;
    if (ctx->interactive) {
        if (err || *offset + size > sizeof(ctx->dialog))
            return false;
        memcpy(ctx->dialog + *offset, data, size);
        *offset += size;
        if (*offset >= 7)
            ctx->ready = !memcmp(ctx->dialog, "READY\0\n", 7);
        return true;
    }
    for (size_t i = 0; i < size; i++)
        if (data[i] != expected(*offset + i, err))
            ctx->invalid = true;
    *offset += size;
    FILE *file = err ? ctx->err_file : ctx->out_file;
    return !file || fwrite(data, 1, size, file) == size;
}

static bool accept_out(void *arg, const unsigned char *data, size_t size) {
    return accept_stream_data(arg, data, size, false);
}

static bool accept_err(void *arg, const unsigned char *data, size_t size) {
    return accept_stream_data(arg, data, size, true);
}

static child_input_status_t provide(void *arg, unsigned char *data, size_t capacity, size_t *size) {
    context_t *ctx = arg;
    ctx->input_calls++;
    *size = 0;
    if (ctx->roundtrip) {
        if (ctx->input_offset == TOTAL)
            return CHILD_INPUT_EOF;
        *size = TOTAL - ctx->input_offset;
        if (*size > capacity)
            *size = capacity;
        for (size_t i = 0; i < *size; i++)
            data[i] = expected(ctx->input_offset + i, false);
        ctx->input_offset += *size;
        return CHILD_INPUT_DATA;
    }
    if (ctx->fail_input)
        return CHILD_INPUT_ERROR;
    if (ctx->invalid_size) {
        *size = capacity + 1;
        return CHILD_INPUT_DATA;
    }
    if (!ctx->ready)
        return CHILD_INPUT_WAIT;
    if (ctx->supplied)
        return CHILD_INPUT_EOF;
    if (capacity < 6)
        return CHILD_INPUT_ERROR;
    memcpy(data, "OK\0YES", 6);
    *size = 6;
    ctx->supplied = true;
    return CHILD_INPUT_DATA;
}

static bool verify_file(FILE *file, bool err) {
    if (fflush(file) || fseek(file, 0, SEEK_SET))
        return false;
    for (size_t i = 0; i < TOTAL; i++)
        if (fgetc(file) != expected(i, err))
            return false;
    return fgetc(file) == EOF && !ferror(file);
}

static int run(const char *name) {
    size_t baseline = get_allocated_memory_size();
    context_t ctx = {0};
    const char *args[] = {"stream fixture", "--worker", "large", NULL};
    child_options_t options = {.arguments = args,
                               .empty_stdin = true,
                               .deadline = child_deadline_after(10000),
                               .stdout_callback = accept_out,
                               .stderr_callback = accept_err,
                               .callback_context = &ctx};
    bool polling = !strcmp(name, "poll");
    bool capture = !strcmp(name, "capture");
    bool legacy = !strcmp(name, "legacy");
    bool timeout = !strcmp(name, "timeout");
    bool stop = !strcmp(name, "stop");
    bool file = !strcmp(name, "file");
    bool error = !strcmp(name, "output-error") || !strcmp(name, "input-error")
                 || !strcmp(name, "invalid-size");
    if (!strcmp(name, "invalid-options")) {
        options.stdin_callback = provide;
        options.stdin_data = "";
        CHECK(!start_child_process(&options));
        CHECK(get_allocated_memory_size() == baseline);
        return 0;
    }
    if (!strcmp(name, "roundtrip")) {
        ctx.roundtrip = true;
        args[2] = "roundtrip";
        options.stdin_callback = provide;
    } else if (!strcmp(name, "dialog") || !strcmp(name, "dialog-poll")) {
        ctx.interactive = true;
        args[2] = "dialog";
        options.stdin_callback = provide;
        polling = !strcmp(name, "dialog-poll");
    } else if (timeout || stop || !strcmp(name, "input-error") || !strcmp(name, "invalid-size")) {
        args[2] = "idle";
        options.stdin_callback = provide;
        options.deadline = stop ? 0 : child_deadline_after(timeout ? 150 : 2000);
        ctx.fail_input = !strcmp(name, "input-error");
        ctx.invalid_size = !strcmp(name, "invalid-size");
    } else if (!strcmp(name, "output-error"))
        ctx.fail_output = true;
    else if (strcmp(name, "large") && !polling && !capture && !legacy && !file)
        return 2;
    options.capture_output = capture || legacy;
    if (legacy)
        options.stdout_callback = options.stderr_callback = NULL;
    if (file) {
        ctx.out_file = tmpfile();
        ctx.err_file = tmpfile();
        CHECK(ctx.out_file && ctx.err_file);
    }
    uint64_t start = child_monotonic_ms();
    child_process_t *child = start_child_process(&options);
    CHECK(child);
    const child_result_t *result;
    if (stop) {
        poll_child_process(child);
        result = stop_child_process(child);
    } else if (polling) {
        do {
            result = poll_child_process(child);
            if (result->status == CHILD_RUNNING)
                pause_ms(1);
        } while (result->status == CHILD_RUNNING);
    } else
        result = wait_child_process(child);
    bool ok = !result->cleanup_error;
    if (error)
        ok = ok && result->status == CHILD_IO_ERROR;
    else if (timeout || stop)
        ok = ok && result->status == (timeout ? CHILD_TIMED_OUT : CHILD_STOPPED)
             && child_monotonic_ms() - start < 8000 && ctx.input_calls;
    else
        ok = ok && result->status == CHILD_EXITED && !result->exit_code && !result->system_error;
    if (!error && !timeout && !stop) {
        if (ctx.interactive)
            ok = ok && ctx.supplied && ctx.out == 12 && !memcmp(ctx.dialog, "READY\0\nDONE\0", 12);
        else if (!legacy)
            ok = ok && !ctx.invalid && ctx.out == TOTAL && ctx.err == TOTAL;
        if (capture || legacy) {
            ok = ok && result->out.truncated && result->err.truncated
                 && result->out.size == CHILD_OUTPUT_LIMIT
                 && result->err.size == CHILD_OUTPUT_LIMIT;
            for (size_t i = 0; i < CHILD_OUTPUT_LIMIT; i++)
                ok = ok && result->out.data[i] == expected(i, false)
                     && result->err.data[i] == expected(i, true);
        } else
            ok = ok && !result->out.size && !result->err.size && !result->out.truncated
                 && !result->err.truncated;
    }
    if (ctx.roundtrip)
        ok = ok && ctx.input_offset == TOTAL && ctx.input_calls > 2;
    if (ctx.fail_output)
        ok = ok && ctx.output_calls == 1;
    if (ctx.fail_input || ctx.invalid_size)
        ok = ok && ctx.input_calls == 1;
    if (file) {
        ok = ok && verify_file(ctx.out_file, false) && verify_file(ctx.err_file, true);
        fclose(ctx.out_file);
        fclose(ctx.err_file);
    }
    if (!ok)
        fprintf(stderr,
                "%s status=%d exit=%lld error=%lu cleanup=%lu out=%zu err=%zu\n",
                name,
                result->status,
                (long long)result->exit_code,
                result->system_error,
                result->cleanup_error,
                ctx.out,
                ctx.err);
    destroy_child_process(child);
    CHECK(ok && get_allocated_memory_size() == baseline);
    return 0;
}

int main(int argc, char **argv) {
    fs_error_t error = {0};
    if (!fs_arguments(&argc, &argv, &error))
        return 2;
    int result = argc == 3 && !strcmp(argv[1], "--worker") ? worker(argv[2])
                 : argc == 2                               ? run(argv[1])
                                                           : 2;
    free_fs_arguments(argc, argv);
    clear_fs_error(&error);
    return result;
}
