/** @file module_validation.c
 * @copyright 2026 Ivan Kniazkov
 * @brief One bounded, nonexecuting validation pipeline for every module consumer.
 */
#include "module_validation.h"

#include "lib/files/file_system.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

struct validated_module {
    goat_module_container *container;
    bytecode_t *code;
    uint32_t entry;
    char *archive;
};

typedef struct {
    module_validation_options_t options;
    module_validation_result_t result;
    const char *archive;
    bytecode_t *code;
    uint32_t entry;
    bool inspected;
} validation;

static validation begin(const char *archive, const module_validation_options_t *options) {
    validation v = {.archive = archive ? archive : "<memory>",
                    .options = {.max_archive = SIZE_MAX, .max_expanded = SIZE_MAX}};
    if (options)
        v.options = *options;
    return v;
}

static void report(validation *v, module_validation_diagnostic_t d) {
    d.archive = v->archive;
    if (d.state == MODULE_CHECK_FAILED) {
        v->result.failures++;
        if (!v->result.status)
            v->result.status = MODULE_VALIDATION_CORRUPT;
    } else if (d.state == MODULE_CHECK_SKIPPED)
        v->result.skipped++;
    if (v->options.report)
        v->options.report(&d, v->options.context);
}

static void
skip(validation *v, module_validation_check_t check, module_validation_check_t prerequisite) {
    report(v,
           (module_validation_diagnostic_t){.check = check,
                                            .state = MODULE_CHECK_SKIPPED,
                                            .prerequisite = prerequisite,
                                            .index = SIZE_MAX});
}

static void native_report(goat_native_status status, size_t index, void *context) {
    validation *v = context;
    report(v,
           (module_validation_diagnostic_t){.check = MODULE_CHECK_NATIVE,
                                            .state = MODULE_CHECK_FAILED,
                                            .entry = {(const uint8_t *)"module.json", 11},
                                            .detail = status,
                                            .index = index,
                                            .native = {status, index}});
}

static void inspect(const goat_module_container *container, void *context) {
    validation *v = context;
    v->inspected = true;
    if (v->options.metadata)
        v->options.metadata(v->archive,
                            goat_module_container_metadata(container),
                            v->options.metadata_context);
    zip_view_t bytes = goat_module_container_bytecode(container);
    char *json = NULL;
    size_t length = 0;
    goat_module_json_error je = goat_module_json_write(goat_module_container_metadata(container),
                                                       GOAT_MODULE_METADATA,
                                                       false,
                                                       &json,
                                                       &length);
    module_validation_diagnostic_t d = {.check = MODULE_CHECK_BYTECODE,
                                        .entry = {(const uint8_t *)"bytecode.bin", 12},
                                        .index = SIZE_MAX};
    if (!bytes.data) {
        d.state = MODULE_CHECK_SKIPPED;
        d.prerequisite = MODULE_CHECK_CONTAINER;
        d.container_prerequisite = GOAT_CONTAINER_CHECK_CONTENT;
    } else if (je) {
        d.state = MODULE_CHECK_FAILED;
        d.detail = MODULE_BYTECODE_JSON;
        d.bytecode.json_error = je;
        if (je == GOAT_MODULE_JSON_MEMORY)
            v->result.status = MODULE_VALIDATION_MEMORY;
    } else {
        module_bytecode_status_t status = read_module_bytecode(json,
                                                               length,
                                                               bytes.data,
                                                               bytes.size,
                                                               &v->code,
                                                               &v->entry,
                                                               &d.bytecode);
        d.state = status ? MODULE_CHECK_FAILED : MODULE_CHECK_PASSED;
        d.detail = status;
        d.index = d.bytecode.index;
        if (d.bytecode.json_error == GOAT_MODULE_JSON_MEMORY)
            v->result.status = MODULE_VALIDATION_MEMORY;
    }
    goat_module_json_free(json);
    report(v, d);
    if (v->result.status == MODULE_VALIDATION_MEMORY) {
        skip(v, MODULE_CHECK_NATIVE, MODULE_CHECK_BYTECODE);
        return;
    }
    d = (module_validation_diagnostic_t){.check = MODULE_CHECK_NATIVE,
                                         .entry = {(const uint8_t *)"module.json", 11},
                                         .index = SIZE_MAX};
    goat_native_status ns =
        goat_module_native_validate_diagnostics(container, &d.native, native_report, v);
    if (!ns) {
        d.state = MODULE_CHECK_PASSED;
        report(v, d);
    }
}

