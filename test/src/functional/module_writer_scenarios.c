/** @file module_writer_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Deterministic archive profile and atomic publication contracts (#367).
 */
#ifndef _WIN32
#    define _POSIX_C_SOURCE 200809L
#endif
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/format_binary.h"
#include "test/support/format_zip.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"
#include "yyjson.h"

#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <windows.h>
#else
#    include <sys/stat.h>
#    include <unistd.h>
#endif
#define REQUIRE(x) scenario_require(r, (x), #x)

typedef struct {
    const void *data;
    size_t size;
} bytes_t;

typedef struct {
    const char *host, *fault, *fixtures, *vectors, *kind;
    yyjson_val *all, *fixture;
    bool allow_acl, skipped;
} configuration_t;

static bool free_doc(void *p) {
    yyjson_doc_free(p);
    return true;
}

static bool free_malloc(void *p) {
    free(p);
    return true;
}

static bool free_entries(void *p) {
    clear_fs_entries(p);
    free(p);
    return true;
}

static const char *field(scenario_run_t *r, yyjson_val *v, const char *key) {
    yyjson_val *x = yyjson_obj_get(v, key);
    REQUIRE(yyjson_is_str(x) && strlen(yyjson_get_str(x)) == yyjson_get_len(x));
    return yyjson_get_str(x);
}

static yyjson_val *fixture(scenario_run_t *r, configuration_t *c, const char *name) {
    char id[128];
    snprintf(id, sizeof(id), "integration.module_writer.%s", name);
    for (size_t i = 0; i < yyjson_arr_size(c->all); i++) {
        yyjson_val *v = yyjson_arr_get(c->all, i);
        if (!strcmp(field(r, v, "id"), id))
            return v;
    }
    REQUIRE(false);
    return NULL;
}

static bytes_t read_bytes(scenario_run_t *r, const char *path) {
    size_t n;
    const void *p = language_read(r, path, &n);
    return (bytes_t){p, n};
}

static bytes_t blob(scenario_run_t *r, configuration_t *c, const char *relative) {
    REQUIRE(!strncmp(relative, "blobs/", 6) && strlen(relative) == 74 && !strstr(relative, ".."));
    bytes_t bytes = read_bytes(r, language_join(r, c->fixtures, relative));
    char hash[65];
    format_binary_sha256(bytes.data, bytes.size, hash);
    REQUIRE(!memcmp(hash, relative + 6, 64));
    return bytes;
}

static bytes_t input(scenario_run_t *r, configuration_t *c, yyjson_val *v) {
    return blob(r, c, field(r, v, "input"));
}

static bytes_t output(scenario_run_t *r, configuration_t *c, yyjson_val *v) {
    return blob(r, c, field(r, v, "output"));
}

static void equal(scenario_run_t *r, const char *label, bytes_t a, bytes_t b) {
    scenario_require_bytes(r, label, a.data, a.size, b.data, b.size, false);
}

static yyjson_val *parse(scenario_run_t *r, bytes_t b) {
    yyjson_doc *d = yyjson_read(b.data, b.size, 0);
    REQUIRE(d);
    scenario_defer(r, free_doc, d);
    return yyjson_doc_get_root(d);
}

static uint16_t u16(const unsigned char *p) {
    return (uint16_t)((unsigned)p[0] | (unsigned)p[1] << 8);
}

static uint32_t u32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static format_zip_info_t inspect(scenario_run_t *r, bytes_t zip, size_t index) {
    format_zip_info_t info;
    char error[256];
    REQUIRE(format_zip_inspect(zip.data, zip.size, index, &info, error, sizeof(error)));
    return info;
}

static bytes_t extract(scenario_run_t *r, bytes_t zip, size_t index) {
    char error[256];
    size_t size;
    void *p = format_zip_extract(zip.data,
                                 zip.size,
                                 index,
                                 32 * 1024 * 1024,
                                 &size,
                                 error,
                                 sizeof(error));
    REQUIRE(p);
    scenario_defer(r, free_malloc, p);
    return (bytes_t){p, size};
}

static bytes_t part(scenario_run_t *r, bytes_t zip, const char *name) {
    format_zip_info_t all = inspect(r, zip, SIZE_MAX);
    for (size_t i = 0; i < all.count; i++) {
        format_zip_info_t info = inspect(r, zip, i);
        if (info.name_size == strlen(name) && !memcmp(info.name, name, info.name_size))
            return extract(r, zip, i);
    }
    REQUIRE(false);
    return (bytes_t){0};
}

