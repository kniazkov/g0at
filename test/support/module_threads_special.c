/** @file module_threads_special.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent native lifetime and initializer pipe-handshake observations.
 */
#include "module_threads_special.h"

#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/language_execution.h"
#include "test/support/module_loading_owner.h"

#include <string.h>
#define REQUIRE(x) scenario_require(run, (x), #x)

static bool byte_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

void module_threads_lifetime(scenario_run_t *run,
                             const char *fixture,
                             const char *source_absolute,
                             bool relative) {
    fs_error_t error = {0};
    char *source = absolute_fs_path(source_absolute, &error);
    clear_fs_error(&error);
    REQUIRE(source);
    language_owned(run, source);
    const char *leaf = source;
    for (const char *p = source; *p; p++) {
#ifdef _WIN32
        if (*p == '/' || *p == '\\')
            leaf = p + 1;
#else
        if (*p == '/')
            leaf = p + 1;
#endif
    }
    REQUIRE(leaf > source && *leaf);
    size_t size = (size_t)(leaf - source - 1);
    char *directory = language_owned(run, ALLOC(size + 1));
    memcpy(directory, source, size);
    directory[size] = 0;
    const char *arguments[] = {fixture, relative ? leaf : source, NULL};
    loading_owner_options_t options = {.working_directory = directory};
    loading_owner_t *owner =
        loading_owner_command_start_options(run,
                                            scenario_work_directory(scenario_run_context(run)),
                                            "module-thread-lifetime",
                                            arguments,
                                            NULL,
                                            NULL,
                                            0,
                                            true,
                                            &options);
    loading_owner_command_wait(owner, 0, 30000, true);
    loading_bytes_t output = loading_owner_output(owner);
    const unsigned char *bytes = output.data;
    size_t begin = 0, end = output.size;
    while (begin < end && byte_space(bytes[begin]))
        begin++;
    while (end > begin && byte_space(bytes[end - 1]))
        end--;
    scenario_require_bytes(run,
                           "native lifetime stdout.strip()",
                           "OK",
                           2,
                           output.size ? bytes + begin : (const unsigned char *)"",
                           end - begin,
                           false);
    /* Original acceptance ignores stderr; transport still saves it for diagnostics. */
}

static void line(scenario_run_t *run, loading_owner_t *owner, const char *expected) {
    loading_bytes_t output;
    REQUIRE(loading_owner_next_line(owner, 30000, &output));
    const unsigned char *bytes = output.data;
    size_t end = output.size;
    while (end && (bytes[end - 1] == '\r' || bytes[end - 1] == '\n'))
        end--;
    scenario_require_bytes(run,
                           "initializer handshake line",
                           expected,
                           strlen(expected),
                           bytes,
                           end,
                           false);
}

void module_threads_handshake(scenario_run_t *run,
                              const char *fixture,
                              const char *archive,
                              const char *unrelated,
                              const char *const *environment) {
    const char *arguments[] = {fixture, "binary", archive, NULL};
    loading_owner_options_t options = {.working_directory = unrelated};
    loading_owner_t *owner =
        loading_owner_command_start_options(run,
                                            scenario_work_directory(scenario_run_context(run)),
                                            "module-thread-handshake",
                                            arguments,
                                            environment,
                                            NULL,
                                            0,
                                            false,
                                            &options);
    line(run, owner, "waiting");
    line(run, owner, "ready");
    /* This is a real two-line release, never a scheduler-delay ordering assumption. */
    static const char release[] = "first\nsecond\n";
    loading_owner_send(owner, release, sizeof(release) - 1, 30000);
    line(run, owner, "done");
    loading_owner_reap(owner, 30000);
    const child_result_t *result = loading_owner_command_poll(owner);
    REQUIRE(result->status == CHILD_EXITED && result->exit_code == 0);
    /* Match the original: leftover stdout and stderr are not acceptance oracles. */
}
