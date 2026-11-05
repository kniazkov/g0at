/** @file format_oracle_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Frozen Python/C parity checks for the independent JSON format oracle.
 */
#include "lib/hash/sha256.h"
#include "module/module_json.h"
#include "test/support/format_analysis.h"
#include "test/support/format_metadata.h"
#include "test/support/registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Deliberately broken production entry points. Any accidental dependency fails
 * this executable; no production module library is linked. Calling these stubs
 * below also proves that they are active, rather than dead-link assumptions. */
static unsigned production_calls;

goat_module_json_error goat_json_canonical(const yyjson_val *value, char **out, size_t *length) {
    (void)value;
    *out = NULL;
    *length = 0;
    production_calls++;
    return GOAT_MODULE_JSON_INVALID_FIELD;
}

goat_module_json_error goat_module_json_read(const char *data,
                                             size_t length,
                                             goat_module_json_kind kind,
                                             yyjson_doc **out) {
    (void)data;
    (void)length;
    (void)kind;
    *out = NULL;
    production_calls++;
    return GOAT_MODULE_JSON_INVALID_FIELD;
}

static const char *string(yyjson_mut_val *v, const char *key) {
    return yyjson_mut_get_str(yyjson_mut_obj_get(v, key));
}

static unsigned char *unhex(const char *text, size_t *size) {
    if (!text || strlen(text) % 2)
        return NULL;
    *size = strlen(text) / 2;
    unsigned char *bytes = malloc(*size + 1);
    if (!bytes)
        return NULL;
    for (size_t i = 0; i < *size; i++) {
        unsigned a, b;
        char ca = text[2 * i], cb = text[2 * i + 1];
        a = ca >= '0' && ca <= '9'   ? (unsigned)(ca - '0')
            : ca >= 'a' && ca <= 'f' ? (unsigned)(ca - 'a' + 10)
                                     : 16;
        b = cb >= '0' && cb <= '9'   ? (unsigned)(cb - '0')
            : cb >= 'a' && cb <= 'f' ? (unsigned)(cb - 'a' + 10)
                                     : 16;
        if (a > 15 || b > 15) {
            free(bytes);
            return NULL;
        }
        bytes[i] = (unsigned char)(16 * a + b);
    }
    bytes[*size] = 0;
    return bytes;
}

static yyjson_mut_doc *read_json(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file)
        return NULL;
    if (fseek(file, 0, SEEK_END)) {
        fclose(file);
        return NULL;
    }
    long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return NULL;
    }
    char *data = malloc((size_t)length + 1);
    if (!data) {
        fclose(file);
        return NULL;
    }
    size_t count = fread(data, 1, (size_t)length, file);
    fclose(file);
    char error[256];
    yyjson_mut_doc *doc =
        count == (size_t)length ? format_json_parse(data, count, error, sizeof(error)) : NULL;
    free(data);
    return doc;
}

