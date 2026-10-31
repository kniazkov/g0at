/** @file module_merge_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent archive-local merge/publication contracts (#374).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "lib/strings/string_ext.h"
#include "test/support/format_binary.h"
#include "test/support/format_json.h"
#include "test/support/format_zip.h"
#include "test/support/inspect_report.h"
#include "test/support/language_execution.h"
#include "test/support/merge_constructor.h"
#include "test/support/merge_terminal.h"
#include "test/support/registry.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <windows.h>
#else
#    include <errno.h>
#    include <sys/socket.h>
#    include <sys/stat.h>
#    include <sys/un.h>
#    include <unistd.h>
#endif
#define REQUIRE(x) scenario_require(r, (x), #x)
#define ARGS(...) ((const char *const[]){__VA_ARGS__, NULL})
typedef yyjson_mut_val jval_t;

typedef struct {
    const void *data;
    size_t size;
} bytes_t;

typedef struct {
    const char *merge, *inspect, *cc, *source, *format;
    jval_t *record;
    bool keep, allow_sockets, skipped;
} config_t;

typedef struct {
    scenario_run_t *r;
    config_t *c;
    const char *root, *data;
    yyjson_mut_doc *schema;
    jval_t *record;
} suite_t;

typedef struct {
    bytes_t raw;
    size_t count;
    format_zip_info_t *entries;
    bytes_t *parts;
    yyjson_mut_doc *doc;
} archive_t;

static jval_t *get(jval_t *v, const char *k) {
    return yyjson_mut_obj_get(v, k);
}

static bool free_plain(void *p) {
    free(p);
    return true;
}

static bool free_doc(void *p) {
    yyjson_mut_doc_free(p);
    return true;
}

static const char *str(scenario_run_t *r, jval_t *v) {
    const char *s = yyjson_mut_get_str(v);
    REQUIRE(s && strlen(s) == yyjson_mut_get_len(v));
    return s;
}

static bool eq(jval_t *v, const char *s) {
    return yyjson_mut_is_str(v) && yyjson_mut_get_len(v) == strlen(s)
           && !memcmp(yyjson_mut_get_str(v), s, strlen(s));
}

static const char *fmt(scenario_run_t *r, const char *format, ...) {
    va_list a, b;
    va_start(a, format);
    va_copy(b, a);
    int n = vsnprintf(NULL, 0, format, b);
    va_end(b);
    REQUIRE(n >= 0);
    char *s = language_owned(r, ALLOC((size_t)n + 1));
    vsnprintf(s, (size_t)n + 1, format, a);
    va_end(a);
    return s;
}

static bytes_t read_bytes(scenario_run_t *r, const char *p) {
    size_t n;
    const char *d = language_read(r, p, &n);
    return (bytes_t){d, n};
}

static yyjson_mut_doc *parse(scenario_run_t *r, bytes_t b) {
    char error[512];
    yyjson_mut_doc *d = inspect_report_parse(b.data, b.size, error, sizeof(error));
    scenario_require(r, d != NULL, error);
    scenario_defer(r, free_doc, d);
    return d;
}

static bool exists(scenario_run_t *r, const char *p, bool follow) {
    fs_info_t info;
    fs_error_t e = {0};
    bool found = inspect_fs_path(p, follow, &info, &e), missing = e.status == FS_NOT_FOUND;
    clear_fs_error(&e);
    REQUIRE(found || missing);
    return found;
}

static void directory(scenario_run_t *r, const char *p) {
    if (exists(r, p, true))
        return;
    const char *slash = strrchr(p, '/');
    if (slash && slash != p)
        directory(r, fmt(r, "%.*s", (int)(slash - p), p));
    language_directory(r, p);
}

static void write_bytes(scenario_run_t *r, const char *p, bytes_t b) {
    const char *slash = strrchr(p, '/');
    if (slash && slash != p)
        directory(r, fmt(r, "%.*s", (int)(slash - p), p));
    language_write(r, p, b.data, b.size);
}

static const char *expand(suite_t *s, const char *value) {
    scenario_run_t *r = s->r;
    const char *token = "@ROOT@", *at = strstr(value, token);
    if (!at)
        return value;
    size_t before = (size_t)(at - value);
    return fmt(r, "%.*s%s%s", (int)before, value, s->root, expand(s, at + strlen(token)));
}

static bytes_t blob(suite_t *s, const char *hash) {
    scenario_run_t *r = s->r;
    REQUIRE(strlen(hash) == 64);
    bytes_t b = read_bytes(r, language_join(r, s->data, fmt(r, "blobs/%s", hash)));
    char actual[65];
    format_binary_sha256(b.data, b.size, actual);
    REQUIRE(!strcmp(actual, hash));
    return b;
}

static bytes_t hexbytes(scenario_run_t *r, const char *hex) {
    size_t n = strlen(hex);
    REQUIRE(n % 2 == 0);
    unsigned char *out = language_owned(r, ALLOC(n / 2 + 1));
    for (size_t i = 0; i < n; i += 2) {
        unsigned a, b;
        char x = hex[i], y = hex[i + 1];
        a = x >= '0' && x <= '9'   ? (unsigned)(x - '0')
            : x >= 'a' && x <= 'f' ? (unsigned)(x - 'a' + 10)
                                   : 99;
        b = y >= '0' && y <= '9'   ? (unsigned)(y - '0')
            : y >= 'a' && y <= 'f' ? (unsigned)(y - 'a' + 10)
                                   : 99;
        REQUIRE(a < 16 && b < 16);
        out[i / 2] = (unsigned char)(a * 16 + b);
    }
    out[n / 2] = 0;
    return (bytes_t){out, n / 2};
}

static archive_t *archive(suite_t *s, const char *path) {
    scenario_run_t *r = s->r;
    archive_t *a = language_owned(r, CALLOC(sizeof(*a)));
    a->raw = read_bytes(r, path);
    format_zip_info_t info;
    char error[512];
    REQUIRE(format_zip_inspect(a->raw.data, a->raw.size, SIZE_MAX, &info, error, sizeof(error)));
    a->count = info.count;
    REQUIRE(a->count >= 2 && a->count <= 4096);
    a->entries = language_owned(r, CALLOC(a->count * sizeof(*a->entries)));
    a->parts = language_owned(r, CALLOC(a->count * sizeof(*a->parts)));
    for (size_t i = 0; i < a->count; i++) {
        REQUIRE(
            format_zip_inspect(a->raw.data, a->raw.size, i, a->entries + i, error, sizeof(error)));
        size_t n;
        void *p = format_zip_extract(a->raw.data,
                                     a->raw.size,
                                     i,
                                     32 * 1024 * 1024,
                                     &n,
                                     error,
                                     sizeof(error));
        REQUIRE(p);
        scenario_defer(r, free_plain, p);
        a->parts[i] = (bytes_t){p, n};
        if (a->entries[i].name_size == 11 && !memcmp(a->entries[i].name, "module.json", 11))
            a->doc = parse(r, a->parts[i]);
    }
    REQUIRE(a->doc);
    return a;
}

static size_t member(scenario_run_t *r, archive_t *a, const char *name) {
    for (size_t i = 0; i < a->count; i++)
        if (a->entries[i].name_size == strlen(name)
            && !memcmp(a->entries[i].name, name, strlen(name)))
            return i;
    REQUIRE(false);
    return 0;
}

static void archive_contract(suite_t *s, archive_t *a) {
    scenario_run_t *r = s->r;
    jval_t *doc = yyjson_mut_doc_get_root(a->doc), *schema = yyjson_mut_doc_get_root(s->schema);
    size_t metadata = member(r, a, "module.json"), code = member(r, a, "bytecode.bin"), n;
    char *canonical = format_json_canonical(doc, &n);
    REQUIRE(canonical);
    scenario_defer(r, free_plain, canonical);
    scenario_require_bytes(r,
                           "canonical metadata",
                           canonical,
                           n,
                           a->parts[metadata].data,
                           a->parts[metadata].size,
                           false);
    yyjson_mut_doc *copy = yyjson_mut_doc_new(NULL);
    REQUIRE(copy);
    scenario_defer(r, free_doc, copy);
    jval_t *root = yyjson_mut_val_mut_copy(copy, doc);
    REQUIRE(root);
    yyjson_mut_doc_set_root(copy, root);
    char error[512];
    REQUIRE(format_metadata_normalize(copy, root, "module", schema, error, sizeof(error)));
    REQUIRE(format_json_equal(root, doc));
    char hash[65];
    REQUIRE(format_binary_main_hash(doc,
                                    schema,
                                    a->parts[code].data,
                                    a->parts[code].size,
                                    hash,
                                    error,
                                    sizeof(error)));
    REQUIRE(eq(get(doc, "mainHash"), hash));
    size_t natives = yyjson_mut_arr_size(get(doc, "native"));
    REQUIRE(a->count == natives + 2 && metadata == 0 && code == 1);
    const char **names = language_owned(r, CALLOC((natives + 1) * sizeof(*names)));
    for (size_t i = 0; i < natives; i++) {
        jval_t *v = yyjson_mut_arr_get(get(doc, "native"), i);
        names[i] = str(r, get(v, "library"));
        size_t at = member(r, a, names[i]);
        format_binary_sha256(a->parts[at].data, a->parts[at].size, hash);
        REQUIRE(eq(get(v, "hash"), hash));
    }
    for (size_t i = 0; i < natives; i++)
        for (size_t j = i + 1; j < natives; j++)
            if (strcmp(names[i], names[j]) > 0) {
                const char *t = names[i];
                names[i] = names[j];
                names[j] = t;
            }
    for (size_t i = 0; i < a->count; i++) {
        format_zip_info_t *e = a->entries + i;
        REQUIRE(e->method == 8 && e->date == 33 && e->time == 0);
        if (i >= 2)
            REQUIRE(e->name_size == strlen(names[i - 2])
                    && !memcmp(e->name, names[i - 2], e->name_size));
    }
}

static const char *json_string(scenario_run_t *r, const char *data, size_t size, size_t *consumed) {
    yyjson_read_err error;
    yyjson_doc *d = yyjson_read_opts((char *)data, size, YYJSON_READ_STOP_WHEN_DONE, NULL, &error);
    REQUIRE(d);
    yyjson_val *v = yyjson_doc_get_root(d);
    REQUIRE(yyjson_is_str(v));
    size_t n = yyjson_get_len(v);
    char *out = language_owned(r, ALLOC(n + 1));
    memcpy(out, yyjson_get_str(v), n);
    out[n] = 0;
    *consumed = yyjson_doc_get_read_size(d);
    yyjson_doc_free(d);
    REQUIRE(strlen(out) == n);
    return out;
}

static void
success_line(suite_t *s, scenario_command_result_t result, const char *path, archive_t *a) {
    scenario_run_t *r = s->r;
    const char *out = result.out;
    size_t n = result.out_size;
    if (n && out[n - 1] == '\n')
        n--;
    if (n && out[n - 1] == '\r')
        n--;
    REQUIRE(n && !memchr(out, '\n', n) && !memchr(out, '\r', n));
    size_t used = 0, offset = 0;
    const char *value = json_string(r, out, n, &used);
    REQUIRE(!strcmp(value, path));
    offset += used;
    REQUIRE(offset + 2 < n && !memcmp(out + offset, ": ", 2));
    offset += 2;
    value = json_string(r, out + offset, n - offset, &used);
    jval_t *doc = yyjson_mut_doc_get_root(a->doc);
    REQUIRE(eq(get(doc, "name"), value));
    offset += used;
    REQUIRE(offset < n && out[offset] == '@');
    offset++;
    value = json_string(r, out + offset, n - offset, &used);
    REQUIRE(eq(get(doc, "version"), value));
    offset += used;
    const char *suffix = fmt(r, "; %zu native variants", yyjson_mut_arr_size(get(doc, "native")));
    REQUIRE(n >= strlen(suffix) && !memcmp(out + n - strlen(suffix), suffix, strlen(suffix)));
}

static bool make_link(scenario_run_t *r,
                      const char *target,
                      const char *path,
                      bool symbolic,
                      bool directory_link) {
#ifdef _WIN32
    int nt = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, target, -1, NULL, 0),
        np = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    REQUIRE(nt > 0 && np > 0);
    wchar_t *t = language_owned(r, ALLOC((size_t)nt * sizeof(*t))),
            *p = language_owned(r, ALLOC((size_t)np * sizeof(*p)));
    REQUIRE(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, target, -1, t, nt) == nt
            && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, p, np) == np);
    DWORD flags = directory_link ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0;
    bool ok =
        symbolic ? CreateSymbolicLinkW(p, t, flags | 2) != 0 : CreateHardLinkW(p, t, NULL) != 0;
    if (!ok && symbolic && GetLastError() == ERROR_INVALID_PARAMETER)
        ok = CreateSymbolicLinkW(p, t, flags) != 0;
    if (!ok && symbolic && GetLastError() == ERROR_PRIVILEGE_NOT_HELD)
        return false;
#else
    (void)directory_link;
    bool ok = (symbolic ? symlink(target, path) : link(target, path)) == 0;
#endif
    REQUIRE(ok);
    return true;
}
#ifndef _WIN32
typedef struct {
    int fd;
} socket_t;

static bool close_socket(void *p) {
    socket_t *s = p;
    bool ok = close(s->fd) == 0;
    free(s);
    return ok;
}
#endif
static bool install_tree(suite_t *s, jval_t *files) {
    scenario_run_t *r = s->r;
    size_t i, n;
    jval_t *k, *v;
    for (unsigned pass = 0; pass < 3; pass++) {
        yyjson_mut_obj_foreach(files, i, n, k, v) {
            const char *relative = str(r, k), *path = language_join(r, s->root, relative);
            if (get(v, "constructor_input") || get(v, "constructor_output")
                || get(v, "constructor_library"))
                continue;
            bool is_dir = get(v, "directory") != NULL,
                 is_link = get(v, "symlink") != NULL || get(v, "hardlink") != NULL;
            if ((pass == 0 && !is_dir) || (pass == 1 && (is_dir || is_link))
                || (pass == 2 && !is_link))
                continue;
            if (is_dir) {
                directory(r, path);
                continue;
            }
            const char *slash = strrchr(path, '/');
            if (slash)
                directory(r, fmt(r, "%.*s", (int)(slash - path), path));
            if (get(v, "blob")) {
                write_bytes(r, path, blob(s, str(r, get(v, "blob"))));
#ifndef _WIN32
                if (get(v, "mode"))
                    REQUIRE(chmod(path, (mode_t)yyjson_mut_get_uint(get(v, "mode"))) == 0);
#endif
            } else if (is_link) {
                bool symbolic = get(v, "symlink") != NULL;
                const char *target = str(r, get(v, symbolic ? "symlink" : "hardlink"));
                target = symbolic ? expand(s, target) : language_join(r, s->root, target);
                if (!make_link(r,
                               target,
                               path,
                               symbolic,
                               yyjson_mut_get_bool(get(v, "target_directory")))) {
                    puts("SKIP: Windows symlink privilege error 1314");
                    s->c->skipped = true;
                    return false;
                }
            } else if (get(v, "fifo")) {
#ifndef _WIN32
                REQUIRE(mkfifo(path, 0600) == 0);
#else
                REQUIRE(false);
#endif
            } else if (get(v, "socket")) {
#ifndef _WIN32
                int fd = socket(AF_UNIX, SOCK_STREAM, 0);
                if (fd < 0 && errno == EPERM && s->c->allow_sockets) {
                    puts("SKIP: explicit local restricted Unix sockets");
                    s->c->skipped = true;
                    return false;
                }
                REQUIRE(fd >= 0);
                socket_t *sock = calloc(1, sizeof(*sock));
                REQUIRE(sock);
                sock->fd = fd;
                scenario_defer(r, close_socket, sock);
                struct sockaddr_un address = {0};
                address.sun_family = AF_UNIX;
                REQUIRE(strlen(path) < sizeof(address.sun_path));
                strcpy(address.sun_path, path);
                REQUIRE(bind(fd, (struct sockaddr *)&address, sizeof(address)) == 0);
#else
                REQUIRE(false);
#endif
            } else
                REQUIRE(false);
        }
    }
    return true;
}

static void verify_entry(suite_t *s, const char *relative, jval_t *v, bool modes) {
    scenario_run_t *r = s->r;
    if (get(v, "constructor_input") || get(v, "constructor_output")
        || get(v, "constructor_library"))
        return;
    const char *path = language_join(r, s->root, relative);
    fs_info_t info;
    fs_error_t e = {0};
    bool ok = inspect_fs_path(path, false, &info, &e);
    if (!ok)
        fprintf(stderr, "missing expected fixture entry %s (native %lu)\n", path, e.native_code);
    clear_fs_error(&e);
    REQUIRE(ok);
    if (get(v, "directory"))
        REQUIRE(info.kind == FS_DIRECTORY);
    else if (get(v, "symlink"))
        REQUIRE(info.kind == FS_LINK);
    else if (get(v, "blob")) {
        REQUIRE(info.kind == FS_FILE);
        bytes_t expected = blob(s, str(r, get(v, "blob"))), actual = read_bytes(r, path);
        scenario_require_bytes(r,
                               relative,
                               expected.data,
                               expected.size,
                               actual.data,
                               actual.size,
                               false);
    } else if (get(v, "hardlink")) {
        REQUIRE(info.kind == FS_FILE);
        bytes_t expected = read_bytes(r, language_join(r, s->root, str(r, get(v, "hardlink")))),
                actual = read_bytes(r, path);
        scenario_require_bytes(r,
                               relative,
                               expected.data,
                               expected.size,
                               actual.data,
                               actual.size,
                               false);
    } else {
#ifndef _WIN32
        struct stat st;
        REQUIRE(lstat(path, &st) == 0);
        if (get(v, "fifo"))
            REQUIRE(S_ISFIFO(st.st_mode));
        else if (get(v, "socket"))
            REQUIRE(S_ISSOCK(st.st_mode));
        else
            REQUIRE(false);
#else
        REQUIRE(false);
#endif
    }
#ifndef _WIN32
    if (modes && get(v, "mode")) {
        struct stat st;
        REQUIRE(stat(path, &st) == 0
                && ((unsigned)st.st_mode & 07777) == yyjson_mut_get_uint(get(v, "mode")));
    }
#else
    (void)modes;
#endif
}

static void tree_names(suite_t *s, jval_t *expected, const char *relative, size_t *count) {
    scenario_run_t *r = s->r;
    const char *path = *relative ? language_join(r, s->root, relative) : s->root;
    fs_entries_t entries = {0};
    fs_error_t e = {0};
    bool ok = list_fs_directory(path, &entries, &e);
    clear_fs_error(&e);
    REQUIRE(ok); /* Copy names before assertions to keep the listing cleanup bounded. */
    const char **names = language_owned(r, CALLOC((entries.count + 1) * sizeof(*names)));
    for (size_t i = 0; i < entries.count; i++)
        names[i] = fmt(r, "%s", entries.names[i]);
    size_t n = entries.count;
    clear_fs_entries(&entries);
    for (size_t i = 0; i < n; i++) {
        const char *rel = *relative ? fmt(r, "%s/%s", relative, names[i]) : names[i];
        jval_t *v = get(expected, rel);
#ifdef _WIN32
        if (!v && !strcmp(rel, "constructor.dll"))
            v = get(expected, "constructor.so");
#endif
        if (!v)
            fprintf(stderr, "unexpected sibling/file left after merge: %s\n", rel);
        REQUIRE(v);
        (*count)++;
        if (get(v, "directory"))
            tree_names(s, expected, rel, count);
    }
}