static int order_names(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static void profile(scenario_run_t *r, configuration_t *c, yyjson_val *v, bytes_t zip) {
    yyjson_val *parts = yyjson_obj_get(v, "parts");
    REQUIRE(yyjson_is_arr(parts) && yyjson_arr_size(parts) >= 2);
    size_t count = yyjson_arr_size(parts);
    format_zip_info_t all = inspect(r, zip, SIZE_MAX);
    REQUIRE(all.count == count && !all.archive_comment_size);
    const char **names = calloc(count, sizeof(*names));
    REQUIRE(names);
    scenario_defer(r, free_malloc, names);
    names[0] = "module.json";
    names[1] = "bytecode.bin";
    size_t n = 2;
    for (size_t i = 0; i < count; i++) {
        const char *name = field(r, yyjson_arr_get(parts, i), "name");
        if (strcmp(name, names[0]) && strcmp(name, names[1])) {
            REQUIRE(n < count);
            names[n++] = name;
        }
    }
    REQUIRE(n == count);
    qsort(names + 2, count - 2, sizeof(*names), order_names);
    for (size_t i = 0; i < count; i++) {
        format_zip_info_t info = inspect(r, zip, i);
        REQUIRE(info.name_size == strlen(names[i]) && !memcmp(info.name, names[i], info.name_size));
        REQUIRE(info.method == 8 && info.date == 33 && info.time == 0 && info.flags == 0x800
                && info.made_by == (3 * 256 + 20) && info.internal == 0
                && info.external == ((uint32_t)0100644 << 16) && !info.extra_size
                && !info.comment_size);
        const unsigned char *data = zip.data;
        REQUIRE(info.local_offset <= zip.size && zip.size - info.local_offset >= 30
                && info.central_offset <= zip.size && zip.size - info.central_offset >= 46);
        const unsigned char *local = data + info.local_offset,
                            *central = data + info.central_offset;
        REQUIRE(u32(local) == 0x04034b50 && u16(local + 4) == 20 && u16(local + 6) == 0x800
                && u16(local + 8) == 8 && u16(local + 10) == 0 && u16(local + 12) == 33
                && u16(local + 28) == 0);
        REQUIRE(u32(local + 14) == info.crc && u32(local + 18) == info.packed_size
                && u32(local + 22) == info.size && u16(central + 6) == 20);
        yyjson_val *expected = NULL;
        for (size_t j = 0; j < count; j++) {
            yyjson_val *x = yyjson_arr_get(parts, j);
            if (!strcmp(field(r, x, "name"), names[i])) {
                REQUIRE(!expected);
                expected = x;
            }
        }
        REQUIRE(expected);
        equal(r,
              "independent normalized ZIP part",
              blob(r, c, field(r, expected, "data")),
              extract(r, zip, i));
    }
    REQUIRE(zip.size >= 22);
    const unsigned char *tail = (const unsigned char *)zip.data + zip.size - 22;
    REQUIRE(!memcmp(tail, "PK\005\006", 4) && !tail[20] && !tail[21]);
    char hash[65];
    format_binary_sha256(zip.data, zip.size, hash);
    REQUIRE(!strcmp(hash, field(r, v, "sha256")));
}

static scenario_command_result_t invoke(scenario_run_t *r,
                                        configuration_t *c,
                                        bytes_t bytes,
                                        const char *destination,
                                        yyjson_val *limits,
                                        const char *fault) {
    char a[32], b[32];
    const char *la = NULL, *lb = NULL;
    if (yyjson_is_arr(limits)) {
        REQUIRE(yyjson_arr_size(limits) == 2);
        snprintf(a,
                 sizeof(a),
                 "%llu",
                 (unsigned long long)yyjson_get_uint(yyjson_arr_get(limits, 0)));
        snprintf(b,
                 sizeof(b),
                 "%llu",
                 (unsigned long long)yyjson_get_uint(yyjson_arr_get(limits, 1)));
        la = a;
        lb = b;
    }
    const char *host = fault ? c->fault : c->host;
    const char *args[] =
        {host, destination ? "file" : "memory", destination ? destination : "unused", la, lb, NULL};
    const char *const *env =
        fault ? language_environment_override(r, "GOAT_WRITER_FAULT", fault) : NULL;
    child_options_t options = {.executable = host,
                               .arguments = args,
                               .stdin_data = bytes.data,
                               .stdin_size = bytes.size,
                               .environment = env};
    scenario_command_result_t result = scenario_command_capture(r, &options);
    if (result.exit_code)
        REQUIRE(!result.out_size);
    return result;
}

static void status(scenario_run_t *r, scenario_command_result_t result, yyjson_val *statuses) {
    REQUIRE(yyjson_is_arr(statuses));
    bool found = false;
    for (size_t i = 0; i < yyjson_arr_size(statuses); i++) {
        yyjson_val *n = yyjson_arr_get(statuses, i);
        REQUIRE(yyjson_is_int(n));
        found |= result.exit_code == yyjson_get_int(n);
    }
    REQUIRE(found);
}

static void memory(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    yyjson_val *v = c->fixture;
    bytes_t bytes = input(r, c, v);
    scenario_command_result_t result = invoke(r, c, bytes, NULL, yyjson_obj_get(v, "limits"), NULL);
    status(r, result, yyjson_obj_get(v, "statuses"));
    if (!result.exit_code) {
        bytes_t zip = {result.out, result.out_size};
        equal(r, "exact deterministic archive", output(r, c, v), zip);
        profile(r, c, v, zip);
        yyjson_val *before = parse(r, part(r, bytes, "module.json")),
                   *after = parse(r, part(r, zip, "module.json"));
        REQUIRE(!strcmp(field(r, before, "mainHash"), field(r, after, "mainHash")));
        const char *id = field(r, v, "id");
        if (strstr(id, "golden") || strstr(id, "normalize-"))
            equal(r,
                  "published golden vector",
                  read_bytes(r, language_join(r, c->vectors, "empty.gbin")),
                  zip);
        if (strstr(id, "native-") && !strstr(id, "invalid-") && !strstr(id, "tied")) {
            char hash[65];
            format_binary_sha256(zip.data, zip.size, hash);
            REQUIRE(
                !strcmp(hash, "98617e59853dee57926692a6698f67c341eb69e9cc118e7b55ec746f3a6c5102"));
            equal(r,
                  "native permutations and idempotence",
                  output(r, c, fixture(r, c, "native-profile")),
                  zip);
        }
        if (strstr(id, "information"))
            equal(r,
                  "information order invariant",
                  output(r, c, fixture(r, c, "information")),
                  zip);
        if (strstr(id, "tied"))
            equal(r,
                  "tied compiler payload association",
                  output(r, c, fixture(r, c, "native-tied")),
                  zip);
    }
}

static void api(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    const char *args[] = {c->host, NULL};
    scenario_command_result_t result = language_command(r, c->host, args, NULL);
    language_check(r, result, 0, "Writer API checks passed\n", 25, NULL, 0);
}

static fs_entries_t *entries(scenario_run_t *r, const char *path) {
    fs_entries_t *e = calloc(1, sizeof(*e));
    REQUIRE(e);
    scenario_defer(r, free_entries, e);
    fs_error_t error = {0};
    bool ok = list_fs_directory(path, e, &error);
    clear_fs_error(&error);
    REQUIRE(ok);
    return e;
}

static void unchanged_directory(scenario_run_t *r, const char *path, fs_entries_t *before) {
    fs_entries_t *after = entries(r, path);
    REQUIRE(before->count == after->count);
    for (size_t i = 0; i < before->count; i++) {
        bool found = false;
        for (size_t j = 0; j < after->count; j++)
            found |= !strcmp(before->names[i], after->names[j]);
        REQUIRE(found);
    }
}

static void permissions(scenario_run_t *r, const char *path, bool set) {
#ifndef _WIN32
    if (set)
        REQUIRE(chmod(path, 0640) == 0);
    struct stat st;
    REQUIRE(stat(path, &st) == 0);
    REQUIRE((st.st_mode & 07777) == 0640);
#else
    (void)r;
    (void)path;
    (void)set;
#endif
}

static bool link_file(scenario_run_t *r, const char *target, const char *alias, bool symbolic) {
#ifdef _WIN32
    int nt = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, target, -1, NULL, 0),
        na = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, alias, -1, NULL, 0);
    REQUIRE(nt > 0 && na > 0);
    wchar_t *t = language_owned(r, ALLOC((size_t)nt * sizeof(*t))),
            *a = language_owned(r, ALLOC((size_t)na * sizeof(*a)));
    REQUIRE(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, target, -1, t, nt) == nt
            && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, alias, -1, a, na) == na);
    bool ok = symbolic ? CreateSymbolicLinkW(a, t, 2) != 0 : CreateHardLinkW(a, t, NULL) != 0;
    if (!ok && symbolic && GetLastError() == ERROR_INVALID_PARAMETER)
        ok = CreateSymbolicLinkW(a, t, 0) != 0;
    if (!ok && symbolic && GetLastError() == ERROR_PRIVILEGE_NOT_HELD)
        return false;
