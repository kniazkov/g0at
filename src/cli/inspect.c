/** @file inspect.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Stable text inspection over the shared graph validation engine.
 */
#include "inspect.h"

#include "lib/allocate.h"
#include "lib/file_system.h"
#include "module/module_native.h"
#include "vm/module_graph_diagnostics.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct metadata_record {
    char *archive;
    yyjson_doc *doc;
    struct metadata_record *next;
} metadata_record_t;

typedef struct {
    metadata_record_t *records;
    bool failed;
} inspection_t;

/* Escape the same non-printing Unicode scalars as graph diagnostics. Input
 * metadata is valid UTF-8; ASCII JSON syntax is retained for nested values. */
static size_t unicode_control(const char *text, size_t length) {
    const unsigned char *p = (const unsigned char *)text;
    if (length >= 2 && p[0] == 0xc2 && p[1] >= 0x80 && p[1] <= 0x9f) {
        printf("\\u%04x", p[1]);
        return 2;
    }
    if (length >= 3 && p[0] == 0xe2
        && ((p[1] == 0x80 && p[2] >= 0xa8 && p[2] <= 0xae)
            || (p[1] == 0x81 && p[2] >= 0xa6 && p[2] <= 0xa9))) {
        printf("\\u%04x", ((p[0] & 15) << 12) | ((p[1] & 63) << 6) | (p[2] & 63));
        return 3;
    }
    return 0;
}

static void quoted(const char *text, size_t length) {
    putchar('"');
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)text[i];
        size_t escaped = unicode_control(text + i, length - i);
        if (escaped)
            i += escaped - 1;
        else if (c == '"' || c == '\\')
            printf("\\%c", c);
        else if (c < 32 || c == 127)
            printf("\\u%04x", c);
        else
            putchar(c);
    }
    putchar('"');
}

static void value(yyjson_val *item) {
    if (yyjson_is_str(item)) {
        quoted(yyjson_get_str(item), yyjson_get_len(item));
        return;
    }
    char *text = NULL;
    size_t length = 0;
    if (item && goat_json_canonical(item, &text, &length) == GOAT_MODULE_JSON_OK) {
        /* Canonical JSON escapes C0, but printable nested metadata must also
         * escape DEL/C1 rather than issuing terminal control sequences. */
        for (size_t i = 0; i < length; ++i) {
            unsigned char c = (unsigned char)text[i];
            size_t escaped = unicode_control(text + i, length - i);
            if (escaped)
                i += escaped - 1;
            else if (c == 127)
                fputs("\\u007f", stdout);
            else
                putchar(c);
        }
        goat_module_json_free(text);
    } else
        fputs("unknown", stdout);
}

static void field(yyjson_val *object, const char *key) {
    value(yyjson_obj_get(object, key));
}

static void capture_metadata(const char *archive, const yyjson_doc *metadata, void *context) {
    inspection_t *inspection = context;
    char *bytes = NULL;
    size_t length = 0;
    if (goat_json_canonical(yyjson_doc_get_root((yyjson_doc *)metadata), &bytes, &length)) {
        inspection->failed = true;
        return;
    }
    yyjson_doc *copy = NULL;
    goat_module_json_error error =
        goat_module_json_read(bytes, length, GOAT_MODULE_METADATA, &copy);
    goat_module_json_free(bytes);
    if (error) {
        inspection->failed = true;
        return;
    }
    metadata_record_t *record = ALLOC(sizeof(*record));
    size_t count = strlen(archive) + 1;
    record->archive = ALLOC(count);
    memcpy(record->archive, archive, count);
    record->doc = copy;
    record->next = inspection->records;
    inspection->records = record;
}

static metadata_record_t *find_metadata(inspection_t *inspection, const char *archive) {
    for (metadata_record_t *record = inspection->records; record; record = record->next)
        if (strcmp(record->archive, archive) == 0)
            return record;
    return NULL;
}

