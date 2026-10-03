/**
 * @file analysis_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Source-file tests against structured analysis observations.
 */
#include "analysis/analysis.h"
#include "analysis/c_contract.h"
#include "analysis/c_expression.h"
#include "analysis/function_summary.h"
#include "analysis/lattice.h"
#include "cli/options.h"
#include "graph/declarations.h"
#include "lib/allocate.h"
#include "lib/io.h"
#include "lib/string_ext.h"
#include "parser/parser.h"
#include "scanner/scanner.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool value_matches(const lattice_element_t *value, const char *text) {
    int64_t a, b;
    int used = 0;
    if (sscanf(text, "int=%" SCNd64 "%n", &a, &used) == 1 && !text[used]) {
        return value->type == LATTICE_INTEGER_CONSTANT
               && ((const integer_constant_element_t *)value)->value == a;
    }
    if (sscanf(text, "range=%" SCNd64 ",%" SCNd64 "%n", &a, &b, &used) == 2 && !text[used]) {
        return value->type == LATTICE_INTEGER_RANGE
               && ((const integer_range_element_t *)value)->min == a
               && ((const integer_range_element_t *)value)->max == b;
    }
    if (!strncmp(text, "real=", 5)) {
        if (value->type != LATTICE_REAL_CONSTANT)
            return false;
        double actual = ((const real_constant_element_t *)value)->value;
        const char *expected = text + 5;
        /* Do not delegate special values to the platform's scanf implementation. */
        if (!strcmp(expected, "nan"))
            return isnan(actual);
        if (!strcmp(expected, "inf") || !strcmp(expected, "+inf"))
            return isinf(actual) && !signbit(actual);
        if (!strcmp(expected, "-inf"))
            return isinf(actual) && signbit(actual);
        double real;
        return sscanf(expected, "%lf%n", &real, &used) == 1 && !expected[used] && isfinite(real)
               && actual == real && (real != 0 || !!signbit(real) == !!signbit(actual));
    }
    if (!strncmp(text, "string=", 7)) {
        if (value->type != LATTICE_STRING_CONSTANT)
            return false;
        string_value_t expected = decode_utf8(text + 7);
        string_view_t actual = ((const string_constant_element_t *)value)->value;
        bool equal =
            expected.length == actual.length && !wmemcmp(expected.data, actual.data, actual.length);
        FREE_STRING(expected);
        return equal;
    }

    struct {
        const char *name;
        lattice_type_t type;
    } types[] = {{"top", LATTICE_TOP},
                 {"bottom", LATTICE_BOTTOM},
                 {"null", LATTICE_NULL},
                 {"boolean", LATTICE_BOOLEAN},
                 {"true", LATTICE_TRUE},
                 {"false", LATTICE_FALSE},
                 {"numeric", LATTICE_NUMERIC},
                 {"real", LATTICE_REAL},
                 {"string", LATTICE_STRING},
                 {"integer", LATTICE_INTEGER},
                 {"function", LATTICE_FUNCTION},
                 {"not_null", LATTICE_NOT_NULL},
                 {"array", LATTICE_ARRAY},
                 {"object", LATTICE_USER_DEFINED_OBJECT}};

    for (size_t i = 0; i < sizeof(types) / sizeof(*types); i++) {
        if (!strcmp(text, types[i].name))
            return value->type == types[i].type
                   || (types[i].type == LATTICE_FUNCTION && value->type == LATTICE_KNOWN_FUNCTION);
    }
    return false;
}

/** @brief Comma-separated formal types; '-' selects a zero-parameter signature. */
static bool signature_matches(const function_summary_t *summary, const char *text) {
    if (!summary->parameter_count)
        return !strcmp(text, "-");
    for (size_t i = 0; i < summary->parameter_count; i++) {
        size_t length = strcspn(text, ",");
        char type[32];
        if (!length || length >= sizeof(type))
            return false;
        memcpy(type, text, length);
        type[length] = 0;
        if (!value_matches(summary->parameter_types[i], type))
            return false;
        text += length;
        if (i + 1 == summary->parameter_count)
            return !*text;
        if (*text++ != ',')
            return false;
    }
    return false;
}

