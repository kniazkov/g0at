/** @file inspect_probe.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Isolated native trials with independent bounded result transport.
 */
#include "inspect_probe.h"

#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "model/module_library.h"
#ifdef GOAT_INSPECT_PROBE_TESTING
#    include "lib/process/process_channel.h"
#endif

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#define PROBE_ARGUMENT "--goat-inspect-native-probe"
#define PROBE_MAGIC UINT32_C(0x47505231)
#define PROBE_FINAL 16u
#define PROBE_ABI 32u
#define PROBE_INFRA 64u
#define PROBE_FAILURE 128u

typedef struct {
    uint32_t magic, sequence, stage, flags;
    char diagnostic[512];
} probe_message_t;

typedef struct {
    uintptr_t channel;
    probe_message_t message;
} probe_writer_t;

#ifdef GOAT_INSPECT_PROBE_TESTING
static bool test_fault(const char *name) {
    const char *fault = getenv("GOAT_INSPECT_PROBE_FAULT");
    return fault && !strcmp(fault, name);
}

static void test_ready_barrier(const char *directory) {
    char *path = fs_environment("GOAT_INSPECT_PROBE_READY_MARKER", NULL);
    if (!path)
        return;
    FILE *file = FOPEN(path, "wb");
    FREE(path);
    if (!file)
        _Exit(90);
    FPRINTF(file, "%s\n", directory);
    FCLOSE(file);
    char *gate = fs_environment("GOAT_INSPECT_PROBE_READY_GATE", NULL);
    fs_info_t info;
    while (gate && !inspect_fs_path(gate, false, &info, NULL))
        process_pause(10);
    FREE(gate);
}
#endif

const char *inspect_probe_state_name(inspect_probe_state_t state) {
    static const char *const names[] = {"passed", "failed", "crashed", "timeout"};
    return (unsigned)state < sizeof(names) / sizeof(*names) ? names[state] : "failed";
}

const char *inspect_probe_stage_name(inspect_probe_stage_t stage) {
    static const char *const names[] =
        {"start", "load", "descriptor", "symbols", "unload", "exit", "capture"};
    return (unsigned)stage < sizeof(names) / sizeof(*names) ? names[stage] : "start";
}

/* OS diagnostics may use a legacy encoding and truncation must never split UTF-8.
 * Preserve printable ASCII, escape every other byte, and bound complete escapes. */
static void diagnostic_bytes(char output[512], const char *input, bool append) {
    static const char hex[] = "0123456789abcdef";
    size_t used = append ? strlen(output) : 0;
    if (append && used && used + 2 < 512) {
        output[used++] = ';';
        output[used++] = ' ';
    }
    for (const unsigned char *p = (const unsigned char *)input; p && *p; p++) {
        size_t needed = *p >= 32 && *p < 127 ? 1 : 4;
        if (used + needed >= 512)
            break;
        if (needed == 1)
            output[used++] = (char)*p;
        else {
            output[used++] = '\\';
            output[used++] = 'x';
            output[used++] = hex[*p >> 4];
            output[used++] = hex[*p & 15];
        }
    }
    output[used] = 0;
}

static void send_progress(probe_writer_t *writer) {
    writer->message.magic = PROBE_MAGIC;
    writer->message.sequence++;
    if (!child_write_result(writer->channel, &writer->message, sizeof(writer->message)))
        _Exit(1);
}

static void observe_load(module_library_stage_t stage, bool completed, void *context) {
    probe_writer_t *writer = context;
    writer->message.stage = INSPECT_PROBE_LOAD + stage;
    if (completed)
        writer->message.flags |= 1u << stage;
    send_progress(writer);
}

static bool parse_unsigned(const char *text, uint64_t *out) {
    if (!text || !*text)
        return false;
    for (const char *p = text; *p; p++)
        if (*p < '0' || *p > '9')
            return false;
    errno = 0;
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 10);
    if (errno || !end || *end)
        return false;
    *out = value;
    return true;
}

