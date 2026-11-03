/** @file compiler_diagnostic.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Bounded diagnostic command using the existing owned process-tree runner.
 */
#include "lib/process/child_process.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static bool write_stdout(void *context, const unsigned char *data, size_t size) {
    (void)context;
    return fwrite(data, 1, size, stdout) == size && fflush(stdout) == 0;
}

static bool write_stderr(void *context, const unsigned char *data, size_t size) {
    (void)context;
    return fwrite(data, 1, size, stderr) == size && fflush(stderr) == 0;
}

int main(int argc, char **argv) {
    if (argc < 5)
        return 125;
    char *end;
    errno = 0;
    unsigned long duration = strtoul(argv[1], &end, 10);
    if (errno || !*argv[1] || *end || duration == 0 || duration > 300000)
        return 125;
    FILE *status = fopen(argv[3], "wb");
    if (!status)
        return 125;
    child_options_t options = {.executable = argv[4],
                               .working_directory = argv[2],
                               .arguments = (const char *const *)(argv + 4),
                               .deadline = child_deadline_after(duration),
                               .empty_stdin = true,
                               .stdout_callback = write_stdout,
                               .stderr_callback = write_stderr};
    child_process_t *child = start_child_process(&options);
    if (!child) {
        fclose(status);
        return 125;
    }
    const child_result_t *result = wait_child_process(child);
    int code = 125;
    if (!result->cleanup_error && !result->system_error) {
        if (result->status == CHILD_EXITED && result->exit_code >= 0 && result->exit_code <= 255)
            code = (int)result->exit_code;
        else if (result->status == CHILD_TIMED_OUT)
            code = 124;
        else if (result->status == CHILD_SIGNALED)
            code = 128 + result->signal_number;
    }
    bool saved = fprintf(status,
                         "status=%d exit=%lld signal=%d system=%lu cleanup=%lu deadline_ms=%lu\n",
                         (int)result->status,
                         (long long)result->exit_code,
                         result->signal_number,
                         result->system_error,
                         result->cleanup_error,
                         duration)
                 >= 0;
    destroy_child_process(child);
    if (fclose(status) || fflush(stdout) || fflush(stderr) || !saved)
        return 125;
    return code;
}