/** @brief Checks status:type without interpreting an inconclusive TOP as a successful proof. */
static bool return_type_matches(const function_summary_t *summary, const char *text) {
    const char *status = summary->status == FUNCTION_ANALYZED       ? "analyzed:"
                         : summary->status == FUNCTION_INCONCLUSIVE ? "inconclusive:"
                         : summary->status == FUNCTION_ANALYZING    ? "analyzing:"
                                                                    : "unanalyzed:";
    size_t length = strlen(status);
    return !strncmp(text, status, length) && value_matches(summary->return_type, text + length);
}

/** @brief Display names use underscores instead of spaces in expectation selectors. */
static bool node_type_matches(const node_t *node, const wchar_t *expected) {
    const wchar_t *actual = node->vtbl->type_name;
    for (; *actual && *expected; actual++, expected++) {
        if ((*actual == L' ' ? L'_' : *actual) != *expected)
            return false;
    }
    return *actual == *expected;
}

/** @brief Stable flag spelling for source-level expectations. */
static bool flags_match(uint32_t flags, const char *expected) {
    if (!strcmp(expected, "none"))
        return flags == 0;
    if (!strcmp(expected, "unreachable"))
        return flags == NODE_FLAG_UNREACHABLE;
    if (!strcmp(expected, "pure"))
        return flags == NODE_FLAG_PURE;
    if (!strcmp(expected, "pure|c-compatible"))
        return flags == (NODE_FLAG_PURE | NODE_FLAG_C_COMPATIBLE);
    return false;
}

/** @brief Exact masks, so unexpected extra effects fail the test. */
static bool effects_match(uint32_t effects, const char *expected) {
    if (!strcmp(expected, "none"))
        return effects == 0;
    const char *names[] = {"input", "output", "external-read", "external-write", "unknown"};
    const uint32_t bits[] = {FUNCTION_EFFECT_INPUT,
                             FUNCTION_EFFECT_OUTPUT,
                             FUNCTION_EFFECT_EXTERNAL_READ,
                             FUNCTION_EFFECT_EXTERNAL_WRITE,
                             FUNCTION_EFFECT_UNKNOWN};
    uint32_t mask = 0;
    while (*expected) {
        size_t length = strcspn(expected, "|");
        size_t i = 0;
        while (i < sizeof(bits) / sizeof(*bits)
               && (strlen(names[i]) != length || strncmp(names[i], expected, length)))
            i++;
        if (i == sizeof(bits) / sizeof(*bits) || (mask & bits[i]))
            return false;
        mask |= bits[i];
        expected += length;
        if (*expected) {
            expected++;
            if (!*expected)
                return false;
        }
    }
    return effects == mask;
}

static bool c_expression_matches(c_value_type_t type, const char *expected) {
    string_value_t wanted = decode_utf8(expected);
    bool matches = wanted.data && !wcscmp(c_expression_type_name(type), wanted.data);
    FREE_STRING(wanted);
    return matches;
}

static bool c_blockers_match(uint32_t blockers, const char *expected) {
    string_value_t actual = c_blockers_to_string(blockers);
    string_value_t wanted = decode_utf8(expected);
    bool matches = wanted.data && !wcscmp(actual.data, wanted.data);
    FREE_STRING(actual);
    FREE_STRING(wanted);
    return matches;
}

static bool
function_matches(const function_summary_t *summary, const char *kind, const char *expected) {
    if (!strcmp(kind, "c-support")) {
        const char *support = summary->c_support == FUNCTION_C_SUPPORTED     ? "supported"
                              : summary->c_support == FUNCTION_C_UNSUPPORTED ? "unsupported"
                                                                             : "unknown";
        return !strcmp(expected, support);
    }
    if (!strcmp(kind, "c-blockers"))
        return c_blockers_match(summary->c_blockers, expected);
    if (!strcmp(kind, "effects"))
        return effects_match(summary->direct_effects, expected);
    if (!strcmp(kind, "total-effects"))
        return effects_match(summary->effects, expected);
    if (!strcmp(kind, "purity"))
        return !strcmp(expected, function_summary_is_pure(summary) ? "pure" : "unknown");
    if (!strcmp(kind, "calls"))
        return !strcmp(expected, summary->has_calls ? "yes" : "no");
    return return_type_matches(summary, expected);
}