static bool check_case(yyjson_mut_val *row, yyjson_mut_val *source, const char *mutant) {
    const char *op = string(row, "operation"), *input = string(row, "input");
    char error[256] = {0};
    size_t input_size = input ? strlen(input) : 0, actual_size = 0, expected_size = 0,
           bytecode_size = 0;
    unsigned char *raw = NULL, *bytecode = NULL, *expected = NULL;
    char *actual = NULL, *before = NULL, *after = NULL;
    yyjson_mut_doc *doc = NULL, *second = NULL, *expected_doc = NULL;
    bool valid = false, ok = false,
         expected_valid = yyjson_mut_get_bool(yyjson_mut_obj_get(row, "valid"));
    if (!strcmp(op, "parse")) {
        raw = unhex(string(row, "inputHex"), &input_size);
        input = (char *)raw;
    }
    if (input)
        doc = format_json_parse(input, input_size, error, sizeof(error));
    yyjson_mut_val *value = doc ? yyjson_mut_doc_get_root(doc) : NULL;
    yyjson_mut_val *defs = yyjson_mut_obj_get(source, "DEFS");
    if (doc) {
        valid = true;
        if (!strcmp(op, "parse") || !strcmp(op, "canonical")) {
        } else if (!strcmp(op, "normalize"))
            valid = format_metadata_normalize(doc,
                                              value,
                                              string(row, "kind"),
                                              source,
                                              error,
                                              sizeof(error));
        else if (!strcmp(op, "facts"))
            valid = format_facts(doc, value, defs, error, sizeof(error));
        else if (!strcmp(op, "normalize_analysis"))
            valid = format_analysis(doc,
                                    value,
                                    (size_t)yyjson_mut_get_uint(yyjson_mut_obj_get(row, "arity")),
                                    defs,
                                    error,
                                    sizeof(error));
        else if (!strcmp(op, "guards_overlap")) {
            const char *text = string(row, "second");
            second = format_json_parse(text, strlen(text), error, sizeof(error));
            if (!second)
                valid = false;
            else {
                const char *answer =
                    format_metadata_guards_overlap(value, yyjson_mut_doc_get_root(second))
                        ? "true"
                        : "false";
                actual = malloc(strlen(answer) + 1);
                if (actual)
                    strcpy(actual, answer);
            }
            if (actual)
                actual_size = strlen(actual);
        } else if (!strcmp(op, "main_bytes") || !strcmp(op, "main_hash")) {
            before = format_json_canonical(value, NULL);
            if (!strcmp(op, "main_bytes"))
                actual =
                    format_metadata_main_bytes(value, source, &actual_size, error, sizeof(error));
            else {
                bytecode = unhex(string(row, "bytecodeHex"), &bytecode_size);
                if (bytecode)
                    actual = (char *)format_metadata_hash_input(value,
                                                                source,
                                                                bytecode,
                                                                bytecode_size,
                                                                &actual_size,
                                                                error,
                                                                sizeof(error));
            }
            valid = actual != NULL;
            after = format_json_canonical(value, NULL);
            if (!before || !after || strcmp(before, after)) {
                fprintf(stderr, "%s modified input\n", string(row, "id"));
                goto done;
            }
        } else {
            fprintf(stderr, "Unknown operation %s\n", op);
            goto done;
        }
        if (valid && !actual)
            actual = format_json_canonical(value, &actual_size);
        if (valid && !actual)
            valid = false;
    }
    if (mutant && !strcmp(mutant, "accept-invalid") && !expected_valid)
        valid = true;
    if (valid != expected_valid) {
        fprintf(stderr,
                "%s %s: expected valid=%d, got %d: %s\n",
                string(row, "id"),
                op,
                expected_valid,
                valid,
                error);
        goto done;
    }
    if (!valid) {
        ok = true;
        goto done;
    }
    if (string(row, "expectedHex"))
        expected = unhex(string(row, "expectedHex"), &expected_size);
    else {
        const char *text = string(row, "expected");
        expected_doc = format_json_parse(text, strlen(text), error, sizeof(error));
        if (expected_doc)
            expected = (unsigned char *)format_json_canonical(yyjson_mut_doc_get_root(expected_doc),
                                                              &expected_size);
    }
    if (mutant && !strcmp(mutant, "corrupt-output") && actual_size)
        actual[0] ^= 1;
    if (!expected || actual_size != expected_size || memcmp(actual, expected, actual_size)) {
        fprintf(stderr,
                "%s %s: byte mismatch (%zu vs %zu)\nExpected: %.*s\nActual: %.*s\n",
                string(row, "id"),
                op,
                expected_size,
                actual_size,
                (int)expected_size,
                expected ? (char *)expected : "",
                (int)actual_size,
                actual);
        goto done;
    }
    if (string(row, "sha256")) {
        uint8_t digest[SHA256_SIZE];
        char hex[65];
        sha256(actual, actual_size, digest);
        for (size_t i = 0; i < 32; i++)
            snprintf(hex + 2 * i, 3, "%02x", digest[i]);
        if (strcmp(hex, string(row, "sha256"))) {
            fprintf(stderr, "%s digest mismatch\n", string(row, "id"));
            goto done;
        }
    }
    ok = true;
done:
    free(raw);
    free(bytecode);
    free(expected);
    free(actual);
    free(before);
    free(after);
    yyjson_mut_doc_free(doc);
    yyjson_mut_doc_free(second);
    yyjson_mut_doc_free(expected_doc);
    return ok;
}

