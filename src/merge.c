/** @file merge.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Nonexecuting module merge with validated atomic publication.
 */
#include "lib/allocate.h"
#include "lib/file_system.h"
#include "lib/string_ext.h"
#include "module/module_information_merge.h"
#include "module/module_native_merge.h"
#include "vm/module_writer.h"

#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <io.h>
#else
#    include <unistd.h>
#endif

static void quoted(FILE *stream, const char *text, size_t length) {
    fputc('"', stream);
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c == '"' || c == '\\')
            fprintf(stream, "\\%c", c);
        else if (c < 32 || c == 127)
            fprintf(stream, "\\u%04x", c);
        else
            fputc(c, stream);
    }
    fputc('"', stream);
}

static void filesystem_error(const char *path, const fs_error_t *error) {
    fputs("goat-merge: ", stderr);
    quoted(stderr, path, strlen(path));
    fprintf(stderr,
            ": %s (filesystem %d, system %lu)\n",
            error->operation ? error->operation : "invalid file destination",
            error->status,
            error->native_code);
}

/* Consumer policy only: all filesystem operations remain in the shared layer. */
static bool destination_ready(const char *path) {
    fs_error_t error = {0};
    fs_info_t info;
    bool ok = false;
    char *parent = absolute_fs_path(path, &error);
    if (!parent)
        goto done;
    if (inspect_fs_path(path, false, &info, &error)) {
        if (info.kind != FS_FILE) {
            error.status = FS_INVALID;
            error.operation = "destination must be a regular file, not a symbolic link";
            goto done;
        }
    } else if (error.status != FS_NOT_FOUND)
        goto done;
    clear_fs_error(&error);
    char *slash = strrchr(parent, '/');
#ifdef _WIN32
    char *backslash = strrchr(parent, '\\');
    if (!slash || (backslash && backslash > slash))
        slash = backslash;
#endif
    if (!slash) {
        error.status = FS_INVALID;
        goto done;
    }
    slash[1] = 0;
    if (!inspect_fs_path(parent, true, &info, &error))
        goto done;
    if (info.kind != FS_DIRECTORY) {
        error.status = FS_INVALID;
        error.operation = "destination parent must be an existing directory";
        goto done;
    }
    ok = true;
done:
    if (!ok)
        filesystem_error(path, &error);
    FREE(parent);
    clear_fs_error(&error);
    return ok;
}

static void validation_error(const module_validation_diagnostic_t *d, void *context) {
    (void)context;
    if (d->state != MODULE_CHECK_FAILED)
        return;
    fputs("goat-merge: source ", stderr);
    quoted(stderr, d->archive, strlen(d->archive));
    static const char *checks[] = {"input", "container", "bytecode", "native"};
    fprintf(stderr,
            ": %s validation failed (detail %d, system %d)",
            checks[d->check],
            d->detail,
            d->system_error);
    if (d->entry.data) {
        fputs(" entry ", stderr);
        quoted(stderr, (const char *)d->entry.data, d->entry.size);
    }
    if (d->field) {
        fputs(" field ", stderr);
        quoted(stderr, d->field, d->field_length);
    }
    fputc('\n', stderr);
}

static bool interactive_input(void) {
#ifdef _WIN32
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(STDIN_FILENO) != 0;
#endif
}

