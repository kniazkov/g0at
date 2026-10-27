/** @file module_writer_fault_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Execute the production writer with deterministic publication failures.
 */
#include "vm/module_writer.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static bool injected;

static bool fault(const char *name) {
    const char *selected = getenv("GOAT_WRITER_FAULT");
    return selected && !strcmp(selected, name);
}

static bool failed(fs_error_t *error, const char *operation) {
    injected = true;
    if (error) {
        clear_fs_error(error);
        error->status = FS_IO;
        error->operation = operation;
    }
    return false;
}

static bool
fault_write(FILE *file, const void *data, size_t size, const char *path, fs_error_t *error) {
    if (fault("write")) {
        assert(!size || FWRITE(data, 1, 1, file) == 1);
        return failed(error, "injected partial write");
    }
    return write_fs_file(file, data, size, path, error);
}

static bool fault_close(FILE *file, const char *path, fs_error_t *error) {
    bool result = close_fs_file(file, path, error);
    return fault("close") ? failed(error, "injected close") : result;
}

static module_validation_result_t fault_validate(const char *path,
                                                 const module_validation_options_t *options,
                                                 validated_module_t **out) {
    if (fault("validation")) {
        injected = true;
        *out = NULL;
        return (module_validation_result_t){MODULE_VALIDATION_CORRUPT, 1, 0};
    }
    return validate_module_file(path, options, out);
}

static bool fault_prepare(const char *prepared, const char *destination, fs_error_t *error) {
    if (fault("prepare"))
        return failed(error, "injected permissions");
    /* Reach the rename fault even on local filesystems without ACL support. This
     * is exclusively the fault harness: no real rename will occur in this case. */
    if (fault("rename"))
        return true;
    return prepare_fs_replace(prepared, destination, error);
}

static bool fault_publish(const char *prepared, const char *destination, fs_error_t *error) {
    if (fault("rename"))
        return failed(error, "injected rename");
    return publish_fs_replace(prepared, destination, error);
}

#define write_fs_file fault_write
#define close_fs_file fault_close
#define validate_module_file fault_validate
#define prepare_fs_replace fault_prepare
#define publish_fs_replace fault_publish
#include "vm/module_writer.c"
#undef write_fs_file
#undef close_fs_file
#undef validate_module_file
#undef prepare_fs_replace
#undef publish_fs_replace

#define main writer_harness_main
#include "module_writer_testing.c"
#undef main

int main(int argc, char **argv) {
    int status = writer_harness_main(argc, argv);
    assert(!getenv("GOAT_WRITER_FAULT") || injected);
    return status;
}