static void verify_tree(suite_t *s, jval_t *expected, bool modes) {
    scenario_run_t *r = s->r;
    size_t count = 0;
    tree_names(s, expected, "", &count);
    REQUIRE(count == yyjson_mut_obj_size(expected));
    size_t i, n;
    jval_t *k, *v;
    yyjson_mut_obj_foreach(expected, i, n, k, v) verify_entry(s, str(r, k), v, modes);
}

static const char *const *environment(suite_t *s, jval_t *overrides) {
    scenario_run_t *r = s->r;
    fs_error_t path_error = {0};
    char *path_value = fs_environment("PATH", &path_error);
    bool missing = path_error.status == FS_NOT_FOUND;
    clear_fs_error(&path_error);
    REQUIRE(path_value || missing);
    if (path_value)
        language_owned(r, path_value);
    const char *const *base =
        language_environment_override(r, "PATH", path_value ? path_value : "");
    size_t n = 0;
    while (base[n])
        n++;
    size_t m = yyjson_mut_obj_size(overrides);
    const char **out = language_owned(r, CALLOC((n + m + 1) * sizeof(*out)));
    size_t used = 0;
    for (size_t i = 0; i < n; i++) {
        bool replace = false;
        size_t j, k;
        jval_t *key, *value;
        yyjson_mut_obj_foreach(overrides, j, k, key, value) {
            (void)value;
            const char *name = str(r, key);
            size_t length = strlen(name);
#ifdef _WIN32
            replace |= !_strnicmp(base[i], name, length) && base[i][length] == '=';
#else
            replace |= !strncmp(base[i], name, length) && base[i][length] == '=';
#endif
        }
        if (!replace)
            out[used++] = base[i];
    }
    size_t j, k;
    jval_t *key, *value;
    yyjson_mut_obj_foreach(overrides, j, k, key, value) out[used++] =
        fmt(r, "%s=%s", str(r, key), expand(s, str(r, value)));
    return out;
}

