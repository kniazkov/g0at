/** @file module_loading.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Snapshot-bound native preparation, extraction and sticky load policy.
 */
#include "module_loading.h"

#include "lib/hash/sha256.h"
#include "lib/memory/allocate.h"
#include "model/module_library.h"
#include "model/process.h"
#include "module/module_native.h"

#include <stdio.h>
#include <string.h>

typedef struct loading_run_t loading_run_t;

typedef struct {
    loading_run_t *run;
    size_t node, variant;
    const yyjson_doc *metadata;
    zip_view_t payload;
    const char *expected_hash;
    module_library_t *library;
    module_loading_statistics_t statistics;
    char *failure;
    bool corrupt;
} loading_module_t;

struct loading_run_t {
    module_snapshot_t *snapshot;
    module_loading_options_t options;
    cleanup_service_t *cleanup;
    bool owns_cleanup, cleaner_failed;
    os_mutex_t *mutex;
    loading_module_t *modules;
    size_t count;
};

static char *copy_text(const char *text) {
    size_t size = strlen(text) + 1;
    char *copy = ALLOC(size);
    memcpy(copy, text, size);
    return copy;
}

static void destroy_run(void *state) {
    loading_run_t *run = state;
    if (!run)
        return;
    for (size_t i = 0; i < run->count; i++) {
        release_module_library(run->modules[i].library);
        FREE(run->modules[i].failure);
    }
    FREE(run->modules);
    if (run->owns_cleanup)
        destroy_cleanup_service(run->cleanup);
    destroy_os_mutex(run->mutex);
    FREE(run);
}

static module_loading_result_t
result(module_loading_status_t status, size_t node, const char *diagnostic) {
    return (module_loading_result_t){status, node, diagnostic ? copy_text(diagnostic) : NULL};
}

void clear_module_loading_result(module_loading_result_t *value) {
    if (value) {
        FREE(value->diagnostic);
        *value = (module_loading_result_t){0};
    }
}

static bool hash_matches(zip_view_t bytes, const char *expected) {
    uint8_t digest[SHA256_SIZE];
    static const char hex[] = "0123456789abcdef";
    sha256(bytes.data, bytes.size, digest);
    for (size_t i = 0; i < sizeof(digest); i++)
        if (expected[2 * i] != hex[digest[i] >> 4] || expected[2 * i + 1] != hex[digest[i] & 15])
            return false;
    return true;
}

static void fail_module(loading_module_t *module, const char *reason, bool corrupt) {
    module->statistics.unavailable = true;
    module->corrupt = corrupt;
    module->failure = copy_text(reason);
    loading_run_t *run = module->run;
    if (run->options.mode != NATIVE_AUTO || corrupt)
        return;
    const yyjson_val *root = yyjson_doc_get_root(module->metadata);
    const char *name = yyjson_get_str(yyjson_obj_get(root, "name"));
    const char *version = yyjson_get_str(yyjson_obj_get(root, "version"));
    size_t length = allocation_size_add(strlen(reason), strlen(name) + strlen(version) + 64);
    char *message = ALLOC(length);
    snprintf(message, length, "Module %s@%s: %s; using VM", name, version, reason);
    module->statistics.warnings++;
    if (run->options.report)
        run->options.report(run->options.report_state, module->node, message);
    else
        FPRINTF(stderr, "%s\n", message);
    FREE(message);
}

