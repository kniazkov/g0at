/** @file module_sources.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Canonical source snapshots and iterative include traversal.
 */
#include "module_sources.h"

#include "lib/memory/allocate.h"

#include <stdint.h>
#include <string.h>

static char *copy_text(const char *text) {
    size_t size = strlen(text) + 1;
    char *result = ALLOC(size);
    memcpy(result, text, size);
    return result;
}

void clear_module_sources_error(module_sources_error_t *error) {
    if (error) {
        FREE(error->path);
        clear_fs_error(&error->filesystem);
        *error = (module_sources_error_t){0};
    }
}

void free_module_sources(module_sources_t *sources) {
    if (!sources)
        return;
    for (size_t i = 0; i < sources->count; i++) {
        module_source_t *file = &sources->files[i];
        FREE(file->path);
        FREE(file->name);
        FREE(file->source);
        FREE(file->targets);
        clear_source_includes(&file->includes);
    }
    FREE(sources->files);
    FREE(sources->descriptor_path);
    FREE(sources->base_directory);
    yyjson_doc_free(sources->descriptor);
    FREE(sources);
}

static char *directory_of(const char *path) {
    const char *last = strrchr(path, '/');
    size_t length = last == path ? 1 : (size_t)(last - path);
#ifdef _WIN32
    if (length == 2 && path[1] == ':')
        length++;
#endif
    char *result = ALLOC(length + 1);
    memcpy(result, path, length);
    result[length] = 0;
    return result;
}

static module_sources_status_t fail(module_sources_error_t *error,
                                    module_sources_status_t status,
                                    const char *path,
                                    const source_include_t *site,
                                    const char *message) {
    if (error) {
        error->status = status;
        FREE(error->path);
        error->path = path ? copy_text(path) : NULL;
        error->row = site ? site->row : 0;
        error->column = site ? site->column : 0;
        error->offset = site ? site->offset : 0;
        error->message = message;
    }
    return status;
}

static char *resolve_path(const char *base, const char *path, fs_error_t *error) {
    bool absolute = path[0] == '/';
#ifdef _WIN32
    bool separator = path[0] == '/' || path[0] == '\\';
    bool unc = separator && (path[1] == '/' || path[1] == '\\');
    bool drive = path[0] && path[1] == ':';
    /* Let the filesystem boundary reject incomplete Windows paths before joining. */
    if ((separator && !unc) || drive)
        return canonical_fs_path(path, error);
    absolute = unc;
#endif
    char *joined = absolute ? copy_text(path) : join_fs_path(base, path, error);
    if (!joined)
        return NULL;
    char *canonical = canonical_fs_path(joined, error);
    FREE(joined);
    return canonical;
}

static void logical_name(module_source_t *file, const char *base) {
    size_t length = strlen(base);
    bool slash = length && base[length - 1] == '/';
    bool inside = !strncmp(file->path, base, length) && (slash || file->path[length] == '/');
    file->external = !inside;
    const char *name = inside ? file->path + length + !slash : strrchr(file->path, '/') + 1;
    file->name = copy_text(name);
}

static module_sources_status_t add_source(module_sources_t *graph,
                                          char *path,
                                          const char *containing,
                                          const source_include_t *site,
                                          const module_sources_options_t *limits,
                                          size_t *total,
                                          size_t *index,
                                          module_sources_error_t *error) {
    for (size_t i = 0; i < graph->count; i++) {
        if (!strcmp(graph->files[i].path, path)) {
            FREE(path);
            *index = i;
            return MODULE_SOURCES_OK;
        }
    }
    if (graph->count == limits->max_files || graph->count >= UINT32_MAX
        || graph->count == SIZE_MAX / sizeof(module_source_t)) {
        FREE(path);
        return fail(error, MODULE_SOURCES_LIMIT, containing, site, "source count limit");
    }
    module_source_t file = {.path = path};
    fs_error_t *fs = error ? &error->filesystem : NULL;
    size_t available = limits->max_total_bytes - *total;
    size_t bound = available < limits->max_file_bytes ? available : limits->max_file_bytes;
    if (!read_fs_regular_file(path, bound, (void **)&file.source, &file.length, fs)) {
        module_sources_status_t status = fail(error,
                                              MODULE_SOURCES_IO,
                                              containing ? containing : path,
                                              site,
                                              "cannot read regular source file");
        FREE(path);
        return status;
    }
    source_include_error_t syntax = {0};
    if (!parse_source_includes(path, file.source, file.length, &file.includes, &syntax)) {
        source_include_t position = {.row = syntax.row,
                                     .column = syntax.column,
                                     .offset = syntax.offset};
        module_sources_status_t status =
            fail(error, MODULE_SOURCES_SYNTAX, path, &position, syntax.message);
        FREE(path);
        FREE(file.source);
        return status;
    }
    logical_name(&file, graph->base_directory);
    if (file.includes.count > SIZE_MAX / sizeof(size_t)) {
        FREE(file.path);
        FREE(file.name);
        FREE(file.source);
        clear_source_includes(&file.includes);
        return fail(error, MODULE_SOURCES_LIMIT, containing, site, "include count limit");
    }
    file.targets = CALLOC(file.includes.count * sizeof(size_t));
    module_source_t *files = ALLOC((graph->count + 1) * sizeof(*files));
    if (graph->count)
        memcpy(files, graph->files, graph->count * sizeof(*files));
    FREE(graph->files);
    graph->files = files;
    *index = graph->count++;
    files[*index] = file;
    *total += file.length;
    return MODULE_SOURCES_OK;
}