/* Container callback adaptation is kept separate from semantic checks. */
static void container_report(const goat_container_diagnostic *event, void *context) {
    validation *v = context;
    module_validation_diagnostic_t d = {.check = MODULE_CHECK_CONTAINER,
                                        .state = (module_check_state_t)event->state,
                                        .prerequisite = MODULE_CHECK_CONTAINER,
                                        .entry = event->entry,
                                        .field = event->field,
                                        .field_length = event->field_length,
                                        .container_prerequisite = event->prerequisite,
                                        .detail = event->check,
                                        .index = event->error.entry_index,
                                        .container = event->error};
    if (event->error.status == GOAT_CONTAINER_MEMORY)
        v->result.status = MODULE_VALIDATION_MEMORY;
    else if (event->error.status == GOAT_CONTAINER_LIMIT
             && v->result.status != MODULE_VALIDATION_MEMORY)
        v->result.status = MODULE_VALIDATION_LIMIT;
    report(v, d);
}

void free_validated_module(validated_module_t *module) {
    if (!module)
        return;
    goat_module_container_free(module->container);
    if (module->code)
        free_bytecode(module->code);
    free(module->archive);
    free(module);
}

module_validation_result_t validate_module(const void *data,
                                           size_t size,
                                           const char *archive,
                                           const module_validation_options_t *options,
                                           validated_module_t **out) {
    validation v = begin(archive, options);
    if (out)
        *out = NULL;
    if (!out || (!data && size) || size > v.options.max_archive) {
        v.result.status =
            !out || (!data && size) ? MODULE_VALIDATION_ARGUMENT : MODULE_VALIDATION_LIMIT;
        report(&v,
               (module_validation_diagnostic_t){.check = MODULE_CHECK_INPUT,
                                                .state = MODULE_CHECK_FAILED,
                                                .detail = v.result.status,
                                                .index = SIZE_MAX});
        skip(&v, MODULE_CHECK_CONTAINER, MODULE_CHECK_INPUT);
        skip(&v, MODULE_CHECK_BYTECODE, MODULE_CHECK_INPUT);
        skip(&v, MODULE_CHECK_NATIVE, MODULE_CHECK_INPUT);
        return v.result;
    }
    report(&v,
           (module_validation_diagnostic_t){.check = MODULE_CHECK_INPUT,
                                            .state = MODULE_CHECK_PASSED,
                                            .index = SIZE_MAX});
    goat_module_container *container = NULL;
    goat_container_error error;
    goat_container_status status = goat_module_container_read_report(data,
                                                                     size,
                                                                     v.options.max_expanded,
                                                                     &container,
                                                                     &error,
                                                                     container_report,
                                                                     &v,
                                                                     inspect);
    if (!v.inspected) {
        skip(&v, MODULE_CHECK_BYTECODE, MODULE_CHECK_CONTAINER);
        skip(&v, MODULE_CHECK_NATIVE, MODULE_CHECK_CONTAINER);
    }
    if (status || v.result.failures) {
        if (status == GOAT_CONTAINER_MEMORY)
            v.result.status = MODULE_VALIDATION_MEMORY;
        if (!v.result.status)
            v.result.status = MODULE_VALIDATION_CORRUPT;
        goat_module_container_free(container);
        if (v.code)
            free_bytecode(v.code);
        return v.result;
    }
    validated_module_t *module = calloc(1, sizeof(*module));
    size_t length = strlen(v.archive);
    char *label = malloc(length + 1);
    if (!module || !label) {
        free(module);
        free(label);
        goat_module_container_free(container);
        free_bytecode(v.code);
        v.result.status = MODULE_VALIDATION_MEMORY;
        report(&v,
               (module_validation_diagnostic_t){.check = MODULE_CHECK_INPUT,
                                                .state = MODULE_CHECK_FAILED,
                                                .detail = MODULE_VALIDATION_MEMORY,
                                                .index = SIZE_MAX});
        return v.result;
    }
    memcpy(label, v.archive, length + 1);
    *module = (validated_module_t){container, v.code, v.entry, label};
    *out = module;
    return v.result;
}