bool inspect_probe_main(int argc, char **argv, int *exit_code) {
    if (argc < 2 || strcmp(argv[1], PROBE_ARGUMENT))
        return false;
    *exit_code = 1;
    uint64_t channel = 0, variant = 0;
    if (argc != 7 || !parse_unsigned(argv[2], &channel) || !channel || channel > UINTPTR_MAX
        || !parse_unsigned(argv[5], &variant) || variant > SIZE_MAX)
        return true;
    probe_writer_t writer = {.channel = (uintptr_t)channel};
    writer.message.stage = INSPECT_PROBE_START;
    send_progress(&writer);
    /* Only this child changes its environment. All temporary descendants inherit
     * the janitor-owned directory; the parent's environment remains untouched. */
#ifdef _WIN32
    bool environment_ok = !_putenv_s("TEMP", argv[6]) && !_putenv_s("TMP", argv[6]);
#else
    bool environment_ok =
        !setenv("TMPDIR", argv[6], 1) && !setenv("TMP", argv[6], 1) && !setenv("TEMP", argv[6], 1);
#endif
    void *bytes = NULL;
    size_t size = 0;
    fs_error_t error = {0};
    yyjson_doc *metadata = NULL;
    if (!environment_ok || !read_fs_regular_file(argv[3], SIZE_MAX - 1, &bytes, &size, &error)
        || goat_module_json_read(bytes, size, GOAT_MODULE_METADATA, &metadata)
               != GOAT_MODULE_JSON_OK) {
        writer.message.flags |= PROBE_INFRA | PROBE_FAILURE | PROBE_FINAL;
        snprintf(writer.message.diagnostic,
                 sizeof(writer.message.diagnostic),
                 "Cannot prepare the native probe child.");
        send_progress(&writer);
        FREE(bytes);
        clear_fs_error(&error);
        child_close_channel(writer.channel);
        return true;
    }
    FREE(bytes);
    clear_fs_error(&error);
    module_library_result_t loaded =
        load_module_library_observed(argv[4], metadata, (size_t)variant, observe_load, &writer);
    inspect_probe_stage_t failure_stage = (inspect_probe_stage_t)writer.message.stage;
    if (!loaded.valid) {
        writer.message.flags |= PROBE_FAILURE;
        if (writer.message.flags & 1u)
            writer.message.flags |= PROBE_ABI;
        diagnostic_bytes(writer.message.diagnostic,
                         loaded.diagnostic ? loaded.diagnostic : "Native library load failed.",
                         false);
        /* Persist ABI failure before unload, which can itself hang or crash. */
        send_progress(&writer);
    }
    bool unloaded = true;
    char *unload_diagnostic = NULL;
    if (writer.message.flags & 1u) {
        writer.message.stage = INSPECT_PROBE_UNLOAD;
        send_progress(&writer);
        unloaded = release_module_library_checked(loaded.library, &unload_diagnostic);
        loaded.library = NULL;
#ifdef GOAT_INSPECT_PROBE_TESTING
        if (test_fault("unload"))
            unloaded = false;
        if (test_fault("diagnostic")) {
            unloaded = false;
            FREE(unload_diagnostic);
            unload_diagnostic = ALLOC(701);
            for (size_t i = 0; i < 700; i++)
                unload_diagnostic[i] = (char)(i % 2 ? 0xff : 0xe2);
            unload_diagnostic[700] = 0;
        }
#endif
        if (unloaded)
            writer.message.flags |= 8u;
        else {
            failure_stage = INSPECT_PROBE_UNLOAD;
            writer.message.flags |= PROBE_FAILURE;
            diagnostic_bytes(writer.message.diagnostic,
                             unload_diagnostic ? unload_diagnostic
                                               : "Explicit native library unload failed.",
                             true);
        }
        send_progress(&writer);
    }
    release_module_library(loaded.library);
    FREE(loaded.diagnostic);
    FREE(unload_diagnostic);
    yyjson_doc_free(metadata);
    writer.message.stage =
        (writer.message.flags & PROBE_FAILURE) ? failure_stage : INSPECT_PROBE_EXIT;
    writer.message.flags |= PROBE_FINAL;
    send_progress(&writer);
    child_close_channel(writer.channel);
#ifdef GOAT_INSPECT_PROBE_TESTING
    if (test_fault("exit-nonzero"))
        _Exit(99);
    if (test_fault("exit-hang"))
        for (;;)
            process_pause(10);
#endif
    *exit_code = loaded.valid && unloaded ? 0 : 1;
    return true;
}

static bool write_payload(const char *path, const void *bytes, size_t size, fs_error_t *error) {
    FILE *file = FOPEN(path, "wbx");
    if (!file)
        return false;
    bool written = write_fs_file(file, bytes, size, path, error);
    bool closed = close_fs_file(file, path, error);
    return written && closed;
}