static int node_compare(const void *left, const void *right) {
    const module_graph_node_t *a = *(const module_graph_node_t *const *)left;
    const module_graph_node_t *b = *(const module_graph_node_t *const *)right;
    int result = strcmp(a->name ? a->name : "", b->name ? b->name : "");
    if (!result)
        result = strcmp(a->version ? a->version : "", b->version ? b->version : "");
    return result ? result : strcmp(a->archive, b->archive);
}

static int key_compare(const void *left, const void *right) {
    yyjson_val *a = *(yyjson_val *const *)left, *b = *(yyjson_val *const *)right;
    size_t alen = yyjson_get_len(a), blen = yyjson_get_len(b);
    int result = memcmp(yyjson_get_str(a), yyjson_get_str(b), alen < blen ? alen : blen);
    return result ? result : (alen > blen) - (alen < blen);
}

static void exports(yyjson_val *metadata) {
    yyjson_val *object = yyjson_obj_get(metadata, "externals");
    size_t count = yyjson_obj_size(object), index = 0, maximum;
    yyjson_val *key, *entry;
    yyjson_val **keys = ALLOC(allocation_size_multiply(count, sizeof(*keys)));
    yyjson_obj_foreach(object, index, maximum, key, entry) keys[index] = key;
    qsort(keys, count, sizeof(*keys), key_compare);
    fputs("  Exports:\n", stdout);
    if (!count)
        fputs("    (none)\n", stdout);
    for (index = 0; index < count; ++index) {
        key = keys[index];
        entry = yyjson_obj_iter_get_val(key);
        fputs("    ", stdout);
        value(key);
        fputs(" mutable=", stdout);
        field(entry, "mutable");
        fputs(" type=", stdout);
        field(entry, "type");
        putchar('\n');
    }
    FREE(keys);
}

static void variants(yyjson_val *metadata, const goat_native_environment *target, bool valid) {
    yyjson_val *array = yyjson_obj_get(metadata, "native"), *variant;
    size_t index, maximum, matches = 0;
    const uint32_t abi = 1;
    fputs("  Native variants:\n", stdout);
    if (!yyjson_arr_size(array))
        fputs("    (none)\n", stdout);
    yyjson_arr_foreach(array, index, maximum, variant) {
        fputs("    Library: ", stdout);
        field(variant, "library");
        fputs("\n      SHA-256: ", stdout);
        field(variant, "hash");
        fputs("\n      Requirements: ", stdout);
        field(variant, "target");
        fputs("\n      ABI: ", stdout);
        field(variant, "goatAbiVersion");
        fputs("\n      C compiler: ", stdout);
        field(variant, "cCompiler");
        fputs("\n      Compatibility: ", stdout);
        if (!valid) {
            fputs("not checked (archive validation failed)", stdout);
        } else {
            goat_native_compatibility_result result;
            goat_native_status status =
                goat_module_native_compatibility(variant, target, &abi, 1, &result);
            if (status == GOAT_NATIVE_OK) {
                ++matches;
                fputs("compatible", stdout);
            } else if (status == GOAT_NATIVE_NO_MATCH) {
                fputs("incompatible", stdout);
                for (unsigned bit = 1; bit <= GOAT_NATIVE_REASON_ABI; bit <<= 1)
                    if (result.reasons & bit)
                        printf("; %s", goat_native_compatibility_reason_string(bit));
                for (uint64_t bit = 1; bit <= GOAT_CPU_SVE2; bit <<= 1)
                    if (result.missing_cpu_features & bit)
                        printf("; missing CPU %s", goat_native_cpu_feature_name(bit));
            } else
                fputs(goat_native_status_string(status), stdout);
        }
        putchar('\n');
    }
    if (valid && !matches)
        fputs("  Warning: no compatible native variant; VM execution remains possible.\n", stdout);
    if (valid && matches > 1)
        fputs("  Warning: ambiguous compatible native variants.\n", stdout);
    fputs("  Loading: not checked (metadata inspection; isolated probe is not implemented yet).\n",
          stdout);
}