module_validation_result_t validate_module_file(const char *path,
                                                const module_validation_options_t *options,
                                                validated_module_t **out) {
    validation v = begin(path, options);
    if (out)
        *out = NULL;
    fs_error_t error = {0};
    if (!out || !path || !validate_fs_path(path, strlen(path), &error)) {
        clear_fs_error(&error);
        return validate_module(NULL, 1, path, options, out);
    }
    if (v.options.max_archive >= UINT32_MAX)
        v.options.max_archive = UINT32_MAX - 1;
    FILE *file = FOPEN(path, "rb");
    int code = errno;
    void *data = NULL;
    size_t size = 0;
    module_validation_status_t failure = MODULE_VALIDATION_IO;
    if (file) {
        size_t capacity = v.options.max_archive < 65536 ? v.options.max_archive : 65536;
        data = malloc(capacity ? capacity : 1);
        if (!data) {
            failure = MODULE_VALIDATION_MEMORY;
        } else {
            for (;;) {
                size += FREAD((uint8_t *)data + size, 1, capacity - size, file);
                if (FERROR(file)) {
                    code = errno;
                    break;
                }
                if (FEOF(file)) {
                    failure = MODULE_VALIDATION_OK;
                    break;
                }
                int ch = FGETC(file);
                if (ch == EOF) {
                    failure = FERROR(file) ? MODULE_VALIDATION_IO : MODULE_VALIDATION_OK;
                    code = errno;
                    break;
                }
                if (capacity == v.options.max_archive) {
                    failure = MODULE_VALIDATION_LIMIT;
                    break;
                }
                size_t next = capacity > (SIZE_MAX - 1) / 2 ? SIZE_MAX : capacity * 2 + 1;
                if (next > v.options.max_archive)
                    next = v.options.max_archive;
                void *grown = realloc(data, next);
                if (!grown) {
                    failure = MODULE_VALIDATION_MEMORY;
                    break;
                }
                data = grown;
                capacity = next;
                ((uint8_t *)data)[size++] = (uint8_t)ch;
            }
        }
        if (FCLOSE(file) && !failure) {
            failure = MODULE_VALIDATION_IO;
            code = errno;
        }
    }
    if (!failure) {
        module_validation_result_t result = validate_module(data, size, path, options, out);
        free(data);
        return result;
    }
    free(data);
    v.result.status = failure;
    report(
        &v,
        (module_validation_diagnostic_t){.check = MODULE_CHECK_INPUT,
                                         .state = MODULE_CHECK_FAILED,
                                         .detail = failure,
                                         .system_error = failure == MODULE_VALIDATION_IO ? code : 0,
                                         .index = SIZE_MAX});
    skip(&v, MODULE_CHECK_CONTAINER, MODULE_CHECK_INPUT);
    skip(&v, MODULE_CHECK_BYTECODE, MODULE_CHECK_INPUT);
    skip(&v, MODULE_CHECK_NATIVE, MODULE_CHECK_INPUT);
    return v.result;
}

const goat_module_container *validated_module_container(const validated_module_t *module) {
    return module ? module->container : NULL;
}

const bytecode_t *validated_module_bytecode(const validated_module_t *module) {
    return module ? module->code : NULL;
}

const char *validated_module_archive(const validated_module_t *module) {
    return module ? module->archive : NULL;
}

uint32_t validated_module_entry(const validated_module_t *module) {
    return module ? module->entry : 0;
}