static void infrastructure_failure(inspect_probe_result_t *result, const char *text) {
    result->state = INSPECT_PROBE_FAILED;
    result->infrastructure_error = true;
    snprintf(result->diagnostic, sizeof(result->diagnostic), "%s", text);
}

static bool decode_progress(inspect_probe_result_t *result, bool *final) {
    const child_output_t *input = &result->child.result;
    *final = false;
    bool interrupted = result->child.status == CHILD_TIMED_OUT
                       || result->child.status == CHILD_SIGNALED
                       || (result->child.status == CHILD_EXITED && result->child.exit_code != 0);
    bool complete = !input->truncated && (!(input->size % sizeof(probe_message_t)) || interrupted);
    uint32_t sequence = 0, flags = 0;
    for (size_t offset = 0; offset + sizeof(probe_message_t) <= input->size;
         offset += sizeof(probe_message_t)) {
        probe_message_t message;
        memcpy(&message, input->data + offset, sizeof(message));
        if (*final || message.magic != PROBE_MAGIC || message.sequence != ++sequence
            || message.stage > INSPECT_PROBE_CAPTURE || message.flags > 255
            || (message.flags & flags) != flags
            || !memchr(message.diagnostic, 0, sizeof(message.diagnostic)))
            return false;
        flags = message.flags;
        result->stage = (inspect_probe_stage_t)message.stage;
        result->load_completed = (flags & 1u) != 0;
        result->descriptor_completed = (flags & 2u) != 0;
        result->symbols_completed = (flags & 4u) != 0;
        result->unload_completed = (flags & 8u) != 0;
        result->abi_error = (flags & PROBE_ABI) != 0;
        result->infrastructure_error = (flags & PROBE_INFRA) != 0;
        diagnostic_bytes(result->diagnostic, message.diagnostic, false);
        *final = (flags & PROBE_FINAL) != 0;
    }
    return complete;
}

