/**
 * @file analysis_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Source-file tests against structured analysis observations.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "analysis/analysis.h"
#include "analysis/lattice.h"
#include "cli/options.h"
#include "graph/declarations.h"
#include "lib/allocate.h"
#include "lib/io.h"
#include "lib/string_ext.h"
#include "scanner/scanner.h"
#include "parser/parser.h"

static bool value_matches(const lattice_element_t *value, const char *text) {
    int64_t a, b;
    int used = 0;
    if (sscanf(text, "int=%" SCNd64 "%n", &a, &used) == 1 && !text[used]) {
        return value->type == LATTICE_INTEGER_CONSTANT &&
            ((const integer_constant_element_t *)value)->value == a;
    }
    if (sscanf(text, "range=%" SCNd64 ",%" SCNd64 "%n", &a, &b, &used) == 2 && !text[used]) {
        return value->type == LATTICE_INTEGER_RANGE &&
            ((const integer_range_element_t *)value)->min == a &&
            ((const integer_range_element_t *)value)->max == b;
    }
    struct { const char *name; lattice_type_t type; } types[] = {
        { "top", LATTICE_TOP }, { "bottom", LATTICE_BOTTOM }, { "null", LATTICE_NULL },
        { "true", LATTICE_TRUE }, { "false", LATTICE_FALSE }, { "numeric", LATTICE_NUMERIC },
        { "integer", LATTICE_INTEGER }, { "function", LATTICE_FUNCTION }
    };
    for (size_t i = 0; i < sizeof(types) / sizeof(*types); i++) {
        if (!strcmp(text, types[i].name)) return value->type == types[i].type;
    }
    return false;
}

static size_t declaration_row(const declarator_t *decl) {
    const node_t *node = &decl->base;
    while (node && (!node->position || !node->position->begin)) node = node->parent;
    return node ? node->position->begin->row : 0;
}

static bool check_expectations(FILE *file, const analysis_collector_t *collector, const char *name) {
    char line[512];
    size_t line_number = 0, checks = 0;
    bool passed = true;
    while (fgets(line, sizeof(line), file)) {
        line_number++;
        if (!strchr(line, '\n') && !feof(file)) return false;
        char mode[16], kind[16], variable[128], expected[128], extra;
        size_t row, decl_row;
        if (line[0] == '#' || strspn(line, " \t\r\n") == strlen(line)) continue;
        if (sscanf(line, "%15s %15s %zu %zu %127s %127s %c", mode, kind,
                &row, &decl_row, variable, expected, &extra) != 6 ||
                (strcmp(mode, "one") && strcmp(mode, "last") && strcmp(mode, "none"))) {
            fprintf(stderr, "%s.expect:%zu: invalid expectation\n", name, line_number);
            return false;
        }
        analysis_event_query_t query = { .row = row };
        if (!strcmp(kind, "write")) query.kind = ANALYSIS_VALUE_WRITE;
        else if (!strcmp(kind, "join")) query.kind = ANALYSIS_STATE_JOIN;
        else if (!strcmp(kind, "summary")) query.kind = ANALYSIS_DECLARATION_SUMMARY;
        else if (!strcmp(kind, "unreachable")) query.kind = ANALYSIS_UNREACHABLE;
        else { fprintf(stderr, "%s.expect:%zu: invalid event kind\n", name, line_number); return false; }
        if (!strcmp(mode, "none") && strcmp(expected, "-")) return false;
        bool unreachable = query.kind == ANALYSIS_UNREACHABLE;
        if (unreachable && (decl_row || strcmp(variable, "-") || strcmp(expected, "-"))) return false;
        string_value_t wide = decode_utf8(variable);
        if (!wide.data) return false;
        size_t matches = 0;
        const analysis_event_t *last = NULL;
        for (const analysis_event_t *event = find_analysis_event(collector, NULL, &query);
                event; event = find_analysis_event(collector, event, &query)) {
            if (!unreachable) {
                string_view_t actual = event->declarator->name;
                if (actual.length != wide.length || wmemcmp(actual.data, wide.data, wide.length)) continue;
                if (decl_row && declaration_row(event->declarator) != decl_row) continue;
            }
            matches++;
            last = event;
        }
        FREE_STRING(wide);
        checks++;
        bool ok = !strcmp(mode, "none") ? matches == 0 :
            last && (!strcmp(mode, "last") || matches == 1) &&
            (unreachable || value_matches(last->value, expected));
        if (!ok) {
            fprintf(stderr, "%s.expect:%zu: %s (selector matched %zu events)\n",
                name, line_number, line, matches);
            passed = false;
        }
    }
    return passed && checks > 0 && !ferror(file);
}

static bool run_case(const char *directory, const char *name) {
    char source_path[1024], expected_path[1024];
    if (snprintf(source_path, sizeof(source_path), "%s/%s.goat", directory, name) >= sizeof(source_path) ||
        snprintf(expected_path, sizeof(expected_path), "%s/%s.expect", directory, name) >= sizeof(expected_path)) return false;
    long before = get_allocated_memory_size();
    string_value_t source = read_utf8_file(source_path);
    FILE *expected = fopen(expected_path, "r");
    if (!source.data || !expected) {
        fprintf(stderr, "Cannot read %s or %s\n", source_path, expected_path);
        FREE_STRING(source);
        if (expected) fclose(expected);
        return false;
    }
    arena_t *arena = create_arena(16);
    parser_memory_t memory = { arena, arena, arena, arena };
    token_groups_t *groups = CALLOC(sizeof(*groups));
    scanner_t *scanner = create_scanner(source_path, source, &memory, groups);
    token_list_t tokens;
    parsing_result_t parsing = {0};
    node_t *root = NULL;
    compilation_error_t *error = process_brackets(&memory, scanner, &tokens, groups);
    if (!error) error = apply_reduction_rules(groups, &memory, &parsing);
    if (!error) error = process_root_token_list(&memory, &tokens, &root);
    options_t *options = create_options();
    analysis_collector_t *collector = create_analysis_collector(arena);
    if (!error) error = analyze(root, &memory, options, collector);
    bool passed = false;
    if (error) {
        for (compilation_error_t *item = error; item; item = item->next) {
            fprintf_utf8(stderr, L"%a, %zu.%zu: %s\n", source_path,
                item->position->begin->row, item->position->begin->column, item->message.data);
        }
    }
    else passed = check_expectations(expected, collector, name);
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
    if (argc != 2) { fprintf(stderr, "Usage: analysis_testing <fixture-directory>\n"); return 1; }
    char path[1024], line[256];
    if (snprintf(path, sizeof(path), "%s/list.txt", argv[1]) >= sizeof(path)) return 1;
    FILE *list = fopen(path, "r");
    if (!list) { perror(path); return 1; }
    size_t total = 0, passed = 0;
    bool valid = true;
    while (fgets(line, sizeof(line), list)) {
        char name[128], extra;
        if (line[0] == '#' || strspn(line, " \t\r\n") == strlen(line)) continue;
        if (sscanf(line, "%127s %c", name, &extra) != 1) { valid = false; break; }
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