static size_t declaration_row(const declarator_t *decl) {
    const node_t *node = &decl->base;
    while (node && (!node->position || !node->position->begin))
        node = node->parent;
    return node ? node->position->begin->row : 0;
}

static bool
check_expectations(FILE *file, const analysis_collector_t *collector, const char *name) {
    char line[512];
    size_t line_number = 0, checks = 0;
    bool passed = true;
    while (fgets(line, sizeof(line), file)) {
        line_number++;
        if (!strchr(line, '\n') && !feof(file))
            return false;
        char mode[16], kind[16], variable[128], expected[128], extra;
        size_t row, decl_row;
        if (line[0] == '#' || strspn(line, " \t\r\n") == strlen(line))
            continue;
        if (sscanf(line,
                   "%15s %15s %zu %zu %127s %127s %c",
                   mode,
                   kind,
                   &row,
                   &decl_row,
                   variable,
                   expected,
                   &extra)
                != 6
            || (strcmp(mode, "one") && strcmp(mode, "last") && strcmp(mode, "none"))) {
            fprintf(stderr, "%s.expect:%zu: invalid expectation\n", name, line_number);
            return false;
        }
        analysis_event_query_t query = {.row = row};
        if (!strcmp(kind, "write"))
            query.kind = ANALYSIS_VALUE_WRITE;
        else if (!strcmp(kind, "join"))
            query.kind = ANALYSIS_STATE_JOIN;
        else if (!strcmp(kind, "summary"))
            query.kind = ANALYSIS_DECLARATION_SUMMARY;
        else if (!strcmp(kind, "function") || !strcmp(kind, "effects") || !strcmp(kind, "calls")
                 || !strcmp(kind, "total-effects") || !strcmp(kind, "purity")
                 || !strcmp(kind, "c-support") || !strcmp(kind, "c-blockers")) {
            query.kind = ANALYSIS_FUNCTION_SUMMARY;
            query.column = decl_row;
        } else if (!strcmp(kind, "c-expression")) {
            query.kind = ANALYSIS_C_EXPRESSION;
            query.column = decl_row;
        } else if (!strcmp(kind, "flags")) {
            query.kind = ANALYSIS_NODE_FLAGS;
            query.column = decl_row;
        } else if (!strcmp(kind, "unreachable"))
            query.kind = ANALYSIS_UNREACHABLE;
        else {
            fprintf(stderr, "%s.expect:%zu: invalid event kind\n", name, line_number);
            return false;
        }
        if (!strcmp(mode, "none") && strcmp(expected, "-"))
            return false;
        bool unreachable = query.kind == ANALYSIS_UNREACHABLE;
        bool flags = query.kind == ANALYSIS_NODE_FLAGS;
        bool function = query.kind == ANALYSIS_FUNCTION_SUMMARY;
        bool expression = query.kind == ANALYSIS_C_EXPRESSION;
        if (unreachable && (decl_row || strcmp(variable, "-") || strcmp(expected, "-")))
            return false;
        string_value_t wide = decode_utf8(variable);
        if (!wide.data)
            return false;
        size_t matches = 0;
        const analysis_event_t *last = NULL;
        for (const analysis_event_t *event = find_analysis_event(collector, NULL, &query); event;
             event = find_analysis_event(collector, event, &query)) {
            if (function || expression) {
                char *separator = expression ? strchr(variable, '/') : NULL;
                if (separator)
                    *separator = 0;
                bool selected = signature_matches(event->function_summary, variable);
                if (separator) {
                    *separator = '/';
                    string_value_t node_type = decode_utf8(separator + 1);
                    selected &= node_type.data && node_type_matches(event->node, node_type.data);
                    FREE_STRING(node_type);
                }
                if (!selected)
                    continue;
            } else if (flags) {
                if (!node_type_matches(event->node, wide.data))
                    continue;
            } else if (!unreachable) {
                string_view_t actual = event->declarator->name;
                if (actual.length != wide.length || wmemcmp(actual.data, wide.data, wide.length))
                    continue;
                if (decl_row && declaration_row(event->declarator) != decl_row)
                    continue;
            }
            matches++;
            last = event;
        }
        FREE_STRING(wide);
        checks++;
        bool ok =
            !strcmp(mode, "none")
                ? matches == 0
                : last && (!strcmp(mode, "last") || matches == 1)
                      && (unreachable
                          || (function ? function_matches(last->function_summary, kind, expected)
                              : expression
                                  ? c_expression_matches(last->c_expression->type, expected)
                              : flags ? flags_match(last->flags, expected)
                                      : value_matches(last->value, expected)));
        if (!ok) {
            fprintf(stderr,
                    "%s.expect:%zu: %s (selector matched %zu events)\n",
                    name,
                    line_number,
                    line,
                    matches);
            passed = false;
        }
    }
    return passed && checks > 0 && !ferror(file);
}

