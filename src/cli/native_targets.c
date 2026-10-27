/** @file native_targets.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Present native compiler successes and failures without hiding either.
 */
#include "native_targets.h"

#include "codegen/native_compiler.h"
#include "codegen/native_toolchain.h"
#include "lib/files/io.h"
#include "resources/messages.h"

#include <stdio.h>
#include <string.h>

/** Descriptions label verified results; this is not an availability catalog. */
static const char *describe_target(const char *target) {
    if (!strcmp(target, "linux-x86_64-gnu"))
        return "Linux x86-64 / glibc";
    if (!strcmp(target, "linux-aarch64-gnu"))
        return "Linux AArch64 / glibc";
    if (!strcmp(target, "linux-x86_64-musl"))
        return "Linux x86-64 / musl";
    if (!strcmp(target, "linux-aarch64-musl"))
        return "Linux AArch64 / musl";
    if (!strcmp(target, "windows-x86_64-mingw64"))
        return "Windows x86-64 / MinGW MSVCRT";
    if (!strcmp(target, "windows-x86_64-ucrt64"))
        return "Windows x86-64 / MinGW UCRT";
    if (!strcmp(target, "windows-x86-mingw32"))
        return "Windows x86 / MinGW MSVCRT";
    return target;
}

int list_native_targets(const options_t *options) {
    native_build_context_t *owner = options->native_build_context;
    bool owned = !owner;
    if (owned)
        owner = create_native_build_context(NULL);
    native_toolchain_context_t *context =
        create_native_toolchain_context(native_build_cleanup(owner));
    if (!context) {
        FPRINTF(stderr, "Could not create native compiler discovery context.\n");
        if (owned)
            release_native_build_context(owner);
        return 1;
    }
    native_toolchain_search_t search = find_native_toolchains(context,
                                                              NULL,
                                                              options->native_compiler,
                                                              options->native_probe_timeout,
                                                              true);
    size_t successes = 0;
    for (size_t index = 0; index < search.count; index++) {
        const native_toolchain_t *candidate = search.items[index];
        if (!candidate->available)
            continue;
        if (!successes++)
            fprintf_utf8(stdout, get_messages()->native_targets_available);
        bool selected = true;
        for (size_t previous = 0; previous < index; previous++) {
            const native_toolchain_t *other = search.items[previous];
            if (other->available && !strcmp(other->target, candidate->target))
                selected = false;
        }
        FPRINTF(stdout,
                "  %s (%s): %s; %s %s",
                candidate->target,
                describe_target(candidate->target),
                candidate->compiler,
                candidate->name ? candidate->name : "",
                candidate->version ? candidate->version : "");
        if (selected)
            fprintf_utf8(stdout, get_messages()->native_targets_selected);
        FPRINTF(stdout, "\n");
    }
    if (!successes)
        fprintf_utf8(stdout, get_messages()->native_targets_none);
    bool failed_heading = false;
    for (size_t index = 0; index < search.count; index++) {
        const native_toolchain_t *candidate = search.items[index];
        if (candidate->available)
            continue;
        if (!failed_heading) {
            fprintf_utf8(stderr, get_messages()->native_targets_failed);
            failed_heading = true;
        }
        FPRINTF(stderr,
                "  %s [%s]: %s\n",
                candidate->compiler,
                candidate->target,
                candidate->diagnostic ? candidate->diagnostic : "probe failed");
    }
    if (!search.count && search.diagnostic)
        FPRINTF(stderr, "%s\n", search.diagnostic);
    bool output_ok = fflush(stdout) == 0 && !ferror(stdout) && !ferror(stderr);
    destroy_native_toolchain_search(&search);
    destroy_native_toolchain_context(context);
    if (owned)
        release_native_build_context(owner);
    return successes && output_ok ? 0 : 1;
}
