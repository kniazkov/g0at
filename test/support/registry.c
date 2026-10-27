/** @file registry.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Validated, immutable scenario selection.
 */
#include "test/support/registry.h"

#include <string.h>

static bool letter(unsigned char value) {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
}

static bool valid_id(const char *id) {
    if (!id || !letter((unsigned char)*id))
        return false;
    for (const unsigned char *p = (const unsigned char *)id; *p; ++p)
        if (!letter(*p) && !(*p >= '0' && *p <= '9') && *p != '.' && *p != '_' && *p != '-')
            return false;
    return true;
}

bool scenario_registry_validate(const scenario_definition_t *definitions,
                                size_t count,
                                FILE *diagnostics) {
    FILE *out = diagnostics ? diagnostics : stderr;
    if (!definitions || !count) {
        fprintf(out, "Invalid empty scenario registry\n");
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        const scenario_definition_t *entry = definitions + i;
        if (!valid_id(entry->id) || !entry->timeout_ms
            || (entry->skip_reason && !*entry->skip_reason)
            || (!entry->skip_reason && !entry->body)) {
            fprintf(out, "Invalid scenario definition at index %zu\n", i);
            return false;
        }
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(entry->id, definitions[j].id)) {
                fprintf(out, "Duplicate scenario ID: %s\n", entry->id);
                return false;
            }
    }
    return true;
}

int scenario_registry_main(const scenario_definition_t *definitions,
                           size_t count,
                           int argc,
                           const char *const *argv,
                           const scenario_options_t *base_options) {
    FILE *out = base_options && base_options->diagnostics ? base_options->diagnostics : stderr;
    if (!scenario_registry_validate(definitions, count, out))
        return 1;
    if (!argv || argc < 1 || !argv[0]) {
        fprintf(out, "Expected scenario ID or --list\n");
        return 1;
    }
    if (!strcmp(argv[0], "--list")) {
        if (argc != 1)
            return 1;
        for (size_t i = 0; i < count; ++i)
            fprintf(out, "%s\n", definitions[i].id);
        return ferror(out) ? 1 : 0;
    }
    if (!base_options || argc > 2
        || (argc == 2 && (!argv[1] || !scenario_keep_work_argument(argv[1])))) {
        fprintf(out, "Expected <scenario ID> [--keep-work]\n");
        return 1;
    }
    for (size_t i = 0; i < count; ++i) {
        const scenario_definition_t *entry = definitions + i;
        if (strcmp(argv[0], entry->id))
            continue;
        if (entry->skip_reason) {
            fprintf(out, "SKIP %s: %s\n", entry->id, entry->skip_reason);
            return ferror(out) ? 1 : 77;
        }
        scenario_options_t options = *base_options;
        options.name = entry->id;
        options.keep_work = options.keep_work || argc == 2;
        return scenario_execute(&options, entry->timeout_ms, entry->body, entry->user) ? 0 : 1;
    }
    fprintf(out, "Unknown scenario ID: %s\n", argv[0]);
    return 1;
}