static bool run_case(const char *directory, const char *name) {
    char source_path[1024], expected_path[1024];
    if (snprintf(source_path, sizeof(source_path), "%s/%s.goat", directory, name)
            >= sizeof(source_path)
        || snprintf(expected_path, sizeof(expected_path), "%s/%s.expect", directory, name)
               >= sizeof(expected_path))
        return false;
    long before = get_allocated_memory_size();
    string_value_t source = read_utf8_file(source_path);
    FILE *expected = fopen(expected_path, "r");
    if (!source.data || !expected) {
        fprintf(stderr, "Cannot read %s or %s\n", source_path, expected_path);
        FREE_STRING(source);
        if (expected)
            fclose(expected);
        return false;
    }
    arena_t *arena = create_arena(16);
    parser_memory_t memory = {arena, arena, arena, arena};
    token_groups_t *groups = CALLOC(sizeof(*groups));
    scanner_t *scanner = create_scanner(source_path, source, &memory, groups);
    token_list_t tokens;
    parsing_result_t parsing = {0};
    node_t *root = NULL;
    compilation_error_t *error = process_brackets(&memory, scanner, &tokens, groups);
    if (!error)
        error = apply_reduction_rules(groups, &memory, &parsing);
    if (!error)
        error = process_root_token_list(&memory, &tokens, &root);
    options_t *options = create_options();
    analysis_collector_t *collector = create_analysis_collector(arena);
    if (!error)
        error = analyze(root, &memory, options, collector);
    bool passed = false;
    if (error) {
        for (compilation_error_t *item = error; item; item = item->next) {
            fprintf_utf8(stderr,
                         L"%a, %zu.%zu: %s\n",
                         source_path,
                         item->position->begin->row,
                         item->position->begin->column,
                         item->message.data);
        }
    } else
        passed = check_expectations(expected, collector, name);
    if (!passed && !error) {
        string_value_t report = analysis_collector_to_text(collector);
        fprintf_utf8(stderr, L"%s", report.data);
        FREE_STRING(report);
    }
    fclose(expected);
    destroy_options(options);
    FREE(groups);
    destroy_arena(arena);
    FREE_STRING(source);
    if (get_allocated_memory_size() != before) {
        fprintf(stderr, "%s: memory leak\n", name);
        passed = false;
    }
    return passed;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: analysis_testing <fixture-directory>\n");
        return 1;
    }
    char path[1024], line[256];
    if (snprintf(path, sizeof(path), "%s/list.txt", argv[1]) >= sizeof(path))
        return 1;
    FILE *list = fopen(path, "r");
    if (!list) {
        perror(path);
        return 1;
    }
    size_t total = 0, passed = 0;
    bool valid = true;
    while (fgets(line, sizeof(line), list)) {
        char name[128], extra;
        if (line[0] == '#' || strspn(line, " \t\r\n") == strlen(line))
            continue;
        if (sscanf(line, "%127s %c", name, &extra) != 1) {
            valid = false;
            break;
        }
        total++;
        bool ok = run_case(argv[1], name);
        printf("[%s] %s\n", ok ? "ok" : "FAIL", name);
        passed += ok;
    }
    valid = valid && !ferror(list);
    fclose(list);
    printf("Analysis testing: %zu/%zu passed\n", passed, total);
    return valid && total && passed == total ? 0 : 1;
}