#else
    bool ok = (symbolic ? symlink(target, alias) : link(target, alias)) == 0;
#endif
    REQUIRE(ok);
    return true;
}

static void kind(scenario_run_t *r, const char *path, fs_kind_t expected) {
    fs_info_t info;
    fs_error_t error = {0};
    bool ok = inspect_fs_path(path, false, &info, &error);
    clear_fs_error(&error);
    REQUIRE(ok && info.kind == expected);
}

static bool
unsupported_acl(scenario_run_t *r, configuration_t *c, scenario_command_result_t result) {
    if (!c->allow_acl || result.exit_code != 8)
        return false;
    const char needle[] = "FS 5";
    bool found = false;
    for (size_t i = 0; i + sizeof(needle) - 1 <= result.err_size; i++)
        found |= !memcmp((const char *)result.err + i, needle, sizeof(needle) - 1);
    REQUIRE(found);
    c->skipped = true;
    puts("Explicit local-only skip: filesystem does not support required ACL transfer");
    return true;
}

static void publication(scenario_run_t *r, void *arg) {
    configuration_t *c = arg;
    const char *which = c->kind;
    const char *directory =
        language_join(r, scenario_work_directory(scenario_run_context(r)), "publication");
    language_directory(r, directory);
    const char *destination = language_join(r, directory, "результат без расширения");
    yyjson_val *base = fixture(r, c, "golden"), *full = fixture(r, c, "native-profile");
    bytes_t base_input = input(r, c, base), full_input = input(r, c, full),
            golden = output(r, c, base), current = output(r, c, full);
    bool fresh = !strcmp(which, "new"), replace = !strcmp(which, "replace"),
         hard = !strcmp(which, "hardlink"), same = !strcmp(which, "same-output");
    scenario_command_result_t initial =
        invoke(r, c, fresh || replace ? base_input : full_input, destination, NULL, NULL);
    REQUIRE(initial.exit_code == 0);
    if (fresh || replace)
        current = golden;
    equal(r, "initial publication", current, read_bytes(r, destination));
    permissions(r, destination, true);
    fs_entries_t *before = entries(r, directory);
    if (fresh) {
        unchanged_directory(r, directory, before);
        return;
    }
    if (replace || hard || same) {
        const char *alias = language_join(r, directory, "old-hard-link");
        if (hard) {
            REQUIRE(link_file(r, destination, alias, false));
            before = entries(r, directory);
        }
        bytes_t source = same ? read_bytes(r, destination) : hard ? base_input : full_input;
        scenario_command_result_t result = invoke(r, c, source, destination, NULL, NULL);
        if (unsupported_acl(r, c, result)) {
            equal(r, "unsupported ACL preserves old bytes", current, read_bytes(r, destination));
            if (hard)
                equal(r, "unsupported ACL preserves alias", current, read_bytes(r, alias));
            permissions(r, destination, false);
            unchanged_directory(r, directory, before);
            return;
        }
        REQUIRE(result.exit_code == 0);
        equal(r,
              "atomic replacement",
              hard ? golden : output(r, c, full),
              read_bytes(r, destination));
        permissions(r, destination, false);
        unchanged_directory(r, directory, before);
        if (hard) {
            equal(r, "hard link retains previous bytes", current, read_bytes(r, alias));
            language_remove(r, alias);
            scenario_command_result_t restore = invoke(r, c, full_input, destination, NULL, NULL);
            REQUIRE(restore.exit_code == 0);
            equal(r, "restore full archive", current, read_bytes(r, destination));
        }
        return;
    }
    if (!strcmp(which, "symlink")) {
        const char *target = language_join(r, directory, "target"),
                   *alias = language_join(r, directory, "link");
        language_write(r, target, "keep target", 11);
        if (!link_file(r, target, alias, true)) {
            c->skipped = true;
            puts("SKIP: Windows symlink privilege error 1314");
            return;
        }
        before = entries(r, directory);
        scenario_command_result_t result = invoke(r, c, base_input, alias, NULL, NULL);
        REQUIRE(result.exit_code == 7);
        kind(r, alias, FS_LINK);
        equal(r, "symlink target untouched", (bytes_t){"keep target", 11}, read_bytes(r, target));
        unchanged_directory(r, directory, before);
        language_remove(r, alias);
    } else if (!strcmp(which, "fifo")) {
#ifndef _WIN32
        const char *fifo = language_join(r, directory, "fifo");
        REQUIRE(mkfifo(fifo, 0600) == 0);
        before = entries(r, directory);
        scenario_command_result_t result = invoke(r, c, base_input, fifo, NULL, NULL);
        REQUIRE(result.exit_code == 7);
        struct stat st;
        REQUIRE(stat(fifo, &st) == 0 && S_ISFIFO(st.st_mode));
        unchanged_directory(r, directory, before);
        language_remove(r, fifo);
#else
        REQUIRE(false);
#endif
    } else if (!strcmp(which, "missing-parent") || !strcmp(which, "directory")) {
        const char *path = !strcmp(which, "directory")
                               ? directory
                               : language_join(r, directory, "missing-parent/output");
        scenario_command_result_t result = invoke(r, c, base_input, path, NULL, NULL);
        REQUIRE(result.exit_code == 7 || result.exit_code == 8);
        unchanged_directory(r, directory, before);
    } else if (!strncmp(which, "fault-", 6)) {
        const char *fault = which + 6;
        scenario_command_result_t result = invoke(r, c, base_input, destination, NULL, fault);
        REQUIRE(result.exit_code == (!strcmp(fault, "validation") ? 6 : 8));
        if (!strcmp(fault, "write")) {
            const char needle[] = "injected partial write: 1 byte";
            const char *error = result.err;
            bool found = false;
            /* stderr remains a text stream on Windows: accept an exact LF or
             * CRLF-terminated marker line, without weakening the write proof. */
            for (size_t start = 0; start < result.err_size;) {
                size_t end = start;
                while (end < result.err_size && error[end] != '\n')
                    end++;
                size_t length = end - start;
                if (length && error[start + length - 1] == '\r')
                    length--;
                found |= end < result.err_size && length == sizeof(needle) - 1
                         && !memcmp(error + start, needle, sizeof(needle) - 1);
                start = end < result.err_size ? end + 1 : end;
            }
            REQUIRE(found);
        }
        unchanged_directory(r, directory, before);
    } else {
        yyjson_val *v = c->fixture;
        REQUIRE(v);
        scenario_command_result_t result =
            invoke(r, c, input(r, c, v), destination, yyjson_obj_get(v, "limits"), NULL);
        status(r, result, yyjson_obj_get(v, "statuses"));
        REQUIRE(result.exit_code != 0);
        unchanged_directory(r, directory, before);
    }
    equal(r, "prepublication failure preserves old bytes", current, read_bytes(r, destination));
    permissions(r, destination, false);
}