typedef struct {
    const char *group;
    const char *schema;
    const char *mutant;
} test_options;

static bool release_doc(void *doc) {
    yyjson_mut_doc_free(doc);
    return true;
}

/* Remaining artifact assertions from the retired reference unittest. The opcode
 * list is the independent format-v1 specification, not production enum values. */
static void
reference_artifacts(scenario_run_t *run, const test_options *options, yyjson_mut_val *source) {
    static const char names[] =
        "NOP ARG END JUMP JIF POP DUP NIL TRUE FALSE ILOAD32 ILOAD64 RLOAD SLOAD CLOAD "
        "VLOAD VAR CONST STORE GETPROP SETPROP UPLUS UMINUS INC DEC LNOT BOOL BNOT LAND LOR "
        "BAND BOR BXOR SHL SHR ADD SUB MUL DIVIDE MODULO POWER LESS LEQ GREATER GREQ EQUAL "
        "DIFF FUNC CALL CALLM RET ENTER LEAVE RESTORE TRY THROW GETINDEX SETINDEX ARRAY "
        "INHERIT LOOP_ENTER LOOP_JUMP DUPN ROTATE SNAPSHOT ITER_NEXT FINALLY_ENTER "
        "FINALLY_LEAVE FINALLY_END DVAR DCONST BLOAD BSTORE INCLUDE IMPORT MAIN DIR "
        "CHECKPROP CHECKINDEX CHECKVAR CHECKBOUND";
    const char *slash = strrchr(options->schema, '/');
    size_t prefix = slash ? (size_t)(slash - options->schema) : 0;
    char path[4096];
    scenario_require(run, prefix + 14 < sizeof(path), "opcode artifact path length");
    snprintf(path, sizeof(path), "%.*s/opcodes.json", (int)prefix, options->schema);
    yyjson_mut_doc *opcodes = read_json(path);
    scenario_require(run, opcodes != NULL, "read independent opcode artifact");
    scenario_defer(run, release_doc, opcodes);
    yyjson_mut_val *root = yyjson_mut_doc_get_root(opcodes);
    size_t count = 0;
    for (const char *name = names; *name;) {
        const char *end = strchr(name, ' ');
        size_t size = end ? (size_t)(end - name) : strlen(name);
        char key[32];
        scenario_require(run, size < sizeof(key), "opcode name length");
        memcpy(key, name, size);
        key[size] = 0;
        yyjson_mut_val *value = yyjson_mut_obj_get(root, key);
        scenario_require(run,
                         yyjson_mut_is_int(value) && yyjson_mut_get_uint(value) == count,
                         "opcode name has exact independent index");
        ++count;
        name += size + (end != NULL);
    }
    scenario_require(run, yyjson_mut_obj_size(root) == count, "no additional opcode entries");
    static const char *top_keys[] = {"DEFS", "ROOTS"};
    static const char *root_keys[] = {"gmod", "module", "native", "target"};
    yyjson_mut_val *roots = yyjson_mut_obj_get(source, "ROOTS");
    yyjson_mut_val *objects[] = {source, roots};
    for (size_t group = 0; group < 2; ++group) {
        size_t index, max;
        yyjson_mut_val *key, *value;
        scenario_require(run,
                         yyjson_mut_obj_size(objects[group]) == (group ? 4 : 2),
                         "declarative schema object size");
        yyjson_mut_obj_foreach(objects[group], index, max, key, value) {
            (void)value;
            scenario_require(
                run,
                !strcmp(yyjson_mut_get_str(key), group ? root_keys[index] : top_keys[index]),
                "declarative schema insertion order");
        }
    }
    yyjson_mut_val *defs = yyjson_mut_obj_get(source, "DEFS");
    yyjson_mut_val *facts = yyjson_mut_obj_get(defs, "facts");
    yyjson_mut_val *properties = yyjson_mut_obj_get(facts, "properties");
    yyjson_mut_val *range = yyjson_mut_obj_get(properties, "range");
    yyjson_mut_val *bounds = yyjson_mut_obj_get(range, "properties");
    scenario_require(run, yyjson_mut_obj_size(bounds) == 2, "both integer range bounds exist");
    size_t index, max;
    yyjson_mut_val *key, *value;
    yyjson_mut_obj_foreach(bounds, index, max, key, value) {
        (void)key;
        yyjson_mut_val *minimum = yyjson_mut_obj_get(value, "minimum");
        yyjson_mut_val *maximum = yyjson_mut_obj_get(value, "maximum");
        scenario_require(run,
                         yyjson_mut_is_int(minimum) && yyjson_mut_is_int(maximum)
                             && yyjson_mut_get_sint(minimum) == INT64_MIN
                             && yyjson_mut_get_sint(maximum) == INT64_MAX,
                         "schema keeps exact int64 bounds without floating conversion");
    }
    puts("reference-artifacts: opcode mapping, source ordering and int64 bounds passed");
}

