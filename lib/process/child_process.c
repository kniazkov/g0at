/** @file child_process.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Bounded capture and absolute deadlines.
 */
#include "lib/process/child_process_internal.h"

#include <string.h>

child_deadline_t child_deadline_after(uint64_t timeout_ms) {
    if (!timeout_ms)
        return 0;
    uint64_t now = child_monotonic_ms();
    return timeout_ms > UINT64_MAX - now ? UINT64_MAX : now + timeout_ms;
}

void child_capture(child_output_t *output, const unsigned char *bytes, size_t size) {
    size_t available = CHILD_OUTPUT_LIMIT - output->size;
    size_t keep = size < available ? size : available;
    memcpy(output->data + output->size, bytes, keep);
    output->size += keep;
    output->truncated |= keep < size;
}

bool child_options_valid(const child_options_t *options) {
    if (!options || !options->arguments || !options->arguments[0]
        || (!options->stdin_data && options->stdin_size)
        || (options->stdin_data && options->stdin_callback))
        return false;
    if (options->environment) {
        for (size_t i = 0; options->environment[i]; i++) {
            const char *entry = options->environment[i];
            const char *equal = strchr(entry, '=');
            if (!equal || equal == entry)
                return false;
            size_t length = (size_t)(equal - entry);
            for (size_t j = 0; j < i; j++) {
                const char *previous = options->environment[j];
                const char *previous_equal = strchr(previous, '=');
                if ((size_t)(previous_equal - previous) == length
                    && !memcmp(entry, previous, length))
                    return false;
            }
        }
    }
    return true;
}