static yyjson_doc *
merged_metadata(const yyjson_doc *base, const yyjson_doc *information, const yyjson_doc *native) {
    yyjson_mut_doc *doc = yyjson_doc_mut_copy((yyjson_doc *)base, NULL);
    if (!doc)
        return NULL;
    yyjson_mut_val *root = yyjson_mut_doc_get_root(doc);
    const char *fields[] = {"description", "license", "homepage", "authors", "producers"};
    bool ok = true;
    for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); ++i) {
        yyjson_mut_obj_remove_str(root, fields[i]);
        yyjson_val *value =
            yyjson_obj_get(yyjson_doc_get_root((yyjson_doc *)information), fields[i]);
        if (value) {
            yyjson_mut_val *copy = yyjson_val_mut_copy(doc, value);
            ok = ok && copy && yyjson_mut_obj_add_val(doc, root, fields[i], copy);
        }
    }
    yyjson_mut_obj_remove_str(root, "native");
    yyjson_val *variants = yyjson_doc_get_root((yyjson_doc *)native);
    if (yyjson_arr_size(variants)) {
        yyjson_mut_val *copy = yyjson_val_mut_copy(doc, variants);
        ok = ok && copy && yyjson_mut_obj_add_val(doc, root, "native", copy);
    }
    yyjson_doc *out = ok ? yyjson_mut_doc_imut_copy(doc, NULL) : NULL;
    yyjson_mut_doc_free(doc);
    return out;
}

static int merge_sources(const char *destination, const char **sources, size_t count) {
    int result = 1;
    validated_module_t **snapshots = calloc(count, sizeof(*snapshots));
    const goat_module_container **containers = calloc(count, sizeof(*containers));
    const yyjson_doc **metadata = calloc(count, sizeof(*metadata));
    goat_information_merge *plan = NULL;
    goat_native_merge_result native = {0};
    yyjson_doc *information = NULL, *output = NULL;
    module_writer_native_t *parts = NULL;
    module_writer_error_t write_error = {0};
    if (!snapshots || !containers || !metadata)
        goto memory;
    if (!destination_ready(destination))
        goto done;
    module_validation_options_t options = {.max_archive = SIZE_MAX,
                                           .max_expanded = SIZE_MAX,
                                           .report = validation_error};
    for (size_t i = 0; i < count; ++i) {
        void *bytes = NULL;
        size_t size = 0;
        fs_error_t read_error = {0};
        if (!read_fs_regular_file(sources[i], UINT32_MAX - 1, &bytes, &size, &read_error)) {
            filesystem_error(sources[i], &read_error);
            clear_fs_error(&read_error);
            goto done;
        }
        module_validation_result_t validated =
            validate_module(bytes, size, sources[i], &options, snapshots + i);
        FREE(bytes);
        if (validated.status)
            goto done;
        containers[i] = validated_module_container(snapshots[i]);
        metadata[i] = goat_module_container_metadata(containers[i]);
    }
    const char *hash =
        yyjson_get_str(yyjson_obj_get(yyjson_doc_get_root((yyjson_doc *)metadata[0]), "mainHash"));
    for (size_t i = 1; i < count; ++i) {
        const char *other = yyjson_get_str(
            yyjson_obj_get(yyjson_doc_get_root((yyjson_doc *)metadata[i]), "mainHash"));
        if (strcmp(hash, other)) {
            fputs("goat-merge: different main hashes; sources are not the same main build.\n",
                  stderr);
            goto done;
        }
    }
    goat_native_merge_status native_error = goat_native_merge(containers, count, &native);
    if (native_error) {
        fprintf(stderr, "goat-merge: %s\n", goat_native_merge_status_string(native_error));
        goto done;
    }
    goat_information_merge_diagnostic diagnostic;
    goat_information_merge_error info_error =
        goat_information_merge_prepare(metadata, count, &plan, &diagnostic);
    if (!info_error)
        info_error = goat_information_merge_interactive(plan,
                                                        interactive_input(),
                                                        stdin,
                                                        stderr,
                                                        &information);
    if (info_error) {
        fprintf(stderr, "goat-merge: %s", goat_information_merge_error_string(info_error));
        if (diagnostic.field)
            fprintf(stderr, " (%s)", diagnostic.field);
        fputc('\n', stderr);
        goto done;
    }
    output = merged_metadata(metadata[0], information, native.metadata);
    if (!output)
        goto memory;
    if (native.count) {
        parts = calloc(native.count, sizeof(*parts));
        if (!parts)
            goto memory;
        for (size_t i = 0; i < native.count; ++i)
            parts[i] = (module_writer_native_t){native.payloads[i].name,
                                                native.payloads[i].name_length,
                                                native.payloads[i].bytes};
    }
    module_writer_input_t input = {output,
                                   goat_module_container_bytecode(containers[0]),
                                   parts,
                                   native.count};
    if (module_write_file(destination, &input, NULL, &write_error)) {
        fprintf(stderr,
                "goat-merge: publication failed (writer %d, JSON %d, ZIP %d).\n",
                write_error.status,
                write_error.json,
                write_error.zip);
        if (write_error.filesystem.status)
            filesystem_error(destination, &write_error.filesystem);
        if (write_error.cleanup.status)
            filesystem_error(write_error.cleanup.path, &write_error.cleanup);
        goto done;
    }
    yyjson_val *root = yyjson_doc_get_root(output);
    quoted(stdout, destination, strlen(destination));
    fputs(": ", stdout);
    yyjson_val *name = yyjson_obj_get(root, "name"), *version = yyjson_obj_get(root, "version");
    quoted(stdout, yyjson_get_str(name), yyjson_get_len(name));
    fputc('@', stdout);
    quoted(stdout, yyjson_get_str(version), yyjson_get_len(version));
    printf("; %zu native variants\n", native.count);
    result = 0;
    goto done;
memory:
    fputs("goat-merge: out of memory.\n", stderr);
done:
    free(parts);
    clear_module_writer_error(&write_error);
    yyjson_doc_free(output);
    yyjson_doc_free(information);
    goat_information_merge_free(plan);
    goat_native_merge_clear(&native);
    if (snapshots)
        for (size_t i = 0; i < count; ++i)
            free_validated_module(snapshots[i]);
    free(snapshots);
    free(containers);
    free(metadata);
    return result;
}