static void run_group(scenario_run_t *run, void *user) {
    test_options *options = user;
    const char *data = scenario_data_directory(scenario_run_context(run));
    size_t length = strlen(data) + 64;
    char *path = malloc(length);
    scenario_require(run, path != NULL, "Allocate corpus path");
    snprintf(path, length, "%s/format-oracle/cases.json", data);
    yyjson_mut_doc *cases = read_json(path);
    free(path);
    scenario_require(run, cases != NULL, "Read frozen oracle corpus");
    scenario_defer(run, release_doc, cases);
    yyjson_mut_doc *schema = read_json(options->schema);
    scenario_require(run, schema != NULL, "Read schema-source.json");
    scenario_defer(run, release_doc, schema);
    if (!strcmp(options->group, "reference-artifacts") || !strcmp(options->group, "all")) {
        reference_artifacts(run, options, yyjson_mut_doc_get_root(schema));
        if (!strcmp(options->group, "reference-artifacts"))
            return;
    }
    yyjson_mut_val *rows = yyjson_mut_doc_get_root(cases);
    size_t index, max, count = 0;
    yyjson_mut_val *row;
    unsigned initial = production_calls;
    yyjson_mut_arr_foreach(rows, index, max, row) {
        if (strcmp(options->group, "all") && strcmp(options->group, string(row, "group")))
            continue;
        scenario_checkpoint(run);
        bool ok = check_case(row, yyjson_mut_doc_get_root(schema), options->mutant);
        scenario_require(run, ok, "Python/C format oracle parity");
        count++;
    }
    scenario_require(run, count > 0, "Group contains recorded cases");
    scenario_require(run,
                     production_calls == initial,
                     "Oracle must not call broken production APIs");
    printf("%s: %zu frozen cases passed; production faults isolated\n", options->group, count);
}

int main(int argc, char **argv) {
    if (argc < 5 || argc > 6) {
        fprintf(stderr,
                "Usage: %s group build data schema-source [corrupt-output|accept-invalid]\n",
                argv[0]);
        return 1;
    }
    if (argc == 6 && strcmp(argv[5], "corrupt-output") && strcmp(argv[5], "accept-invalid"))
        return 1;
    char *broken = NULL;
    size_t broken_size = 0;
    yyjson_doc *broken_doc = NULL;
    if (goat_json_canonical(NULL, &broken, &broken_size) != GOAT_MODULE_JSON_INVALID_FIELD
        || goat_module_json_read("{}", 2, GOAT_MODULE_METADATA, &broken_doc)
               != GOAT_MODULE_JSON_INVALID_FIELD
        || production_calls != 2)
        return 1;
    test_options options = {argv[1], argv[4], argc == 6 ? argv[5] : NULL};
    scenario_definition_t definition = {argv[1], 30000, run_group, &options, NULL};
    scenario_options_t base = {NULL, argv[2], argv[3], false, stderr};
    const char *selection[] = {argv[1]};
    return scenario_registry_main(&definition, 1, 1, selection, &base);
}