static void load_selected(loading_module_t *module) {
    loading_run_t *run = module->run;
    if (run->cleaner_failed)
        return;
    if (!hash_matches(module->payload, module->expected_hash)) {
        fail_module(module, "native snapshot integrity mismatch", true);
        return;
    }
    if (ensure_cleanup_service(run->cleanup) != CLEANUP_OK) {
        run->cleaner_failed = true;
        fail_module(module,
                    "cleanup service did not become ready; native disabled for this run",
                    false);
        return;
    }
    const char *directory = cleanup_service_directory(run->cleanup);
    module->statistics.extraction_attempts++;
    fs_error_t error = {0};
    char *prefix = join_fs_path(directory, "module-", &error);
    char *private_directory = prefix ? create_fs_temporary(prefix, true, NULL, &error) : NULL;
    FREE(prefix);
#ifdef _WIN32
    const char *filename = "module.dll";
#else
    const char *filename = "module.so";
#endif
    char *path = private_directory ? join_fs_path(private_directory, filename, &error) : NULL;
    FREE(private_directory);
    FILE *file = path ? FOPEN(path, "wbx") : NULL;
    bool written = false;
    if (file) {
        written =
            run->options.write
                ? run->options.write(run->options.write_state,
                                     file,
                                     module->payload.data,
                                     module->payload.size)
                : write_fs_file(file, module->payload.data, module->payload.size, path, &error);
        if (!close_fs_file(file, path, &error))
            written = false;
    }
    if (!written) {
        fail_module(module, "native library extraction failed", false);
        goto done;
    }
    /* Never load an incomplete or altered extraction. Archive bytes are retained
       in memory; the original archive is deliberately never reopened. */
    void *bytes = NULL;
    size_t size = 0;
    if (!read_fs_regular_file(path, module->payload.size, &bytes, &size, &error)) {
        bool corrupt = error.operation && !strcmp(error.operation, "regular file size limit");
        fail_module(module,
                    corrupt ? "extracted native library size mismatch"
                            : "native library extraction could not be verified",
                    corrupt);
        goto done;
    }
    bool intact = hash_matches((zip_view_t){bytes, size}, module->expected_hash);
    FREE(bytes);
    if (!intact) {
        fail_module(module, "extracted native library integrity mismatch", true);
        goto done;
    }
    module->statistics.loader_attempts++;
    module_library_result_t loaded = load_module_library(path, module->metadata, module->variant);
    if (!loaded.library) {
        fail_module(module,
                    loaded.diagnostic ? loaded.diagnostic : "native OS loading failed",
                    false);
    } else {
        module->library = loaded.library;
        module->statistics.loaded = true;
    }
    FREE(loaded.diagnostic);
done:
    clear_fs_error(&error);
    FREE(path);
}

static bool
resolve(void *state, uint32_t specialization_id, const char *symbol, module_native_entry_t *out) {
    (void)specialization_id;
    loading_module_t *module = state;
    loading_run_t *run = module->run;
    process_t *process = module_snapshot_process(run->snapshot);
    /* Never wait for the loader mutex while retaining the model lock: another
       native thread may need that lock to finish or publish its result. */
    size_t depth = process_execution_suspend(process);
    lock_os_mutex(run->mutex);
    if (!module->statistics.loaded && !module->statistics.unavailable && !run->cleaner_failed)
        load_selected(module);
    bool success = module->library && resolve_module_library_entry(module->library, symbol, out);
    if (!success) {
        out->failure_reported = true;
        out->integrity_failed = module->corrupt;
        out->failure_reason = module->failure       ? module->failure
                              : run->cleaner_failed ? "cleanup service unavailable for this run"
                                                    : "native adapter resolution failed";
    }
    unlock_os_mutex(run->mutex);
    process_execution_resume(process, depth);
    return success;
}