static bool contains(bytes_t b, const char *text) {
    size_t n = strlen(text);
    if (n > b.size)
        return false;
    for (size_t i = 0; i <= b.size - n; i++)
        if (!memcmp((const char *)b.data + i, text, n))
            return true;
    return false;
}

static const char *quoted(scenario_run_t *r, const char *text) {
    yyjson_mut_doc *d = yyjson_mut_doc_new(NULL);
    REQUIRE(d);
    scenario_defer(r, free_doc, d);
    jval_t *v = yyjson_mut_strcpy(d, text);
    REQUIRE(v);
    size_t n;
    char *out = format_json_canonical(v, &n);
    REQUIRE(out);
    scenario_defer(r, free_plain, out);
    return out;
}

static void failure_diagnostics(suite_t *s,
                                scenario_command_result_t result,
                                const char *const *args,
                                size_t argc) {
    scenario_run_t *r = s->r;
    bytes_t err = {result.err, result.err_size};
    REQUIRE(result.err_size > 0 && result.out_size == 0);
    REQUIRE(!contains(err, "Choose author order"));
    unsigned line = (unsigned)yyjson_mut_get_uint(get(s->record, "source_line"));
    if (line == 201) {
        REQUIRE(argc >= 3);
        REQUIRE(contains(err, quoted(r, args[argc - 2]))
                && !contains(err, quoted(r, args[argc - 1])));
    }
    if (line == 313)
        REQUIRE(argc > 0 && contains(err, quoted(r, args[0])));
    if (line == 550) {
        REQUIRE(contains(err, "container validation failed"));
        REQUIRE(contains(err, "field \"/externals/alias/functionRef/functionId\""));
    }
    const char *keys[] = {"diagnostic_contains", "diagnostic_absent"};
    for (size_t kind = 0; kind < 2; kind++) {
        jval_t *values = get(s->record, keys[kind]);
        size_t i, n;
        jval_t *v;
        yyjson_mut_arr_foreach(values, i, n, v) {
            const char *raw = str(r, v);
            const char *text = expand(s, raw);
            if (raw[0] == '"' && strstr(raw, "@ROOT@")) {
                yyjson_mut_doc *d = parse(r, (bytes_t){raw, strlen(raw)});
                text = quoted(r, expand(s, str(r, yyjson_mut_doc_get_root(d))));
            }
            REQUIRE(contains(err, text) == (kind == 0));
        }
    }
}