module_sources_status_t module_sources_read(const char *input,
                                            bool descriptor,
                                            const module_sources_options_t *options,
                                            module_sources_t **out,
                                            module_sources_error_t *error) {
    if (out)
        *out = NULL;
    clear_module_sources_error(error);
    module_sources_options_t limits = {65536, 64 * 1024 * 1024, 256 * 1024 * 1024};
    if (options)
        limits = *options;
    if (!out || !input || !limits.max_files || !limits.max_file_bytes || !limits.max_total_bytes
        || limits.max_file_bytes == SIZE_MAX)
        return fail(error, MODULE_SOURCES_ARGUMENT, input, NULL, "invalid source graph arguments");
    fs_error_t *fs = error ? &error->filesystem : NULL;
    char *canonical = canonical_fs_path(input, fs);
    if (!canonical)
        return fail(error, MODULE_SOURCES_IO, input, NULL, "cannot canonicalize input");
    module_sources_t *graph = CALLOC(sizeof(*graph));
    graph->base_directory = directory_of(canonical);
    module_sources_status_t status = MODULE_SOURCES_OK;
    char *main_path = canonical;
    if (descriptor) {
        graph->descriptor_path = canonical;
        main_path = NULL;
        void *bytes = NULL;
        size_t length = 0;
        if (!read_fs_regular_file(canonical, limits.max_file_bytes, &bytes, &length, fs)) {
            status =
                fail(error, MODULE_SOURCES_IO, canonical, NULL, "cannot read regular descriptor");
            goto done;
        }
        goat_module_json_error json =
            goat_module_json_read(bytes, length, GOAT_MODULE_GMOD, &graph->descriptor);
        FREE(bytes);
        if (json) {
            if (error)
                error->json = json;
            status = fail(error,
                          MODULE_SOURCES_JSON,
                          canonical,
                          NULL,
                          goat_module_json_error_string(json));
            goto done;
        }
        const char *main =
            yyjson_get_str(yyjson_obj_get(yyjson_doc_get_root(graph->descriptor), "main"));
        main_path = resolve_path(graph->base_directory, main, fs);
        if (!main_path) {
            status = fail(error, MODULE_SOURCES_IO, canonical, NULL, "cannot resolve main");
            goto done;
        }
    }
    size_t total = 0, main_index = 0;
    status = add_source(graph,
                        main_path,
                        graph->descriptor_path,
                        NULL,
                        &limits,
                        &total,
                        &main_index,
                        error);
    if (status)
        goto done;

    /* Each frame is an existing file index and next include; source arrays may move. */
    typedef struct {
        size_t file, next;
    } frame_t;

    size_t capacity = 16, depth = 1;
    frame_t *stack = CALLOC(capacity * sizeof(*stack));
    while (depth) {
        frame_t *frame = &stack[depth - 1];
        module_source_t *source = &graph->files[frame->file];
        if (frame->next == source->includes.count) {
            depth--;
            continue;
        }
        size_t parent = frame->file, edge = frame->next++;
        source_include_t *site = &source->includes.items[edge];
        char *directory = directory_of(source->path);
        char *path = resolve_path(directory, site->path, fs);
        FREE(directory);
        if (!path) {
            status = fail(error, MODULE_SOURCES_IO, source->path, site, "cannot resolve include");
            break;
        }
        size_t before = graph->count, target = 0;
        status = add_source(graph, path, source->path, site, &limits, &total, &target, error);
        if (status)
            break;
        graph->files[parent].targets[edge] = target;
        if (graph->count == before)
            continue;
        if (depth == capacity) {
            size_t next_capacity = capacity * 2;
            frame_t *next = ALLOC(next_capacity * sizeof(*next));
            memcpy(next, stack, depth * sizeof(*next));
            FREE(stack);
            stack = next;
            capacity = next_capacity;
        }
        stack[depth++] = (frame_t){target, 0};
    }
    FREE(stack);
done:
    if (status)
        free_module_sources(graph);
    else
        *out = graph;
    return status;
}
