/** @file native_artifact_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Static image export inspection harness, without loading code.
 */
#include "codegen/native_artifact.h"
#include "codegen/native_artifact_validation.h"
#include "lib/allocate.h"
#include "lib/binary_file.h"
#include "lib/file_system.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    if (!fs_arguments(&argc, &argv, NULL))
        return 2;
    if (argc == 4 && !strcmp(argv[1], "--validate")) {
        size_t length = 0;
        char *data = read_binary_file(argv[3], 16u * 1024u * 1024u, &length);
        yyjson_doc *metadata = NULL;
        char *diagnostic = NULL;
        bool ok = data
                  && goat_module_json_read(data, length, GOAT_MODULE_METADATA, &metadata)
                         == GOAT_MODULE_JSON_OK;
        if (ok) {
            ok = validate_native_artifact(argv[2], metadata, 0, &diagnostic);
            size_t image_size = 0;
            void *image = read_binary_file(argv[2], 128u * 1024u * 1024u, &image_size);
            char *bytes_error = NULL;
            bool bytes_ok =
                validate_native_artifact_bytes(image, image_size, metadata, 0, &bytes_error);
            assert(bytes_ok == ok);
            FREE(bytes_error);
            FREE(image);
        }
        if (diagnostic)
            fprintf(stderr, "%s\n", diagnostic);
        FREE(diagnostic);
        FREE(data);
        yyjson_doc_free(metadata);
        free_fs_arguments(argc, argv);
        return ok ? 0 : 1;
    }
    if (argc != 3) {
        free_fs_arguments(argc, argv);
        return 2;
    }
    char *diagnostic = NULL;
    native_artifact_t *artifact = open_native_artifact(argv[1], &diagnostic);
    const void *descriptor = NULL;
    size_t size = 0;
    bool ok = artifact && native_artifact_descriptor(artifact, &descriptor, &size, &diagnostic);
    if (ok) {
        ok = native_artifact_has_function(artifact, argv[2]);
        printf("descriptor=%zu function=%d\n", size, ok);
    }
    if (diagnostic)
        fprintf(stderr, "%s\n", diagnostic);
    FREE(diagnostic);
    close_native_artifact(artifact);
    free_fs_arguments(argc, argv);
    return ok ? 0 : 1;
}