static jval_t *expanded_json(suite_t *s, yyjson_mut_doc *d, jval_t *v) {
    scenario_run_t *r = s->r;
    if (yyjson_mut_is_str(v)) {
        const char *value = yyjson_mut_get_str(v);
        if (strlen(value) != yyjson_mut_get_len(v))
            return yyjson_mut_val_mut_copy(d, v);
        return yyjson_mut_strcpy(d, expand(s, value));
    }
    if (yyjson_mut_is_arr(v)) {
        jval_t *out = yyjson_mut_arr(d);
        REQUIRE(out);
        size_t i, n;
        jval_t *x;
        yyjson_mut_arr_foreach(v, i, n, x)
            REQUIRE(yyjson_mut_arr_append(out, expanded_json(s, d, x)));
        return out;
    }
    if (yyjson_mut_is_obj(v)) {
        jval_t *out = yyjson_mut_obj(d);
        REQUIRE(out);
        size_t i, n;
        jval_t *k, *x;
        yyjson_mut_obj_foreach(v, i, n, k, x)
            REQUIRE(yyjson_mut_obj_add(out, yyjson_mut_val_mut_copy(d, k), expanded_json(s, d, x)));
        return out;
    }
    return yyjson_mut_val_mut_copy(d, v);
}

static void inspect_contract(suite_t *s, scenario_command_result_t result, int expected) {
    scenario_run_t *r = s->r;
    REQUIRE(!result.err_size);
    yyjson_mut_doc *d = parse(r, (bytes_t){result.out, result.out_size});
    jval_t *report = yyjson_mut_doc_get_root(d);
    yyjson_mut_doc *schema =
        parse(r, read_bytes(r, language_join(r, s->c->format, "inspect-report.schema.json")));
    char error[512];
    REQUIRE(inspect_report_schema(report,
                                  yyjson_mut_doc_get_root(schema),
                                  yyjson_mut_doc_get_root(schema),
                                  s->c->format,
                                  error,
                                  sizeof(error)));
    REQUIRE(yyjson_mut_get_bool(get(report, "success")) == (expected == 0)
            && yyjson_mut_get_sint(get(report, "exitCode")) == expected);
    jval_t *code = get(s->record, "code");
    if (yyjson_mut_is_str(code)) {
        bool found = false;
        size_t i, n;
        jval_t *v;
        yyjson_mut_arr_foreach(get(report, "diagnostics"), i, n, v) found |=
            eq(get(v, "severity"), "error") && format_json_equal(get(v, "code"), code);
        REQUIRE(found);
    }
    if (expected == 0) {
        jval_t *modules = get(report, "modules");
        REQUIRE(yyjson_mut_arr_size(modules) == 3);
        size_t i, n;
        jval_t *m;
        yyjson_mut_arr_foreach(modules, i, n, m)
            REQUIRE(eq(get(m, "validation"), "passed") && yyjson_mut_is_arr(get(m, "native"))
                    && !yyjson_mut_arr_size(get(m, "native")));
        bool warning = false;
        yyjson_mut_arr_foreach(get(report, "diagnostics"), i, n, m) warning |=
            eq(get(m, "severity"), "warning");
        REQUIRE(warning);
    }
    jval_t *saved = get(s->record, "report");
    if (saved) {
        jval_t *reference = expanded_json(
            s,
            d,
            saved); /* Current-runtime target differs across platforms; graph semantics do not. */
        yyjson_mut_obj_remove_str(reference, "target");
        yyjson_mut_obj_remove_str(report, "target");
        REQUIRE(format_json_equal(report, reference));
    }
}
#ifndef _WIN32
typedef struct {
    mode_t previous;
} umask_t;