static void module_text(const module_graph_node_t *node,
                        inspection_t *inspection,
                        const goat_native_environment *target) {
    fputs("Archive: ", stdout);
    quoted(node->archive, strlen(node->archive));
    printf("\n  Validation: %s\n", node->snapshot ? "passed" : "failed");
    metadata_record_t *record = find_metadata(inspection, node->archive);
    if (!record) {
        fputs("  Metadata: unavailable\n", stdout);
        return;
    }
    yyjson_val *metadata = yyjson_doc_get_root(record->doc);
    fputs("  Module: ", stdout);
    field(metadata, "name");
    putchar('@');
    field(metadata, "version");
    fputs("\n  Main hash: ", stdout);
    field(metadata, "mainHash");
    fputs("\n  Producers: ", stdout);
    yyjson_val *item = yyjson_obj_get(metadata, "producers");
    if (item)
        value(item);
    else
        fputs("(none)", stdout);
    const char *information[] = {"authors", "description", "license", "homepage"};
    for (size_t i = 0; i < sizeof(information) / sizeof(*information); ++i) {
        item = yyjson_obj_get(metadata, information[i]);
        if (item) {
            printf("\n  %s: ", information[i]);
            value(item);
        }
    }
    fputs("\n  Dependencies: ", stdout);
    item = yyjson_obj_get(metadata, "dependencies");
    if (item)
        value(item);
    else
        fputs("[]", stdout);
    putchar('\n');
    exports(metadata);
    variants(metadata, target, node->snapshot != NULL);
}

static bool timeout_valid(const char *text) {
    uint64_t value = 0;
    if (!*text)
        return false;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9')
            return false;
        unsigned digit = (unsigned)(*text - '0');
        if (value > (UINT64_MAX / 1000 - digit) / 10)
            return false;
        value = value * 10 + digit;
    }
    return value != 0;
}

static void help(void) {
    fputs("Usage: goat-inspect [--module-dir DIR] [--target PROFILE.json]\n"
          "                    [--probe-timeout SECONDS] ARCHIVE\n"
          "Inspect metadata and the entire dependency graph without executing code.\n"
          "Paths are literal; no suffix is added. -- ends option parsing.\n"
          "This stage provides text metadata inspection only. JSON reporting and\n"
          "isolated native probing are not implemented yet. No loading is claimed.\n"
          "--probe-timeout validates a future probe budget (default 5 seconds).\n"
          "--help, --version: display information without opening archives.\n",
          stdout);
}