module_loading_result_t prepare_module_snapshot_native(module_snapshot_t *snapshot,
                                                       const module_loading_options_t *options) {
    if (!snapshot || !options || options->mode < NATIVE_OFF || options->mode > NATIVE_REQUIRED
        || module_snapshot_native_state(snapshot))
        return result(MODULE_LOADING_ARGUMENT, SIZE_MAX, "invalid or repeated native preparation");
    process_t *process = module_snapshot_process(snapshot);
    if (process->execution || process->active_threads || process->native_failed)
        return result(MODULE_LOADING_ARGUMENT,
                      SIZE_MAX,
                      "native preparation must precede execution");
    size_t count = module_snapshot_count(snapshot);
    for (size_t i = 0; i < count; i++) {
        module_files_t *files = module_snapshot_files(snapshot, i);
        if (module_files_native(files))
            return result(MODULE_LOADING_ARGUMENT, i, "module already has native bindings");
        for (size_t j = 0; j < module_files_count(files); j++)
            if (module_file_state(module_files_get(files, (uint32_t)j))
                != MODULE_FILE_UNINITIALIZED)
                return result(MODULE_LOADING_ARGUMENT, i, "module execution already started");
    }
    loading_run_t *run = CALLOC(sizeof(*run));
    run->snapshot = snapshot;
    run->options = *options;
    run->options.environment = NULL;
    run->count = count;
    run->modules = CALLOC(allocation_size_multiply(count, sizeof(*run->modules)));
    if (create_os_mutex(&run->mutex) != OS_THREAD_OK) {
        process->native_failed = true;
        destroy_run(run);
        return result(MODULE_LOADING_BINDING,
                      SIZE_MAX,
                      "native loader synchronization unavailable");
    }
    goat_native_environment environment = {0};
    if (options->environment)
        environment = *options->environment;
    else if (options->mode != NATIVE_OFF)
        goat_native_environment_detect(&environment);
    const uint32_t abi = 1;
    module_loading_result_t outcome = result(MODULE_LOADING_OK, SIZE_MAX, NULL);
    for (size_t i = 0; i < count; i++) {
        loading_module_t *module = run->modules + i;
        module->run = run;
        module->node = i;
        module->variant = SIZE_MAX;
        const goat_module_container *container =
            validated_module_container(module_snapshot_module(snapshot, i));
        module->metadata = goat_module_container_metadata(container);
        if (options->mode == NATIVE_OFF)
            continue;
        goat_native_status selected =
            goat_module_native_select(container, &environment, &abi, 1, &module->variant, NULL);
        if (selected != GOAT_NATIVE_OK && selected != GOAT_NATIVE_NO_MATCH) {
            outcome = result(MODULE_LOADING_PLATFORM, i, goat_native_status_string(selected));
            goto failed;
        }
        yyjson_val *variant =
            yyjson_arr_get(yyjson_obj_get(yyjson_doc_get_root(module->metadata), "native"),
                           module->variant);
        size_t specializations = yyjson_arr_size(yyjson_obj_get(variant, "specializations"));
        if (i == 0 && options->mode == NATIVE_REQUIRED
            && (selected != GOAT_NATIVE_OK || !specializations)) {
            outcome = result(MODULE_LOADING_REQUIRED,
                             0,
                             "main module has no own compatible native specialization");
            goto failed;
        }
        if (selected != GOAT_NATIVE_OK || !specializations) {
            module->variant = SIZE_MAX;
            continue;
        }
        const char *library = yyjson_get_str(yyjson_obj_get(variant, "library"));
        module->expected_hash = yyjson_get_str(yyjson_obj_get(variant, "hash"));
        for (size_t j = 0; j < goat_module_container_native_count(container); j++) {
            goat_container_native payload;
            if (goat_module_container_native_at(container, j, &payload)
                && payload.name_length == strlen(library)
                && !memcmp(payload.name, library, payload.name_length)) {
                module->payload = payload.bytes;
                break;
            }
        }
        if (!module->payload.data) {
            outcome = result(MODULE_LOADING_BINDING, i, "validated native payload is unavailable");
            goto failed;
        }
    }
    run->cleanup = options->cleanup;
    if (options->mode != NATIVE_OFF && !run->cleanup) {
        run->cleanup = create_cleanup_service(NULL);
        run->owns_cleanup = true;
    }
    if (!set_module_snapshot_native_state(snapshot, run, destroy_run)) {
        outcome = result(MODULE_LOADING_ARGUMENT, SIZE_MAX, "native preparation already exists");
        goto failed;
    }
    for (size_t i = 0; i < count; i++) {
        loading_module_t *module = run->modules + i;
        if (module->variant == SIZE_MAX)
            continue;
        yyjson_val *variant =
            yyjson_arr_get(yyjson_obj_get(yyjson_doc_get_root(module->metadata), "native"),
                           module->variant);
        module->statistics.bound_functions = goat_module_native_function_count(variant);
        if (!bind_module_files_native(module_snapshot_files(snapshot, i),
                                      variant,
                                      options->mode == NATIVE_REQUIRED,
                                      resolve,
                                      module,
                                      NULL)) {
            process->native_failed = true;
            return result(MODULE_LOADING_BINDING, i, "native specialization binding failed");
        }
    }
    return outcome;
failed:
    /* A failed preflight cannot be bypassed by executing the already built scopes. */
    process->native_failed = true;
    destroy_run(run);
    return outcome;
}

bool module_snapshot_native_statistics(module_snapshot_t *snapshot,
                                       size_t node,
                                       module_loading_statistics_t *out) {
    if (out)
        *out = (module_loading_statistics_t){0};
    loading_run_t *run = module_snapshot_native_state(snapshot);
    if (!run || !out || node >= run->count)
        return false;
    lock_os_mutex(run->mutex);
    *out = run->modules[node].statistics;
    if (run->cleaner_failed)
        out->unavailable = true;
    unlock_os_mutex(run->mutex);
    return true;
}

const char *module_snapshot_native_directory(module_snapshot_t *snapshot) {
    loading_run_t *run = module_snapshot_native_state(snapshot);
    if (!run)
        return NULL;
    lock_os_mutex(run->mutex);
    const char *path = cleanup_service_directory(run->cleanup);
    unlock_os_mutex(run->mutex);
    return path;
}