static bool restore_umask(void *p) {
    umask_t *u = p;
    umask(u->previous);
    free(u);
    return true;
}
#endif
typedef struct {
    suite_t *s;
    const char *destination;
    bool existed;
    bytes_t before;
} prompt_t;

static void pending_action(scenario_run_t *r, void *arg) {
    prompt_t *p = arg;
    suite_t *s = p->s;
    bool present = exists(r, p->destination, true);
    REQUIRE(present == p->existed);
    if (present) {
        bytes_t now = read_bytes(r, p->destination);
        scenario_require_bytes(r,
                               "pending destination",
                               p->before.data,
                               p->before.size,
                               now.data,
                               now.size,
                               false);
    }
    const char *action = yyjson_mut_get_str(get(s->record, "during"));
    if (!action || !*action)
        return;
    if (!strcmp(action, "corrupt_after_read")) {
        write_bytes(r,
                    language_join(r, s->root, "authors-a"),
                    (bytes_t){"path changed after source snapshot", 34});
    } else if (!strcmp(action, "swap_destination")) {
        language_remove(r, p->destination);
        REQUIRE(
            make_link(r, language_join(r, s->root, "swap-target"), p->destination, true, false));
    } else
        REQUIRE(false);
}

static void body(scenario_run_t *r, void *argument) {
    config_t *c = argument;
    suite_t s = {.r = r, .c = c, .record = c->record};
#ifdef _WIN32
    if (yyjson_mut_get_bool(get(c->record, "posix_only"))) {
        puts("SKIP: POSIX filesystem/terminal scenario");
        c->skipped = true;
        return;
    }
#endif
    s.root = language_join(r, scenario_work_directory(scenario_run_context(r)), "root");
    directory(r, s.root);
    s.data = language_join(r, scenario_data_directory(scenario_run_context(r)), "module-merge");
    s.schema = parse(r, read_bytes(r, language_join(r, c->format, "schema-source.json")));
    if (!install_tree(&s, get(s.record, "files")))
        return;
    merge_constructor_t *constructor = NULL;
    if (yyjson_mut_get_bool(get(s.record, "constructor")))
        constructor = merge_constructor_prepare(r, c->cc, s.data, s.root, c->format, s.record);
#ifndef _WIN32
    umask_t *u = calloc(1, sizeof(*u));
    REQUIRE(u);
    u->previous = umask((mode_t)yyjson_mut_get_uint(get(s.record, "umask")));
    scenario_defer(r, restore_umask, u);
#endif
    jval_t *input_args = get(s.record, "args");
    size_t argc = yyjson_mut_arr_size(input_args);
    const char *kind = str(r, get(s.record, "kind"));
    bool inspecting = !strcmp(kind, "inspect"), terminal = !strcmp(kind, "interactive");
    const char *exe = inspecting ? c->inspect : c->merge;
    const char **args = language_owned(r, CALLOC((argc + 2) * sizeof(*args)));
    args[0] = exe;
    for (size_t i = 0; i < argc; i++)
        args[i + 1] = expand(&s, str(r, yyjson_mut_arr_get(input_args, i)));
    const char *cwd =
        yyjson_mut_is_str(get(s.record, "cwd")) ? expand(&s, str(r, get(s.record, "cwd"))) : s.root;
    int expected = (int)yyjson_mut_get_sint(get(s.record, "status"));
    scenario_command_result_t result;
    if (terminal) {
        REQUIRE(argc >= 1);
        prompt_t *p = language_owned(r, CALLOC(sizeof(*p)));
        p->s = &s;
        p->destination = args[1];
        p->existed = exists(r, p->destination, true);
        if (p->existed)
            p->before = read_bytes(r, p->destination);
        const char *hex = yyjson_mut_get_str(get(s.record, "responsehex"));
        if (!hex)
            hex = str(r, get(get(s.record, "response"), "hex"));
        bytes_t response = hexbytes(r, hex);
        result = merge_terminal(r, args, pending_action, p, response.data, response.size);
        REQUIRE(result.exit_code == expected);
        if (expected) {
            REQUIRE(!result.out_size);
            REQUIRE(exists(r, p->destination, true) == p->existed);
            if (p->existed) {
                bytes_t now = read_bytes(r, p->destination);
                scenario_require_bytes(r,
                                       "terminal destination failure preservation",
                                       p->before.data,
                                       p->before.size,
                                       now.data,
                                       now.size,
                                       false);
            }
        } else {
            const char *out = result.out;
            size_t n = result.out_size;
            if (n && out[n - 1] == '\n')
                n--;
            if (n && out[n - 1] == '\r')
                n--;
            REQUIRE(n && !memchr(out, '\n', n) && !memchr(out, '\r', n));
        }
        if (get(s.record, "prompt_count")) {
            const char *needle = "Choose author order";
            size_t n = strlen(needle), count = 0;
            for (size_t i = 0; i + n <= result.err_size; i++)
                if (!memcmp((const char *)result.err + i, needle, n))
                    count++;
            REQUIRE(count == yyjson_mut_get_uint(get(s.record, "prompt_count")));
        }
    } else {
        const char *hex = yyjson_mut_get_str(get(s.record, "inputhex"));
        bytes_t input = hexbytes(r, hex ? hex : "");
        child_options_t options = {.executable = exe,
                                   .arguments = args,
                                   .working_directory = cwd,
                                   .environment = environment(&s, get(s.record, "env")),
                                   .stdin_data = input.data,
                                   .stdin_size = input.size,
                                   .empty_stdin = true};
        result = scenario_command_capture(r, &options);
        REQUIRE(result.exit_code == expected);
        if (inspecting)
            inspect_contract(&s, result, expected);
        else if (expected)
            failure_diagnostics(&s, result, args + 1, argc);
        else if (yyjson_mut_get_bool(get(s.record, "success"))) {
            const char *output = expand(&s, str(r, get(s.record, "output")));
            archive_t *a = archive(&s, output);
            archive_contract(&s, a);
            success_line(&s, result, output, a);
            if (get(s.record, "expected_metadata"))
                REQUIRE(format_json_equal(yyjson_mut_doc_get_root(a->doc),
                                          get(s.record, "expected_metadata")));
            if (get(s.record, "expected_output")) {
                bytes_t expected_bytes = blob(&s, str(r, get(s.record, "expected_output")));
                scenario_require_bytes(r,
                                       "independent original expected output",
                                       expected_bytes.data,
                                       expected_bytes.size,
                                       a->raw.data,
                                       a->raw.size,
                                       false);
            }
        } else if (argc == 1 && (!strcmp(args[1], "--help") || !strcmp(args[1], "--version")))
            REQUIRE(result.out_size > 0);
    }
    if (eq(get(s.record, "id"), "paths.dash-input"))
        REQUIRE(result.out_size < 2 || memcmp(result.out, "PK", 2));
    if (constructor)
        merge_constructor_verify(constructor);
    verify_tree(&s, get(s.record, "after"), yyjson_mut_get_bool(get(s.record, "assert_modes")));
}

