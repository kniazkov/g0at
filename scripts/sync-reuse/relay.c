/* SYNTHETIC experiment: deliberately reuse a stale GNU Make sync HANDLE.
 * Never use this relay for a production build. All descendants are isolated in
 * a kill-on-close Job Object and started suspended until assignment succeeds.
 */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define LIMIT 4096
int main(int argc, char **argv) {
    const char *flags = getenv("MAKEFLAGS"), *token;
    char *end, *clean = NULL, command[32768];
    HANDLE events[LIMIT], stale, job = NULL;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    DWORD info, error, wait, code = 0;
    unsigned long long number;
    unsigned count = 0;
    int stripped, result = 2, created = 0, assigned = 0;
    if (argc != 3 || (strcmp(argv[1], "stale") && strcmp(argv[1], "stripped"))) {
        fprintf(stderr, "usage: relay.exe stale|stripped path-to-mingw32-make.exe\n");
        return 2;
    }
    stripped = !strcmp(argv[1], "stripped");
    token = flags ? strstr(flags, "--sync-mutex=") : NULL;
    if (!token || (token != flags && token[-1] != ' ')) {
        puts("SYNTHETIC inconclusive: no standalone sync-mutex token"); return 77;
    }
    errno = 0;
    number = strtoull(token + strlen("--sync-mutex="), &end, 0);
    if (errno || end == token + strlen("--sync-mutex=") || (*end && *end != ' ') ||
        number > (uintptr_t)-1) return 2;
    stale = (HANDLE)(uintptr_t)number;
    if (GetHandleInformation(stale, &info)) {
        puts("SYNTHETIC inconclusive: original handle is valid, cannot model drop"); return 77;
    }
    error = GetLastError();
    if (error != ERROR_INVALID_HANDLE) return 2;
    /* No other handles are allocated before this bounded reuse attempt. */
    while (count < LIMIT) {
        HANDLE event = CreateEventA(&sa, TRUE, FALSE, NULL);
        if (!event) goto cleanup;
        events[count++] = event;
        if (event == stale) break;
    }
    if (!count || events[count - 1] != stale) {
        puts("SYNTHETIC inconclusive: allocation bound reached without reuse");
        result = 77; goto cleanup;
    }
    printf("SYNTHETIC mode=%s stale_handle=%p reused_as=unsignalled_Event allocations=%u\n",
           argv[1], stale, count);
    if (stripped) {
        size_t prefix = (size_t)(token - flags);
        clean = malloc(strlen(flags) + 1);
        if (!clean) goto cleanup;
        memcpy(clean, flags, prefix);
        strcpy(clean + prefix, end); /* Preserve every other option/variable. */
        if (!SetEnvironmentVariableA("MAKEFLAGS", clean)) goto cleanup;
    }
    job = CreateJobObjectA(NULL, NULL);
    if (!job) goto cleanup;
    memset(&limits, 0, sizeof(limits));
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                &limits, sizeof(limits))) goto cleanup;
    memset(&startup, 0, sizeof(startup)); startup.cb = sizeof(startup);
    memset(&process, 0, sizeof(process));
    if (snprintf(command, sizeof(command), "\"%s\" -j4 -f tiny.mk child", argv[2]) >=
        (int)sizeof(command)) goto cleanup;
    /* Native inheritance keeps the synthetic event at the advertised value.
     * Explicit -j4 isolates jobserver tokens; -Otarget remains in MAKEFLAGS. */
    if (!CreateProcessA(NULL, command, NULL, NULL, TRUE, CREATE_SUSPENDED,
                        NULL, NULL, &startup, &process)) goto cleanup;
    created = 1;
    if (!AssignProcessToJobObject(job, process.hProcess)) {
        fprintf(stderr, "Job assignment failed error=%lu; refusing unsafe launch\n",
                (unsigned long)GetLastError()); goto cleanup;
    }
    assigned = 1;
    if (ResumeThread(process.hThread) == (DWORD)-1) goto cleanup;
    wait = WaitForSingleObject(process.hProcess, 5000);
    if (wait == WAIT_TIMEOUT) {
        puts("SYNTHETIC observed=watchdog_timeout");
        result = 1;
        if (!stripped && SetEvent(stale) &&
            WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0 &&
            GetExitCodeProcess(process.hProcess, &code) && code == 0) {
            puts("SYNTHETIC causal_control=completed_after_signalling_reused_event");
            result = 0;
        }
    } else if (wait == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, &code)) {
        printf("SYNTHETIC observed=completed exit=%lu\n", (unsigned long)code);
        result = stripped && code == 0 ? 0 : 1;
    }
cleanup:
    if (created) {
        /* Never permit the intentionally blocked process tree to escape. */
        if (assigned) TerminateJobObject(job, 99);
        else TerminateProcess(process.hProcess, 99); /* Still suspended, no children. */
    }
    if (job) CloseHandle(job); /* kill-on-close remains a fallback */
    if (created) {
        if (WaitForSingleObject(process.hProcess, 5000) != WAIT_OBJECT_0) result = 2;
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
    }
    while (count) CloseHandle(events[--count]);
    free(clean);
    printf("SYNTHETIC result=%d (77=inconclusive; 0=predicted paired outcome)\n", result);
    return result;
}