static int command(int argc, char **argv) {
    const char **paths = calloc((size_t)argc, sizeof(*paths));
    if (!paths)
        return 1;
    size_t count = 0;
    bool positional = false, help = false, version = false;
    int result = 2;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (!positional && !strcmp(arg, "--")) {
            positional = true;
            continue;
        }
        if (!positional && (!strcmp(arg, "--help") || !strcmp(arg, "-h"))) {
            help = true;
            continue;
        }
        if (!positional && !strcmp(arg, "--version")) {
            version = true;
            continue;
        }
        if ((!positional && arg[0] == '-' && arg[1]) || !arg[0]) {
            fputs("goat-merge: invalid command argument.\n", stderr);
            goto done;
        }
        paths[count++] = arg;
    }
    if (help || version) {
        fputs(help ? "Usage: goat-merge <destination> <source_1> [<source_2> ...]\n--help, -h: "
                     "help; --version: version; --: end options.\nPaths are literal; '-' is a "
                     "filename, not a standard stream.\n"
                   : "goat-merge 1 (module format v1)\n",
              stdout);
        result = 0;
        goto done;
    }
    if (count < 2) {
        fputs("goat-merge: destination and at least one source are required.\n", stderr);
        goto done;
    }
    size_t unique = 1;
    for (size_t i = 1; i < count; ++i) {
        bool duplicate = false;
        for (size_t j = 1; j < unique; ++j)
            duplicate = duplicate || !strcmp(paths[i], paths[j]);
        if (!duplicate)
            paths[unique++] = paths[i];
    }
    result = merge_sources(paths[0], paths + 1, unique - 1);
done:
    free(paths);
    return result;
}

int main(int argc, char **argv) {
    init_unicode_locale();
    fs_error_t error = {0};
    if (!fs_arguments(&argc, &argv, &error)) {
        fputs("goat-merge: invalid Unicode command line.\n", stderr);
        clear_fs_error(&error);
        return 2;
    }
    int result = command(argc, argv);
    free_fs_arguments(argc, argv);
    return result;
}