int main(int argc, char **argv) {
#ifdef _WIN32
    fs_error_t argument_error = {0};
    if (!fs_arguments(&argc, &argv, &argument_error)) {
        clear_fs_error(&argument_error);
        return 1;
    }
#endif
    int result = 1;
    bool keep = false, sockets = false, valid = true;
    for (int i = 9; i < argc; i++) {
        if (!strcmp(argv[i], "--keep-work"))
            keep = true;
        else if (!strcmp(argv[i], "--allow-restricted-sockets"))
            sockets = true;
        else
            valid = false;
    }
    if (argc >= 9 && valid) {
        char *paths[9] = {0};
        for (int i = 2; i < 9; i++) {
            fs_error_t e = {0};
            if (i == 4 && !strchr(argv[i], '/') && !strchr(argv[i], '\\')) {
                paths[i] = ALLOC(strlen(argv[i]) + 1);
                strcpy(paths[i], argv[i]);
            } else
                paths[i] = absolute_fs_path(argv[i], &e);
            clear_fs_error(&e);
            valid &= paths[i] != NULL;
        }
        fs_error_t e = {0};
        char *file = paths[6] ? join_fs_path(paths[6], "module-merge/cases.json", &e) : NULL;
        clear_fs_error(&e);
        void *data = NULL;
        size_t size = 0;
        bool loaded = file && read_fs_regular_file(file, 64 * 1024 * 1024, &data, &size, &e);
        clear_fs_error(&e);
        char error[512];
        yyjson_mut_doc *doc =
            loaded ? inspect_report_parse(data, size, error, sizeof(error)) : NULL;
        jval_t *cases = get(yyjson_mut_doc_get_root(doc), "cases");
        size_t count = yyjson_mut_arr_size(cases);
        if (valid && count) {
            config_t *configs = CALLOC(count * sizeof(*configs));
            scenario_definition_t *definitions = CALLOC(count * sizeof(*definitions));
            char(*ids)[160] = CALLOC(count * sizeof(*ids));
            for (size_t i = 0; i < count; i++) {
                jval_t *record = yyjson_mut_arr_get(cases, i);
                const char *id = yyjson_mut_get_str(get(record, "id"));
                if (!id) {
                    valid = false;
                    break;
                }
                snprintf(ids[i], sizeof(ids[i]), "integration.module_merge.%s", id);
                configs[i] = (config_t){paths[2],
                                        paths[3],
                                        paths[4],
                                        paths[7],
                                        paths[8],
                                        record,
                                        keep,
                                        sockets,
                                        false};
                uint64_t timeout = eq(get(record, "kind"), "interactive")            ? 30000
                                   : yyjson_mut_get_bool(get(record, "constructor")) ? 90000
                                                                                     : 15000;
                definitions[i] = (scenario_definition_t){ids[i], timeout, body, configs + i, NULL};
            }
            scenario_options_t options = {.build_directory = paths[5], .data_directory = paths[6]};
            const char *selection[] = {argv[1], keep ? "--keep-work" : NULL};
            if (valid)
                result =
                    scenario_registry_main(definitions, count, keep ? 2 : 1, selection, &options);
            if (!result)
                for (size_t i = 0; i < count; i++)
                    if (configs[i].skipped)
                        result = 77;
            FREE(ids);
            FREE(definitions);
            FREE(configs);
        } else
            fputs("Cannot load merge corpus or executable paths\n", stderr);
        yyjson_mut_doc_free(doc);
        FREE(data);
        FREE(file);
        for (int i = 2; i < 9; i++)
            FREE(paths[i]);
    } else
        fputs("usage: module_merge_scenarios ID MERGE INSPECT CC BUILD DATA SOURCE_ROOT "
              "FORMAT_ROOT [--keep-work] [--allow-restricted-sockets]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