int main(int argc, char **argv) {
#ifdef _WIN32
    fs_error_t ae = {0};
    if (!fs_arguments(&argc, &argv, &ae)) {
        clear_fs_error(&ae);
        return 1;
    }
#endif
    int result = 1;
    bool allow = false, keep = false, valid = argc >= 8 && argc <= 10;
    for (int i = 8; i < argc && valid; i++) {
        if (!strcmp(argv[i], "--allow-unsupported-acl") && !allow)
            allow = true;
        else if (scenario_keep_work_argument(argv[i]) && !keep)
            keep = true;
        else
            valid = false;
    }
    if (valid) {
        fs_error_t error = {0};
        char *host = absolute_fs_path(argv[2], &error);
        clear_fs_error(&error);
        char *fault = absolute_fs_path(argv[3], &error);
        clear_fs_error(&error);
        char *directory = join_fs_path(argv[6], "module-writer", &error);
        clear_fs_error(&error);
        char *path = directory ? join_fs_path(directory, "scenarios.json", &error) : NULL;
        clear_fs_error(&error);
        void *raw = NULL;
        size_t size = 0;
        bool read = host && fault && path
                    && read_fs_regular_file(path, 2 * 1024 * 1024, &raw, &size, &error);
        clear_fs_error(&error);
        yyjson_doc *doc = read ? yyjson_read(raw, size, 0) : NULL;
        yyjson_val *all = yyjson_doc_get_root(doc);
        if (yyjson_is_arr(all) && yyjson_arr_size(all)) {
            size_t ordinary = yyjson_arr_size(all), count = ordinary + 14;
            for (size_t i = 0; i < ordinary; i++) {
                const char *id = yyjson_get_str(yyjson_obj_get(yyjson_arr_get(all, i), "id"));
                if (id && (strstr(id, ".invalid-") || strstr(id, ".limit-one")))
                    count++;
            }
            configuration_t *config = CALLOC(count * sizeof(*config));
            scenario_definition_t *defs = CALLOC(count * sizeof(*defs));
            char(*ids)[128] = CALLOC(count * sizeof(*ids));
            const char *fixed[] = {"api",
                                   "new",
                                   "replace",
                                   "hardlink",
                                   "same-output",
                                   "missing-parent",
                                   "directory",
                                   "symlink",
                                   "fifo",
                                   "fault-write",
                                   "fault-close",
                                   "fault-validation",
                                   "fault-prepare",
                                   "fault-rename"};
            for (size_t i = 0; i < 14; i++) {
                snprintf(ids[i],
                         sizeof(ids[i]),
                         "integration.module_writer.%s%s",
                         i ? "publication." : "",
                         fixed[i]);
                config[i] = (configuration_t){host,
                                              fault,
                                              directory,
                                              argv[7],
                                              fixed[i],
                                              all,
                                              NULL,
                                              allow,
                                              false};
                defs[i] = (scenario_definition_t){ids[i],
                                                  120000,
                                                  i ? publication : api,
                                                  config + i,
                                                  NULL};
#ifdef _WIN32
                if (!strcmp(fixed[i], "fifo"))
                    defs[i].skip_reason = "POSIX FIFO creation has no Windows equivalent";
#endif
            }
            size_t index = 14;
            for (size_t i = 0; i < ordinary; i++) {
                yyjson_val *v = yyjson_arr_get(all, i);
                config[index] =
                    (configuration_t){host, fault, directory, argv[7], NULL, all, v, allow, false};
                defs[index] = (scenario_definition_t){yyjson_get_str(yyjson_obj_get(v, "id")),
                                                      30000,
                                                      memory,
                                                      config + index,
                                                      NULL};
                index++;
            }
            for (size_t i = 0; i < ordinary; i++) {
                yyjson_val *v = yyjson_arr_get(all, i);
                const char *id = yyjson_get_str(yyjson_obj_get(v, "id"));
                if (id && (strstr(id, ".invalid-") || strstr(id, ".limit-one"))) {
                    const char *suffix = strrchr(id, '.') + 1;
                    snprintf(ids[index],
                             sizeof(ids[index]),
                             "integration.module_writer.publication.%s",
                             suffix);
                    config[index] = (configuration_t){host,
                                                      fault,
                                                      directory,
                                                      argv[7],
                                                      suffix,
                                                      all,
                                                      v,
                                                      allow,
                                                      false};
                    defs[index] = (scenario_definition_t){ids[index],
                                                          120000,
                                                          publication,
                                                          config + index,
                                                          NULL};
                    index++;
                }
            }
            scenario_options_t options = {.build_directory = argv[4], .data_directory = argv[5]};
            const char *selection[] = {argv[1], keep ? "--keep-work" : NULL};
            result = scenario_registry_main(defs, count, keep ? 2 : 1, selection, &options);
            if (!result)
                for (size_t i = 0; i < count; i++)
                    if (config[i].skipped)
                        result = 77;
            FREE(ids);
            FREE(defs);
            FREE(config);
        } else
            fputs("Cannot load writer fixtures\n", stderr);
        yyjson_doc_free(doc);
        FREE(raw);
        FREE(path);
        FREE(directory);
        FREE(host);
        FREE(fault);
    } else
        fputs("usage: module_writer_scenarios ID HOST FAULT_HOST BUILD DATA FIXTURES VECTORS "
              "[--keep-work] [--allow-unsupported-acl]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