void inspect_probe_module(cleanup_service_t *service,
                          const goat_module_container *container,
                          size_t variant_index,
                          size_t ordinal,
                          uint64_t timeout_ms,
                          inspect_probe_result_t *result) {
    memset(result, 0, sizeof(*result));
    result->state = INSPECT_PROBE_FAILED;
    result->child.status = CHILD_START_ERROR;
    result->service_status = ensure_cleanup_service(service);
    if (result->service_status != CLEANUP_OK) {
        infrastructure_failure(result, "Cannot start native-probe cleanup service.");
        return;
    }
#ifdef GOAT_INSPECT_PROBE_TESTING
    test_ready_barrier(cleanup_service_directory(service));
#endif
    const yyjson_doc *metadata = goat_module_container_metadata(container);
    const yyjson_val *root = metadata ? yyjson_doc_get_root((yyjson_doc *)metadata) : NULL;
    const yyjson_val *variant = yyjson_arr_get(yyjson_obj_get(root, "native"), variant_index);
    const char *name = yyjson_get_str(yyjson_obj_get(variant, "library"));
    goat_container_native native = {0};
    bool found = false;
    for (size_t i = 0; name && i < goat_module_container_native_count(container); i++) {
        if (goat_module_container_native_at(container, i, &native)
            && strlen(name) == native.name_length
            && !memcmp(name, native.name, native.name_length)) {
            found = true;
            break;
        }
    }
    if (!found) {
        infrastructure_failure(result,
                               "Selected native payload is absent from the validated snapshot.");
        return;
    }
    fs_error_t error = {0};
    char number[40];
    snprintf(number, sizeof(number), "probe-%zu", ordinal);
    char *prefix = join_fs_path(cleanup_service_directory(service), number, &error);
    char *directory = prefix ? create_fs_temporary(prefix, true, NULL, &error) : NULL;
    const char *basename = strrchr(name, '/');
    char *native_directory = directory ? join_fs_path(directory, "native", &error) : NULL;
    bool native_directory_created =
        native_directory && create_fs_directory(native_directory, &error);
    char *library_path =
        native_directory_created
            ? join_fs_path(native_directory, basename ? basename + 1 : name, &error)
            : NULL;
    char *metadata_path = directory ? join_fs_path(directory, "probe-metadata.json", &error) : NULL;
    char *json = NULL;
    size_t json_size = 0;
    bool prepared =
        directory && library_path && metadata_path
        && goat_module_json_write(metadata, GOAT_MODULE_METADATA, false, &json, &json_size)
               == GOAT_MODULE_JSON_OK
        && write_payload(library_path, native.bytes.data, native.bytes.size, &error)
        && write_payload(metadata_path, json, json_size, &error);
    if (!prepared) {
        infrastructure_failure(result,
                               "Cannot prepare native-probe files in the cleanup-owned directory.");
        goto done;
    }
    char index[32];
    snprintf(index, sizeof(index), "%zu", variant_index);
    const char *arguments[] = {"goat-inspect",
                               PROBE_ARGUMENT,
                               CHILD_RESULT_ARGUMENT,
                               metadata_path,
                               library_path,
                               index,
                               directory,
                               NULL};
    child_options_t options = {.working_directory = directory,
                               .arguments = arguments,
                               .deadline = child_deadline_after(timeout_ms),
                               .result_channel = true};
#ifdef GOAT_INSPECT_PROBE_TESTING
    char *missing_executable = NULL;
    if (test_fault("start")) {
        missing_executable = join_fs_path(directory, "missing-probe-executable", NULL);
        options.executable = missing_executable;
    }
#endif
    result->service_status = run_cleanup_process(service, &options, &result->child);
#ifdef GOAT_INSPECT_PROBE_TESTING
    FREE(missing_executable);
    if (test_fault("capture"))
        result->child.status = CHILD_IO_ERROR;
#endif
    if (result->service_status != CLEANUP_OK) {
        infrastructure_failure(result, "Native-probe cleanup service communication failed.");
        goto done;
    }
    bool final = false;
    bool protocol = decode_progress(result, &final);
    if (!protocol) {
        result->stage = INSPECT_PROBE_CAPTURE;
        infrastructure_failure(result, "Invalid or incomplete native-probe result transport.");
    } else if (result->child.status == CHILD_TIMED_OUT) {
        result->state = INSPECT_PROBE_TIMEOUT;
        if (!result->diagnostic[0])
            snprintf(result->diagnostic,
                     sizeof(result->diagnostic),
                     "Native-probe deadline expired.");
    } else if (result->child.status == CHILD_SIGNALED
               || (result->child.status == CHILD_EXITED && !final
                   && result->child.exit_code != 0)) {
        result->state = INSPECT_PROBE_CRASHED;
        if (!result->diagnostic[0])
            snprintf(result->diagnostic,
                     sizeof(result->diagnostic),
                     "Native-probe child terminated abnormally.");
    } else if (result->child.status == CHILD_START_ERROR) {
        infrastructure_failure(result, "Cannot start native-probe child.");
    } else if (result->child.status != CHILD_EXITED) {
        result->stage = INSPECT_PROBE_CAPTURE;
        infrastructure_failure(result, "Native-probe output capture failed.");
    } else if (!final && result->stage >= INSPECT_PROBE_LOAD) {
        snprintf(result->diagnostic,
                 sizeof(result->diagnostic),
                 "Native-probe child exited before completing the load/unload trial.");
    } else if (!final) {
        infrastructure_failure(result, "Native-probe child exited without a complete result.");
    } else if (result->child.exit_code == 0 && result->load_completed
               && result->descriptor_completed && result->symbols_completed
               && result->unload_completed && !result->diagnostic[0] && !result->abi_error
               && !result->infrastructure_error) {
        result->state = INSPECT_PROBE_PASSED;
    } else if (result->child.exit_code != 0 && !result->abi_error && !result->infrastructure_error
               && !result->diagnostic[0]) {
        result->state = INSPECT_PROBE_CRASHED;
        result->stage = INSPECT_PROBE_EXIT;
        snprintf(result->diagnostic,
                 sizeof(result->diagnostic),
                 "Native-probe child terminated abnormally after reporting completion.");
    } else if (!result->diagnostic[0]) {
        infrastructure_failure(result, "Native-probe result disagrees with child exit status.");
    }

done:
    goat_module_json_free(json);
    FREE(metadata_path);
    FREE(library_path);
    FREE(native_directory);
    FREE(directory);
    FREE(prefix);
    clear_fs_error(&error);
    /* Removal belongs exclusively to the external cleaner, including failures. */
}