int inspect_module_command(int argc, char **argv) {
    const char *archive = NULL, *directory = NULL, *profile = NULL, *timeout = NULL;
    bool positional = false, show_help = false, show_version = false;
    const char *error = NULL;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (!positional && strcmp(arg, "--") == 0) {
            positional = true;
            continue;
        }
        if (!positional && (!strcmp(arg, "--help") || !strcmp(arg, "-h"))) {
            show_help = true;
            continue;
        }
        if (!positional && !strcmp(arg, "--version")) {
            show_version = true;
            continue;
        }
        if (!positional
            && (!strcmp(arg, "--module-dir") || !strcmp(arg, "--target")
                || !strcmp(arg, "--probe-timeout"))) {
            const char **destination = !strcmp(arg, "--module-dir") ? &directory
                                       : !strcmp(arg, "--target")   ? &profile
                                                                    : &timeout;
            if (*destination || i + 1 == argc || !argv[i + 1][0]) {
                error = "Missing or repeated option value.";
                break;
            }
            *destination = argv[++i];
            continue;
        }
        if (!positional && arg[0] == '-') {
            error = !strcmp(arg, "--json") ? "JSON reporting is not implemented yet (#223)."
                                           : "Unknown option.";
            break;
        }
        if (archive || !arg[0]) {
            error = "Exactly one archive path is required.";
            break;
        }
        archive = arg;
    }
    if (!error && timeout && !timeout_valid(timeout))
        error = "Invalid --probe-timeout value.";
    if (!error && !show_help && !show_version && !archive)
        error = "Exactly one archive path is required.";
    if (error) {
        fprintf(stderr, "%s\n", error);
        return 2;
    }
    if (show_help) {
        help();
        return 0;
    }
    if (show_version) {
        fputs("goat-inspect 1 (module format v1)\n", stdout);
        return 0;
    }
    goat_native_environment target;
    goat_native_environment_detect(&target);
    if (profile) {
        fs_error_t fs = {0};
        void *bytes = NULL;
        size_t size = 0;
        if (!read_fs_regular_file(profile, SIZE_MAX - 1, &bytes, &size, &fs)) {
            fprintf(stderr,
                    "Cannot read target profile: %s\n",
                    fs.operation ? fs.operation : "I/O error");
            clear_fs_error(&fs);
            return 2;
        }
        goat_module_json_error parsed = goat_native_environment_read(bytes, size, &target);
        FREE(bytes);
        clear_fs_error(&fs);
        if (parsed) {
            fprintf(stderr, "Invalid target profile: %s\n", goat_module_json_error_string(parsed));
            return 2;
        }
    }
    const char *os_names[] = {"unknown", "linux", "windows"};
    const char *arch_names[] = {"unknown", "x86", "x86_64", "aarch64"};
    const char *runtime_names[] = {"unknown", "glibc", "musl", "msvcrt", "ucrt"};
    printf("Target: %s; OS=%s architecture=%s bits=%u endian=%s runtime=%s\n",
           profile ? "explicit profile" : "current environment",
           os_names[target.os],
           arch_names[target.architecture],
           target.bits,
           target.little_endian ? "little" : "big",
           runtime_names[target.runtime]);
    printf("  OS version: %s %u.%u.%u.%u; runtime version: %s %u.%u.%u.%u\n",
           target.os_version.known ? "known" : "unknown",
           target.os_version.components[0],
           target.os_version.components[1],
           target.os_version.components[2],
           target.os_version.components[3],
           target.runtime_version.known ? "known" : "unknown",
           target.runtime_version.components[0],
           target.runtime_version.components[1],
           target.runtime_version.components[2],
           target.runtime_version.components[3]);
    fputs("  CPU:", stdout);
    for (uint64_t bit = 1; bit <= GOAT_CPU_SVE2; bit <<= 1)
        if (target.cpu_features & bit)
            printf(" %s", goat_native_cpu_feature_name(bit));
    putchar('\n');
    module_search_t *search = NULL;
    module_search_error_t search_error = {0};
    if (prepare_module_search_from_environment(archive, directory, &search, &search_error)) {
        char *message = format_module_search_error(&search_error, NULL, NULL, archive);
        printf("Inspection: failed\n%s\n", message ? message : "Cannot open archive/search paths.");
        FREE(message);
        clear_module_search_error(&search_error);
        return 1;
    }
    inspection_t inspection = {0};
    module_validation_options_t options = {.max_archive = SIZE_MAX,
                                           .max_expanded = SIZE_MAX,
                                           .metadata = capture_metadata,
                                           .metadata_context = &inspection};
    module_graph_t *graph = NULL;
    module_graph_status_t status =
        validate_module_graph(module_search_root(search), search, &options, &graph);
    size_t count = module_graph_node_count(graph);
    const module_graph_node_t **nodes = ALLOC(allocation_size_multiply(count, sizeof(*nodes)));
    for (size_t i = 0; i < count; ++i)
        nodes[i] = module_graph_node(graph, i);
    qsort(nodes, count, sizeof(*nodes), node_compare);
    for (size_t i = 0; i < count; ++i)
        module_text(nodes[i], &inspection, &target);
    FREE(nodes);
    char *diagnostics = format_module_graph_diagnostics(graph);
    if (diagnostics && *diagnostics)
        printf("Diagnostics:\n%s", diagnostics);
    FREE(diagnostics);
    bool success = status == MODULE_GRAPH_OK && !inspection.failed;
    printf("Inspection: %s\n",
           success ? "passed (metadata and graph only; loading not checked)" : "failed");
    free_module_graph(graph);
    free_module_search(search);
    clear_module_search_error(&search_error);
    while (inspection.records) {
        metadata_record_t *next = inspection.records->next;
        FREE(inspection.records->archive);
        yyjson_doc_free(inspection.records->doc);
        FREE(inspection.records);
        inspection.records = next;
    }
    if (ferror(stdout) || fflush(stdout))
        return 1;
    return success ? 0 : 1;
}
