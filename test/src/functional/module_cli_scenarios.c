/** @file module_cli_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Real public CLI, publication, launch and compiler selection contracts (#371).
 */
#ifndef _WIN32
#    define _POSIX_C_SOURCE 200809L
#endif
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/format_zip.h"
#include "test/support/language_execution.h"
#include "test/support/process_watch.h"
#include "test/support/registry.h"
#include "yyjson.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <windows.h>
#    define SUFFIX ".exe"
#    define PATH_SEPARATOR ';'
#else
#    include <sys/stat.h>
#    include <time.h>
#    include <unistd.h>
#    ifdef __linux__
#        include <sys/xattr.h>
#    endif
#    define SUFFIX ""
#    define PATH_SEPARATOR ':'
#endif
#define REQUIRE(x) scenario_require(r, (x), #x)
#define ARGS(...) ((const char *const[]){__VA_ARGS__, NULL})

typedef struct {
    const void *data;
    size_t size;
} bytes_t;

typedef struct {
    const char *goat, *wrapper, *cc, *kind;
    bool allow_acl, limited, skipped;
} config_t;

typedef struct {
    scenario_run_t *r;
    config_t *c;
    const char *root, *cwd, *data;
} suite_t;

typedef struct {
    const char *dir, *descriptor, *binary, *name;
} project_t;

typedef struct {
    bytes_t zip;
    yyjson_val *metadata;
} archive_t;

static bool free_plain(void *p) {
    free(p);
    return true;
}

static bool free_doc(void *p) {
    yyjson_doc_free(p);
    return true;
}

static bool free_entries(void *p) {
    clear_fs_entries(p);
    free(p);
    return true;
}

static const char *string(scenario_run_t *r, const char *format, ...) {
    va_list args, copy;
    va_start(args, format);
    va_copy(copy, args);
    int n = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    REQUIRE(n >= 0);
    char *s = language_owned(r, ALLOC((size_t)n + 1));
    vsnprintf(s, (size_t)n + 1, format, args);
    va_end(args);
    return s;
}

static bool contains(bytes_t b, const char *text) {
    size_t n = strlen(text);
    for (size_t i = 0; n <= b.size && i <= b.size - n; i++)
        if (!memcmp((const char *)b.data + i, text, n))
            return true;
    return false;
}

static const char *lower(scenario_run_t *r, bytes_t b) {
    char *s = language_owned(r, ALLOC(b.size + 1));
    for (size_t i = 0; i < b.size; i++) {
        unsigned char c = ((const unsigned char *)b.data)[i];
        s[i] = c == '\\' ? '/' : (char)tolower(c);
    }
    s[b.size] = 0;
    return s;
}

static bytes_t read_bytes(scenario_run_t *r, const char *path) {
    size_t n;
    const char *p = language_read(r, path, &n);
    return (bytes_t){p, n};
}

static void equal(scenario_run_t *r, const char *label, bytes_t a, bytes_t b) {
    scenario_require_bytes(r, label, a.data, a.size, b.data, b.size, false);
}

static bool exists(scenario_run_t *r, const char *path) {
    fs_info_t info;
    fs_error_t e = {0};
    bool ok = inspect_fs_path(path, false, &info, &e), missing = e.status == FS_NOT_FOUND;
    clear_fs_error(&e);
    REQUIRE(ok || missing);
    return ok;
}

static void directory(scenario_run_t *r, const char *path) {
    if (exists(r, path))
        return;
    const char *slash = strrchr(path, '/');
    if (slash) {
        const char *parent = string(r, "%.*s", (int)(slash - path), path);
        if (*parent)
            directory(r, parent);
    }
    language_directory(r, path);
}

static const char *put(scenario_run_t *r, const char *path, bytes_t b) {
    const char *slash = strrchr(path, '/');
    if (slash)
        directory(r, string(r, "%.*s", (int)(slash - path), path));
    language_write(r, path, b.data, b.size);
    return path;
}

static const char *text_file(scenario_run_t *r, const char *path, const char *s) {
    return put(r, path, (bytes_t){s, strlen(s)});
}

static const char *source(suite_t *s, const char *name) {
    scenario_run_t *r = s->r;
    size_t n;
    return language_read(r, language_join(r, s->data, string(r, "%s.goat", name)), &n);
}

static const char *path(suite_t *s, const char *name) {
    return language_join(s->r, s->root, name);
}

static fs_entries_t *entries(scenario_run_t *r, const char *directory_name) {
    fs_entries_t *e = calloc(1, sizeof(*e));
    REQUIRE(e);
    scenario_defer(r, free_entries, e);
    fs_error_t error = {0};
    bool ok = list_fs_directory(directory_name, e, &error);
    clear_fs_error(&error);
    REQUIRE(ok);
    return e;
}

static const char *canonical(scenario_run_t *r, const char *p) {
    fs_error_t e = {0};
    char *q = canonical_fs_path(p, &e);
    clear_fs_error(&e);
    return language_owned(r, q);
}

static void copy_file(scenario_run_t *r, const char *from, const char *to, bool executable) {
    put(r, to, read_bytes(r, from));
#ifndef _WIN32
    if (executable)
        REQUIRE(chmod(to, 0755) == 0);
#else
    (void)executable;
#endif
}

static const char *const *environment(scenario_run_t *r, const char *const *overrides) {
    const char *const *base = language_environment_override(r, "GOAT_MODULE_PATH", "");
    size_t count = 0, extra = 0;
    while (base[count])
        count++;
    while (overrides && overrides[extra])
        extra++;
    const char **out = language_owned(r, CALLOC((count + extra + 1) * sizeof(*out)));
    size_t n = 0;
    for (size_t i = 0; i < count; i++) {
        bool replace = false;
        for (size_t j = 0; j < extra; j++) {
            const char *eq = strchr(overrides[j], '=');
            REQUIRE(eq);
            size_t len = (size_t)(eq - overrides[j]);
#ifdef _WIN32
            replace |= !_strnicmp(base[i], overrides[j], len) && base[i][len] == '=';
#else
            replace |= !strncmp(base[i], overrides[j], len) && base[i][len] == '=';
#endif
        }
        if (!replace)
            out[n++] = base[i];
    }
    for (size_t j = 0; j < extra; j++)
        out[n++] = overrides[j];
    return out;
}

static scenario_command_result_t invoke(suite_t *s,
                                        const char *const *tail,
                                        const char *expected,
                                        bool fail,
                                        const char *const *env,
                                        const char *cwd) {
    scenario_run_t *r = s->r;
    size_t n = 0;
    while (tail[n])
        n++;
    const char **args = language_owned(r, CALLOC((n + 2) * sizeof(*args)));
    args[0] = s->c->goat;
    for (size_t i = 0; i < n; i++)
        args[i + 1] = tail[i];
    child_options_t options = {.executable = s->c->goat,
                               .arguments = args,
                               .working_directory = cwd ? cwd : s->cwd,
                               .environment = environment(r, env),
                               .empty_stdin = true};
    scenario_command_result_t result = scenario_command_capture(r, &options);
#ifdef __linux__
    const char *prefix = "Module publication failed (8): ";
    size_t plen = strlen(prefix);
    const char *publication = NULL;
    size_t length = 0;
    for (size_t i = 0; i + plen <= result.err_size; i++) {
        if ((i == 0 || ((const char *)result.err)[i - 1] == '\n')
            && !memcmp((const char *)result.err + i, prefix, plen)) {
            publication = (const char *)result.err + i + plen;
            while (i + plen + length < result.err_size && publication[length] != '\n')
                length++;
            if (length && publication[length - 1] == '\r')
                length--;
            break;
        }
    }
    if (s->c->allow_acl && !fail && result.exit_code && publication) {
        const char *begin = publication;
        const char *destination = string(r, "%.*s", (int)length, begin);
        REQUIRE(language_exists(r, destination));
        errno = 0;
        ssize_t got = getxattr(destination, "system.posix_acl_access", NULL, 0);
        REQUIRE(got < 0 && (errno == ENOTSUP || errno == EOPNOTSUPP));
        language_remove(r, destination);
        s->c->limited = true;
        puts("Local ACL limitation: replacement checked only as fresh publication (CI remains "
             "strict)");
        result = scenario_command_capture(r, &options);
    }
#endif
    REQUIRE(fail ? result.exit_code != 0 : result.exit_code == 0);
    if (expected)
        scenario_require_bytes(r,
                               "CLI stdout",
                               expected,
                               strlen(expected),
                               result.out,
                               result.out_size,
                               true);
    if (fail)
        REQUIRE(result.out_size || result.err_size);
    return result;
}

/* Match the original json.dumps separators without changing string contents. */
static const char *descriptor_json(scenario_run_t *r, const char *compact) {
    size_t n = strlen(compact), used = 0;
    char *out = language_owned(r, ALLOC(n * 2 + 1));
    bool quoted = false, escaped = false;
    for (size_t i = 0; i < n; i++) {
        char c = compact[i];
        out[used++] = c;
        if (quoted) {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                quoted = false;
        } else if (c == '"')
            quoted = true;
        else if (c == ',' || c == ':')
            out[used++] = ' ';
    }
    out[used] = 0;
    return out;
}

static project_t project(suite_t *s,
                         const char *directory_name,
                         const char *name,
                         const char *source_name,
                         const char *fields) {
    scenario_run_t *r = s->r;
    const char *dir = path(s, directory_name);
    directory(r, dir);
    text_file(r, language_join(r, dir, "main.goat"), source(s, source_name));
    const char *descriptor = text_file(
        r,
        language_join(r, dir, "module.gmod"),
        descriptor_json(
            r,
            string(
                r,
                "{\"formatVersion\":1,\"name\":\"%s\",\"version\":\"1\",\"main\":\"main.goat\"%s}",
                name,
                fields ? fields : "")));
    return (project_t){dir,
                       descriptor,
                       language_join(r, dir, string(r, "modules/%s/1.gbin", name)),
                       name};
}

static const char *build(suite_t *s,
                         project_t p,
                         const char *modules,
                         const char *mode,
                         bool fail,
                         const char *const *env) {
    const char *args[] =
        {"build", p.descriptor, "--native", mode, modules ? "--module-dir" : NULL, modules, NULL};
    invoke(s, args, "", fail, env, NULL);
    return modules ? language_join(s->r, modules, string(s->r, "%s/1.gbin", p.name)) : p.binary;
}

static bytes_t member(scenario_run_t *r, bytes_t zip, const char *name) {
    format_zip_info_t info;
    char error[256];
    REQUIRE(format_zip_inspect(zip.data, zip.size, SIZE_MAX, &info, error, sizeof(error)));
    size_t count = info.count;
    for (size_t i = 0; i < count; i++) {
        REQUIRE(format_zip_inspect(zip.data, zip.size, i, &info, error, sizeof(error)));
        if (info.name_size == strlen(name) && !memcmp(info.name, name, info.name_size)) {
            size_t n;
            void *data = format_zip_extract(zip.data,
                                            zip.size,
                                            i,
                                            16 * 1024 * 1024,
                                            &n,
                                            error,
                                            sizeof(error));
            REQUIRE(data);
            scenario_defer(r, free_plain, data);
            return (bytes_t){data, n};
        }
    }
    REQUIRE(false);
    return (bytes_t){0};
}

static archive_t archive(scenario_run_t *r, const char *p) {
    bytes_t zip = read_bytes(r, p), json = member(r, zip, "module.json");
    yyjson_doc *doc = yyjson_read(json.data, json.size, 0);
    REQUIRE(doc);
    scenario_defer(r, free_doc, doc);
    return (archive_t){zip, yyjson_doc_get_root(doc)};
}

static const char *field(scenario_run_t *r, yyjson_val *v, const char *key) {
    const char *str = yyjson_get_str(yyjson_obj_get(v, key));
    REQUIRE(str);
    return str;
}

static bool native(scenario_run_t *r, archive_t a) {
    yyjson_val *value = yyjson_obj_get(a.metadata, "native");
    REQUIRE(!value || yyjson_is_null(value) || yyjson_is_arr(value));
    return yyjson_arr_size(value) > 0;
}

static void run_binary(suite_t *s, const char *binary, const char *mode, const char *expected) {
    invoke(s, ARGS(binary, "--native", mode), expected, false, NULL, NULL);
}

static process_watch_result_t directory_empty(void *context, unsigned long *native_error) {
    fs_entries_t found = {0};
    fs_error_t error = {0};
    bool ok = list_fs_directory(context, &found, &error);
    *native_error = error.native_code;
    bool empty = !found.count;
    clear_fs_entries(&found);
    clear_fs_error(&error);
    return !ok ? PROCESS_WATCH_ERROR : empty ? PROCESS_WATCH_OK : PROCESS_WATCH_TIMEOUT;
}

static void wait_empty(scenario_run_t *r, const char *p) {
    scenario_checkpoint(r);
    uint64_t now = process_watch_now_ms(), deadline = scenario_run_deadline(r);
    REQUIRE(now <= UINT64_MAX - 10000);
    if (deadline > now + 10000)
        deadline = now + 10000;
    unsigned long error = 0;
    REQUIRE(process_watch_until(directory_empty, (void *)p, deadline, &error) == PROCESS_WATCH_OK);
    scenario_checkpoint(r);
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
    if (!ok && !symbolic
        && (GetLastError() == ERROR_NOT_SUPPORTED || GetLastError() == ERROR_INVALID_FUNCTION
            || GetLastError() == ERROR_PRIVILEGE_NOT_HELD)) {
        printf("SKIP: hardlink unavailable, Windows error %lu\n", (unsigned long)GetLastError());
        return false;
    }
#else
    bool ok = (symbolic ? symlink(target, alias) : link(target, alias)) == 0;
    if (!ok && !symbolic && (errno == ENOTSUP || errno == EOPNOTSUPP || errno == ENOSYS)) {
        printf("SKIP: hardlink unavailable, errno %d\n", errno);
        return false;
    }
#endif
    REQUIRE(ok);
    return true;
}

static void basic(suite_t *s) {
    scenario_run_t *r = s->r;
    project_t p = project(s,
                          "basic",
                          "basic",
                          "basic",
                          ",\"authors\":[{\"name\":\"Author\",\"email\":\"not an "
                          "email\"}],\"homepage\":\"not a URL\"");
    build(s, p, NULL, "off", false, NULL);
    invoke(s,
           ARGS(p.descriptor, "--optimize", "none", "--native", "auto"),
           "built only when run\n",
           false,
           NULL,
           NULL);
    archive_t a = archive(r, p.binary);
    REQUIRE(!strcmp(field(r, a.metadata, "name"), "basic")
            && !strcmp(field(r, a.metadata, "version"), "1"));
    REQUIRE(!native(r, a));
    member(r, a.zip, "bytecode.bin");
    REQUIRE(!strcmp(field(r, yyjson_arr_get(yyjson_obj_get(a.metadata, "authors"), 0), "email"),
                    "not an email"));
    yyjson_val *producers = yyjson_obj_get(a.metadata, "producers");
    REQUIRE(yyjson_arr_size(producers) == 1);
    yyjson_val *producer = yyjson_arr_get(producers, 0);
    REQUIRE(*field(r, producer, "name") && *field(r, producer, "version")
            && *field(r, producer, "buildId"));
    size_t i, max;
    yyjson_val *key, *value;
    yyjson_obj_foreach(producer, i, max, key, value) {
        const char *v = yyjson_get_str(value);
        REQUIRE(v && !strstr(v, s->root) && !strchr(v, '\\'));
    }
    bytes_t previous = read_bytes(r, p.binary);
    build(s, p, NULL, "off", false, NULL);
    equal(r, "deterministic build", previous, read_bytes(r, p.binary));
    run_binary(s, p.binary, "off", "built only when run\n");
    invoke(s, ARGS("run", p.binary, "--native", "off"), "built only when run\n", false, NULL, NULL);
    invoke(s,
           ARGS("--run", p.binary, "--native", "off"),
           "built only when run\n",
           false,
           NULL,
           NULL);
    equal(r, "run preserves bytecode", previous, read_bytes(r, p.binary));
    text_file(r, language_join(r, p.dir, "main.goat"), source(s, "broken"));
    run_binary(s, p.binary, "off", "built only when run\n");
    build(s, p, NULL, "off", true, NULL);
    equal(r, "bad source preserves old output", previous, read_bytes(r, p.binary));
    text_file(r, language_join(r, p.dir, "main.goat"), source(s, "replacement"));
    build(s, p, NULL, "off", false, NULL);
    bytes_t after = read_bytes(r, p.binary);
    REQUIRE(previous.size != after.size || memcmp(previous.data, after.data, after.size));
    run_binary(s, p.binary, "off", "replacement\n");
    fs_entries_t *files = entries(r, language_join(r, p.dir, "modules/basic"));
    REQUIRE(files->count == 1 && !strcmp(files->names[0], "1.gbin"));
}

static void strict_descriptor(suite_t *s, unsigned index) {
    scenario_run_t *r = s->r;
    project_t p = project(s, "strict", "strict", "strict", NULL);
    put(r,
        p.descriptor,
        read_bytes(r, language_join(r, s->data, string(r, "invalid-%u.gmod", index))));
    if (index < 10)
        invoke(s,
               ARGS("build", p.descriptor, "--native", "off", "--native-target", "host"),
               NULL,
               true,
               NULL,
               NULL);
    else
        invoke(s, ARGS("build", p.descriptor, "--native", "off"), NULL, true, NULL, NULL);
    REQUIRE(!exists(r, language_join(r, p.dir, "modules")));
}

static project_t numeric(suite_t *s) {
    return project(s,
                   "numeric",
                   "numeric",
                   "numeric",
                   ",\"native\":{\"mode\":\"required\",\"target\":\"host\"}");
}

static void valid_override(suite_t *s) {
    scenario_run_t *r = s->r;
    project_t p = project(s,
                          "strict",
                          "strict",
                          "strict",
                          ",\"native\":{\"mode\":\"required\",\"target\":\"linux-unknown-gnu\"},"
                          "\"dependencies\":{\"unused\":\"1\"}");
    scenario_command_result_t result =
        invoke(s,
               ARGS("build", p.descriptor, "--native", "off", "--cc", path(s, "missing-compiler")),
               NULL,
               false,
               NULL,
               NULL);
    REQUIRE(strstr(lower(r, (bytes_t){result.err, result.err_size}), "unused"));
    REQUIRE(!native(r, archive(r, p.binary)));
}

static void native_overrides(suite_t *s) {
    scenario_run_t *r = s->r;
    project_t p = numeric(s);
    const char *missing = path(s, "missing-compiler");
    invoke(s,
           ARGS("build", p.descriptor, "--native", "off", "--cc", missing),
           NULL,
           false,
           NULL,
           NULL);
    archive_t off = archive(r, p.binary);
    run_binary(s, p.binary, "off", "49\n");
    invoke(s, ARGS(p.binary, "--native", "required"), "", true, NULL, NULL);
    bytes_t prior = read_bytes(r, p.binary);
    invoke(s,
           ARGS("build", p.descriptor, "--native", "required", "--cc", missing),
           NULL,
           true,
           NULL,
           NULL);
    equal(r, "required failure preserves output", prior, read_bytes(r, p.binary));
    invoke(s,
           ARGS("build",
                p.descriptor,
                "--native",
                "auto",
                "--native-target",
                "linux-unknown-gnu",
                "--cc",
                missing),
           NULL,
           true,
           NULL,
           NULL);
    equal(r, "target failure preserves output", prior, read_bytes(r, p.binary));
    scenario_command_result_t automatic =
        invoke(s,
               ARGS("build", p.descriptor, "--native", "auto", "--cc", missing),
               NULL,
               false,
               NULL,
               NULL);
    REQUIRE(strstr(lower(r, (bytes_t){automatic.err, automatic.err_size}), "warning"));
    REQUIRE(!native(r, archive(r, p.binary)));
    invoke(s,
           ARGS("build", p.descriptor, "--native", "required", "--cc", s->c->cc),
           "",
           false,
           NULL,
           NULL);
    archive_t on = archive(r, p.binary);
    REQUIRE(yyjson_arr_size(yyjson_obj_get(on.metadata, "native")) == 1);
    REQUIRE(!strcmp(field(r, on.metadata, "mainHash"), field(r, off.metadata, "mainHash")));
    equal(r,
          "native bytecode unchanged",
          member(r, off.zip, "bytecode.bin"),
          member(r, on.zip, "bytecode.bin"));
    REQUIRE(yyjson_equals(yyjson_obj_get(on.metadata, "producers"),
                          yyjson_obj_get(off.metadata, "producers")));
    run_binary(s, p.binary, "required", "49\n");
    run_binary(s, p.binary, "auto", "49\n");
    run_binary(s, p.binary, "off", "49\n");
}

static void damaged_native(suite_t *s) {
    scenario_run_t *r = s->r;
    project_t p = numeric(s);
    invoke(s,
           ARGS("build", p.descriptor, "--native", "required", "--cc", s->c->cc),
           "",
           false,
           NULL,
           NULL);
    archive_t a = archive(r, p.binary);
    const char *library =
        field(r, yyjson_arr_get(yyjson_obj_get(a.metadata, "native"), 0), "library");
    format_zip_info_t info;
    char error[256];
    REQUIRE(format_zip_inspect(a.zip.data, a.zip.size, SIZE_MAX, &info, error, sizeof(error)));
    size_t count = info.count;
    format_zip_entry_t *all = calloc(count, sizeof(*all));
    REQUIRE(all);
    scenario_defer(r, free_plain, all);
    for (size_t i = 0; i < count; i++) {
        REQUIRE(format_zip_inspect(a.zip.data, a.zip.size, i, &info, error, sizeof(error)));
        const char *name = string(r, "%.*s", (int)info.name_size, info.name);
        bytes_t data = member(r, a.zip, name);
        if (!strcmp(name, library)) {
            char *bad = language_owned(r, ALLOC(data.size + 10));
            memcpy(bad, data.data, data.size);
            memcpy(bad + data.size, "corruption", 10);
            data = (bytes_t){bad, data.size + 10};
        }
        format_zip_entry_init(all + i, name, data.data, data.size, false);
    }
    size_t n;
    void *data = format_zip_build(all, count, NULL, 0, false, &n, error, sizeof(error));
    REQUIRE(data);
    scenario_defer(r, free_plain, data);
    const char *bad = put(r, path(s, "damaged.gbin"), (bytes_t){data, n});
    invoke(s, ARGS(bad, "--native", "off"), "", true, NULL, NULL);
}

static void dependency(suite_t *s,
                       const char *dir,
                       const char *source_name,
                       const char *modules,
                       const char *name) {
    project_t p = project(s, dir, name, source_name, NULL);
    build(s, p, modules, "off", false, NULL);
}

static void search_precedence(suite_t *s) {
    scenario_run_t *r = s->r;
    project_t app = project(s, "real/проект 漢字", "app", "app", NULL);
    const char *local = language_join(r, app.dir, "modules"),
               *explicit = language_join(r, s->cwd, "output"),
               *vendor = language_join(r, s->cwd, "vendor");
    dependency(s, "dependency-local", "value-local", local, "value");
    dependency(s, "dependency-explicit", "value-explicit", explicit, "value");
    dependency(s, "dependency-environment", "value-env", vendor, "value");
    const char *env[] = {
        string(r, "GOAT_MODULE_PATH=vendor%c%cvendor", PATH_SEPARATOR, PATH_SEPARATOR),
        NULL};
    invoke(s,
           ARGS("build", app.descriptor, "--native", "off", "--module-dir", "./output"),
           NULL,
           false,
           env,
           NULL);
    const char *binary = language_join(r, explicit, "app/1.gbin");
    archive_t first = archive(r, binary),
              local_dep = archive(r, language_join(r, local, "value/1.gbin"));
    REQUIRE(
        !strcmp(field(r, yyjson_arr_get(yyjson_obj_get(first.metadata, "dependencies"), 0), "hash"),
                field(r, local_dep.metadata, "mainHash")));
    const char *portable = path(s, "distribution/app.gbin");
    copy_file(r, binary, portable, false);
    copy_file(r,
              language_join(r, local, "value/1.gbin"),
              path(s, "distribution/modules/value/1.gbin"),
              false);
    invoke(s,
           ARGS(portable, "--native", "off", "--module-dir", explicit),
           "11\n",
           false,
           NULL,
           NULL);
    invoke(s,
           ARGS(binary, "--native", "off", "--module-dir", explicit),
           "",
           true,
           ARGS(string(r, "GOAT_MODULE_PATH=%s", local)),
           NULL);
    language_remove(r, language_join(r, local, "value/1.gbin"));
    invoke(s,
           ARGS("build", app.descriptor, "--native", "off", "--module-dir", "./output"),
           NULL,
           false,
           env,
           NULL);
    archive_t second = archive(r, binary),
              explicit_dep = archive(r, language_join(r, explicit, "value/1.gbin"));
    REQUIRE(!strcmp(
        field(r, yyjson_arr_get(yyjson_obj_get(second.metadata, "dependencies"), 0), "hash"),
        field(r, explicit_dep.metadata, "mainHash")));
    invoke(s,
           ARGS(binary, "--native", "off", "--module-dir", "./output"),
           "22\n",
           false,
           env,
           NULL);
    language_remove(r, language_join(r, explicit, "value/1.gbin"));
    invoke(s,
           ARGS("build", app.descriptor, "--native", "off", "--module-dir", "./output"),
           NULL,
           false,
           env,
           NULL);
    invoke(s,
           ARGS(binary, "--native", "off", "--module-dir", "./output"),
           "33\n",
           false,
           env,
           NULL);
    text_file(r, language_join(r, local, "value/1.gbin"), "corrupt archive");
    bytes_t old = read_bytes(r, binary);
    invoke(s,
           ARGS("build", app.descriptor, "--native", "off", "--module-dir", "./output"),
           NULL,
           true,
           env,
           NULL);
    equal(r, "corrupt first dependency preserves output", old, read_bytes(r, binary));
}

static void cycle(suite_t *s) {
    scenario_run_t *r = s->r;
    const char *modules = path(s, "cycle/modules");
    project_t app = project(s, "cycle/app", "cycleapp", "cycle-initial", NULL);
    const char *saved = build(s, app, modules, "off", false, NULL);
    bytes_t old = read_bytes(r, saved);
    dependency(s, "cycle/dep", "cycle-dep", modules, "cycledep");
    text_file(r, language_join(r, app.dir, "main.goat"), source(s, "cycle-app"));
    build(s, app, modules, "off", true, NULL);
    equal(r, "cycle preserves output", old, read_bytes(r, saved));
}

static void descriptor_link(suite_t *s) {
    scenario_run_t *r = s->r;
    project_t p = project(s, "link-real", "linked", "linked", NULL);
    dependency(s, "linkdep-real", "linkdep-real", language_join(r, p.dir, "modules"), "linkdep");
    dependency(s, "linkdep-alias", "linkdep-alias", path(s, "link-alias/modules"), "linkdep");
    text_file(r, path(s, "link-alias/main.goat"), source(s, "wrong"));
    const char *alias = path(s, "link-alias/alias.gmod");
    if (!link_file(r, p.descriptor, alias, true)) {
        s->c->skipped = true;
        puts("SKIP: Windows symlink privilege error 1314");
        return;
    }
    invoke(s, ARGS("build", alias, "--native", "off"), NULL, false, NULL, s->cwd);
    REQUIRE(language_exists(r, p.binary) && !exists(r, path(s, "link-alias/modules/linked")));
    bytes_t before = read_bytes(r, p.binary);
    invoke(s, ARGS("build", p.descriptor, "--native", "off"), NULL, false, NULL, s->root);
    equal(r, "descriptor cwd independence", before, read_bytes(r, p.binary));
    invoke(s,
           ARGS("build", alias, "--native", "off", "--module-dir", "./link-output"),
           NULL,
           false,
           NULL,
           NULL);
    const char *binary = language_join(r, s->cwd, "link-output/linked/1.gbin");
    equal(r, "linked explicit output", before, read_bytes(r, binary));
    invoke(s,
           ARGS(binary, "--native", "off", "--module-dir", language_join(r, p.dir, "modules")),
           "real source\n41\n",
           false,
           NULL,
           NULL);
}

static void direct_source(suite_t *s) {
    scenario_run_t *r = s->r;
    const char *modules = language_join(r, s->cwd, "vendor");
    dependency(s, "dependency", "value-env", modules, "value");
    const char *direct = text_file(r, path(s, "direct/main.goat"), source(s, "direct"));
    text_file(r, path(s, "direct/helper.goat"), source(s, "helper"));
    invoke(s,
           ARGS(direct, "--native", "off", "--module-dir", modules),
           "true\n40\n33\n",
           false,
           NULL,
           NULL);
    invoke(s,
           ARGS("run", direct, "--native", "off", "--module-dir", modules),
           "true\n40\n33\n",
           false,
           NULL,
           NULL);
    invoke(s,
           ARGS("--compile", direct, "--native", "off", "--module-dir", modules),
           "",
           false,
           NULL,
           NULL);
    invoke(s,
           ARGS("--run", path(s, "direct/main.gbin"), "--native", "off", "--module-dir", modules),
           "true\n40\n33\n",
           false,
           NULL,
           NULL);
}

static void descriptor_context(suite_t *s) {
    scenario_run_t *r = s->r;
    project_t p = project(s, "run-description", "context", "context", NULL);
    const char *expected = string(r, "true\n%s\n", canonical(r, p.dir));
    invoke(s, ARGS(p.descriptor, "--native", "off"), expected, false, NULL, NULL);
    invoke(s, ARGS("run", p.descriptor, "--native", "off"), expected, false, NULL, NULL);
    REQUIRE(!exists(r, language_join(r, p.dir, "modules")));
}

static const char *find_tool(scenario_run_t *r, const char *name) {
    if (strchr(name, '/') || strchr(name, '\\'))
        return canonical(r, name);
    fs_error_t e = {0};
    char *raw = fs_environment("PATH", &e);
    clear_fs_error(&e);
    if (!raw)
        return NULL;
    const char *search = language_owned(r, raw);
    const char *start = search;
    for (const char *p = search;; p++) {
        if (*p != PATH_SEPARATOR && *p)
            continue;
        const char *dir = string(r, "%.*s", (int)(p - start), start);
        if (!*dir)
            dir = ".";
        const char *candidate = language_join(r, dir, name);
        if (language_exists(r, candidate))
            return canonical(r, candidate);
#ifdef _WIN32
        candidate = string(r, "%s.exe", candidate);
        if (language_exists(r, candidate))
            return canonical(r, candidate);
#endif
        if (!*p)
            break;
        start = p + 1;
    }
    return NULL;
}

static const char *parent_path(scenario_run_t *r, const char *p) {
    const char *slash = strrchr(p, '/');
    REQUIRE(slash);
    return string(r, "%.*s", (int)(slash - p), p);
}

typedef struct {
    const char *compiler, *calls, *temporary;
    const char *const *env;
} controlled_t;

static controlled_t controlled(suite_t *s) {
    scenario_run_t *r = s->r;
    const char *compiler = path(s, "controlled-compiler" SUFFIX), *calls = path(s, "compiler.log"),
               *temporary = path(s, "probe-temporary");
    copy_file(r, s->c->wrapper, compiler, true);
    directory(r, temporary);
    const char **env = language_owned(r, CALLOC(7 * sizeof(*env)));
    env[0] = string(r, "GOAT_TOOLCHAIN_REAL=%s", s->c->cc);
    env[1] = string(r, "GOAT_TOOLCHAIN_LOG=%s", calls);
    env[2] = string(r, "TMPDIR=%s", temporary);
    env[3] = string(r, "TEMP=%s", temporary);
    env[4] = string(r, "TMP=%s", temporary);
    return (controlled_t){compiler, calls, temporary, env};
}

static const char *const *extend(scenario_run_t *r, const char *const *a, const char *const *b) {
    size_t n = 0, m = 0;
    while (a && a[n])
        n++;
    while (b && b[m])
        m++;
    const char **out = language_owned(r, CALLOC((n + m + 1) * sizeof(*out)));
    for (size_t i = 0; i < n; i++)
        out[i] = a[i];
    for (size_t i = 0; i < m; i++)
        out[n + i] = b[i];
    return out;
}

static const char *const *isolated(suite_t *s, controlled_t c) {
    scenario_run_t *r = s->r;
    const char *dir = path(s, "tool-path");
    directory(r, dir);
    const char *names[] = {"cc" SUFFIX, "gcc" SUFFIX, "clang" SUFFIX};
    for (size_t i = 0; i < 3; i++)
        copy_file(r, s->c->wrapper, language_join(r, dir, names[i]), true);
    const char *ld = find_tool(r, "ld");
#ifndef _WIN32
    const char *as = find_tool(r, "as");
    if (as)
        REQUIRE(link_file(r, as, language_join(r, dir, "as"), true));
    if (ld)
        REQUIRE(link_file(r, ld, language_join(r, dir, "ld"), true));
    const char *path_value = dir;
#else
    fs_error_t e = {0};
    char *old = fs_environment("PATH", &e);
    clear_fs_error(&e);
    const char *old_path = old ? language_owned(r, old) : "";
    const char *path_value = string(r, "%s%c%s", dir, PATH_SEPARATOR, old_path);
#endif
    const char *tools = parent_path(r, s->c->cc);
    if (ld)
        tools = string(r, "%s%c%s", tools, PATH_SEPARATOR, parent_path(r, ld));
    return extend(r,
                  c.env,
                  ARGS(string(r, "PATH=%s", path_value), string(r, "COMPILER_PATH=%s", tools)));
}

static void only_compiler(scenario_run_t *r, const char *calls, const char *name, bool nonempty) {
    bytes_t log = read_bytes(r, calls);
    if (nonempty)
        REQUIRE(log.size);
    const char *p = log.data, *end = p + log.size;
    const char *needle = string(r, "%s%s ", name, SUFFIX);
    while (p < end) {
        const char *q = memchr(p, '\n', (size_t)(end - p));
        if (!q)
            q = end;
        bool found = false;
        size_t length = (size_t)(q - p), needle_size = strlen(needle);
        for (size_t i = 0; needle_size <= length && i <= length - needle_size; i++)
            if ((!i || p[i - 1] == '/' || p[i - 1] == '\\') && !memcmp(p + i, needle, needle_size))
                found = true;
        REQUIRE(found);
        p = q == end ? end : q + 1;
    }
}

static void toolchain(suite_t *s) {
    scenario_run_t *r = s->r;
    const char *kind = s->c->kind;
    controlled_t c = controlled(s);
    if (!strcmp(kind, "listing")) {
        scenario_command_result_t result =
            invoke(s,
                   ARGS("--list-native-targets", "--cc", c.compiler, "--lang", "en"),
                   NULL,
                   false,
                   c.env,
                   NULL);
        wait_empty(r, c.temporary);
        const char *out = lower(r, (bytes_t){result.out, result.out_size});
        REQUIRE(strstr(out, "automatic")
                && strstr(out, lower(r, (bytes_t){c.compiler, strlen(c.compiler)})));
        bytes_t first = read_bytes(r, c.calls);
        REQUIRE(first.size);
        invoke(s, ARGS("--list-native-targets", "--cc", c.compiler), NULL, false, c.env, NULL);
        REQUIRE(read_bytes(r, c.calls).size > first.size);
        return;
    }
    if (!strcmp(kind, "listing-link-fail")) {
        invoke(s,
               ARGS("--list-native-targets", "--cc", c.compiler),
               NULL,
               true,
               extend(r, c.env, ARGS("GOAT_TOOLCHAIN_MODE=link-fail")),
               NULL);
        return;
    }
    if (!strcmp(kind, "listing-missing")) {
        invoke(s,
               ARGS("--list-native-targets", "--cc", path(s, "missing-compiler")),
               NULL,
               true,
               NULL,
               NULL);
        return;
    }
    if (!strcmp(kind, "listing-path")) {
        invoke(s,
               ARGS("--list-native-targets"),
               NULL,
               true,
               ARGS(string(r, "PATH=%s", path(s, "empty-path"))),
               NULL);
        return;
    }
    if (!strcmp(kind, "listing-timeout")) {
        invoke(s,
               ARGS("--list-native-targets", "--cc", c.compiler, "--native-probe-timeout", "1"),
               NULL,
               true,
               extend(r, c.env, ARGS("GOAT_TOOLCHAIN_MODE=hang-query")),
               NULL);
        return;
    }
    if (!strncmp(kind, "invalid-timeout-", 16)) {
        const char *timeouts[] = {"0", "-1", "1.5", "184467440737095516160"};
        unsigned index = (unsigned)strtoul(kind + 16, NULL, 10);
        REQUIRE(index < 4);
        invoke(s,
               ARGS("--list-native-targets", "--native-probe-timeout", timeouts[index]),
               NULL,
               true,
               NULL,
               NULL);
        return;
    }
    project_t p = numeric(s);
    if (!strcmp(kind, "auto-preferred") || !strcmp(kind, "auto-fallback")
        || !strcmp(kind, "listing-mixed") || !strcmp(kind, "explicit-no-fallback")) {
        const char *const *env = isolated(s, c);
        text_file(r, c.calls, "");
#ifdef _WIN32
        const char *preferred = "gcc", *fallback = "clang";
#else
        const char *preferred = "cc", *fallback = "gcc";
#endif
        if (!strcmp(kind, "auto-preferred")) {
            invoke(s, ARGS("build", p.descriptor, "--native", "required"), NULL, false, env, NULL);
            only_compiler(r, c.calls, preferred, true);
            return;
        }
        env = extend(r, env, ARGS(string(r, "GOAT_TOOLCHAIN_FAIL=%s%s", preferred, SUFFIX)));
        if (!strcmp(kind, "auto-fallback")) {
            scenario_command_result_t result =
                invoke(s,
                       ARGS("build", p.descriptor, "--native", "required"),
                       NULL,
                       false,
                       env,
                       NULL);
            REQUIRE(
                !contains((bytes_t){result.err, result.err_size}, "controlled compiler failure"));
            REQUIRE(contains(read_bytes(r, c.calls), string(r, "%s%s ", fallback, SUFFIX)));
            return;
        }
        if (!strcmp(kind, "listing-mixed")) {
            scenario_command_result_t result =
                invoke(s, ARGS("--list-native-targets", "--lang", "en"), NULL, false, env, NULL);
            REQUIRE(
                contains((bytes_t){result.out, result.out_size}, "controlled compiler failure")
                || contains((bytes_t){result.err, result.err_size}, "controlled compiler failure"));
            const char *expected = path(s, string(r, "tool-path/%s%s", fallback, SUFFIX));
            REQUIRE(strstr(lower(r, (bytes_t){result.out, result.out_size}),
                           lower(r, (bytes_t){expected, strlen(expected)})));
            return;
        }
        invoke(s,
               ARGS("build",
                    p.descriptor,
                    "--native",
                    "required",
                    "--cc",
                    path(s, string(r, "tool-path/%s%s", preferred, SUFFIX))),
               NULL,
               true,
               env,
               NULL);
        only_compiler(r, c.calls, preferred, false);
        return;
    }
    text_file(r, c.calls, "");
    if (!strcmp(kind, "off-no-probe")) {
        invoke(s,
               ARGS("build", p.descriptor, "--native", "off", "--cc", c.compiler),
               NULL,
               false,
               c.env,
               NULL);
        REQUIRE(!read_bytes(r, c.calls).size);
        return;
    }
    if (!strcmp(kind, "controlled-native")) {
        invoke(s,
               ARGS("build", p.descriptor, "--native", "required", "--cc", c.compiler),
               NULL,
               false,
               c.env,
               NULL);
        REQUIRE(read_bytes(r, c.calls).size);
        run_binary(s, p.binary, "required", "49\n");
        wait_empty(r, c.temporary);
        return;
    }
    if (!strcmp(kind, "descriptor-off")) {
        project_t off = project(s,
                                "descriptor-off",
                                "descriptor-off",
                                "descriptor-off",
                                ",\"native\":{\"mode\":\"off\"}");
        invoke(s, ARGS("build", off.descriptor, "--cc", c.compiler), "", false, c.env, NULL);
        REQUIRE(!read_bytes(r, c.calls).size && !native(r, archive(r, off.binary)));
        return;
    }
    REQUIRE(!strcmp(kind, "default-auto"));
    project_t automatic = project(s, "default-auto", "default-auto", "default-auto", NULL);
    invoke(s, ARGS("build", automatic.descriptor, "--cc", c.compiler), "", false, c.env, NULL);
    REQUIRE(native(r, archive(r, automatic.binary)) && read_bytes(r, c.calls).size);
    run_binary(s, automatic.binary, "required", "3\n");
    wait_empty(r, c.temporary);
}

static project_t default_auto(suite_t *s) {
    project_t p = project(s, "default-auto", "default-auto", "default-auto", NULL);
    invoke(s, ARGS("build", p.descriptor, "--cc", s->c->cc), "", false, NULL, NULL);
    return p;
}

static void dependency_native(suite_t *s) {
    scenario_run_t *r = s->r;
    project_t p = default_auto(s);
    const char *only = text_file(r, path(s, "only-import.goat"), source(s, "only-import"));
    invoke(s,
           ARGS(only,
                "--native",
                "required",
                "--module-dir",
                language_join(r, p.dir, "modules"),
                "--cc",
                s->c->cc),
           "",
           true,
           NULL,
           NULL);
    invoke(s,
           ARGS(only,
                "--native",
                "auto",
                "--module-dir",
                language_join(r, p.dir, "modules"),
                "--cc",
                s->c->cc),
           "3\n9\n",
           false,
           NULL,
           NULL);
}

static uint64_t report_integer(scenario_run_t *r, bytes_t text, const char *key) {
    const char *p = text.data, *end = p + text.size;
    size_t n = strlen(key);
    while (p < end) {
        const char *q = memchr(p, '\n', (size_t)(end - p));
        if (!q)
            q = end;
        if ((size_t)(q - p) > n && p[n] == '=' && !memcmp(p, key, n)) {
            REQUIRE(p + n + 1 < q && p[n + 1] >= '0' && p[n + 1] <= '9');
            char *finish;
            errno = 0;
            unsigned long long value = strtoull(p + n + 1, &finish, 10);
            REQUIRE(!errno && (finish == q || (finish + 1 == q && *finish == '\r')));
            return value;
        }
        p = q == end ? end : q + 1;
    }
    REQUIRE(false);
    return 0;
}

static void reports(suite_t *s) {
    scenario_run_t *r = s->r;
    project_t p = default_auto(s);
    const char *report = path(s, "native-report.txt");
    invoke(s, ARGS(p.binary, "--save-native", report), "3\n", false, NULL, NULL);
    bytes_t text = read_bytes(r, report);
    REQUIRE(report_integer(r, text, "succeeded") > 0 && report_integer(r, text, "bound") > 0);
    bytes_t saved = read_bytes(r, p.binary);
    invoke(s, ARGS(p.binary, "--save-native", p.binary), "", true, NULL, NULL);
    equal(r, "report preserves archive", saved, read_bytes(r, p.binary));
    const char *source_path = language_join(r, p.dir, "main.goat");
    bytes_t before = read_bytes(r, source_path);
    invoke(s,
           ARGS(p.descriptor, "--native", "off", "--save-native", source_path),
           "",
           true,
           NULL,
           NULL);
    equal(r, "report preserves source", before, read_bytes(r, source_path));
}

static void uppercase(suite_t *s) {
    scenario_run_t *r = s->r;
    project_t p = default_auto(s);
    const char *description = language_join(r, p.dir, "uppercase-description.GMOD"),
               *binary = path(s, "upper.GBIN");
    copy_file(r, p.descriptor, description, false);
    invoke(s, ARGS("run", description, "--native", "off"), "3\n", false, NULL, NULL);
    copy_file(r, p.binary, binary, false);
    run_binary(s, binary, "off", "3\n");
}

static void source_collision(suite_t *s) {
    scenario_run_t *r = s->r;
    const char *file =
        text_file(r, path(s, "collision/modules/collision/1.gbin"), source(s, "collision"));
    const char *descriptor =
        text_file(r,
                  path(s, "collision/module.gmod"),
                  "{\"formatVersion\": 1, \"name\": \"collision\", \"version\": \"1\", \"main\": "
                  "\"modules/collision/1.gbin\"}");
    bytes_t saved = read_bytes(r, file);
    invoke(s, ARGS("build", descriptor, "--native", "off"), "", true, NULL, NULL);
    equal(r, "output/source collision", saved, read_bytes(r, file));
}

static void unsafe_output(suite_t *s) {
    scenario_run_t *r = s->r;
    const char *kind = s->c->kind;
    if (!strcmp(kind, "hardlink-output")) {
        project_t p = project(s, "hardlink", "hardlink", "hardlink", NULL);
        directory(r, parent_path(r, p.binary));
        const char *src = language_join(r, p.dir, "main.goat");
        if (!link_file(r, src, p.binary, false)) {
            s->c->skipped = true;
            return;
        }
        bytes_t saved = read_bytes(r, src);
        build(s, p, NULL, "off", true, NULL);
        equal(r, "hardlink source", saved, read_bytes(r, src));
        equal(r, "hardlink output", saved, read_bytes(r, p.binary));
        return;
    }
    if (!strcmp(kind, "directory-output")) {
        project_t p = project(s, "blocked", "blocked", "blocked", NULL);
        directory(r, p.binary);
        const char *sentinel = text_file(r, language_join(r, p.binary, "keep.txt"), "keep");
        build(s, p, NULL, "off", true, NULL);
        equal(r, "directory sentinel", (bytes_t){"keep", 4}, read_bytes(r, sentinel));
        return;
    }
    REQUIRE(!strcmp(kind, "symlink-output"));
    project_t p = project(s, "linked-output", "linked-output", "linked-output", NULL);
    directory(r, parent_path(r, p.binary));
    const char *kept = text_file(r, path(s, "kept.bin"), "must not overwrite");
    if (!link_file(r, kept, p.binary, true)) {
        s->c->skipped = true;
        puts("SKIP: Windows output symlink privilege error 1314");
        return;
    }
    build(s, p, NULL, "off", true, NULL);
    equal(r, "symlink target", (bytes_t){"must not overwrite", 18}, read_bytes(r, kept));
    fs_info_t info;
    fs_error_t error = {0};
    bool ok = inspect_fs_path(p.binary, false, &info, &error);
    clear_fs_error(&error);
    REQUIRE(ok && info.kind == FS_LINK);
}

static const char *legacy(suite_t *s) {
    scenario_run_t *r = s->r;
    const char *src = text_file(r, path(s, "standalone.goat"), source(s, "standalone"));
    invoke(s, ARGS("--native", "off", src), "42\n", false, NULL, NULL);
    invoke(s, ARGS("--compile", "--native", "off", src), "", false, NULL, NULL);
    const char *binary = path(s, "standalone.gbin");
    REQUIRE(language_exists(r, binary));
    invoke(s, ARGS("--run", "--native", "off", binary), "42\n", false, NULL, NULL);
    return binary;
}

static void raw(suite_t *s, bool invalid, unsigned index) {
    scenario_run_t *r = s->r;
    const char *binary = legacy(s);
    bytes_t envelope = read_bytes(r, binary);
    REQUIRE(envelope.size >= 96 && !memcmp(envelope.data, "GOATPG01", 8));
    const unsigned char *bytes = envelope.data;
    uint64_t size = 0;
    for (unsigned i = 0; i < 8; i++)
        size |= (uint64_t)bytes[24 + i] << (8 * i);
    REQUIRE(size <= envelope.size - 96 && size >= 12 && !memcmp(bytes + 96, "GOATBC01", 8));
    bytes_t code = {bytes + 96, (size_t)size};
    unsigned mode = invalid ? index % 3 : index;
    if (invalid) {
        switch (index / 3) {
            case 0:
                code.size = 0;
                break;
            case 1:
                code.size = 7;
                break;
            case 2:
                code.size = 8;
                break;
            case 3:
                code.size--;
                break;
            case 4:
            case 5: {
                unsigned char *bad = language_owned(r, ALLOC(code.size));
                memcpy(bad, code.data, code.size);
                if (index / 3 == 4)
                    memcpy(bad, "GOATBIN1", 8);
                else
                    REQUIRE(format_bytes_patch_le(bad, code.size, 8, UINT32_MAX, 4));
                code.data = bad;
                break;
            }
            default:
                REQUIRE(false);
        }
    }
    const char *file = put(r, path(s, "raw-bytecode.gbin"), code);
    const char *direct[] = {file, "--native", "off", "--lang", "en", NULL},
               *command[] =
                   {mode == 1 ? "run" : "--run", file, "--native", "off", "--lang", "en", NULL};
    scenario_command_result_t result =
        invoke(s, mode ? command : direct, invalid ? "" : "42\n", invalid, NULL, NULL);
    if (invalid)
        REQUIRE(contains((bytes_t){result.err, result.err_size},
                         "Invalid, incompatible or unreadable Goat binary"));
    equal(r, "raw input unchanged", code, read_bytes(r, file));
}

static void body(scenario_run_t *r, void *user) {
    config_t *c = user;
    c->cc = find_tool(r, c->cc);
    REQUIRE(c->cc);
    const char *root = scenario_work_directory(scenario_run_context(r));
    suite_t s = {r,
                 c,
                 root,
                 language_join(r, root, "unrelated"),
                 language_join(r, scenario_data_directory(scenario_run_context(r)), "module-cli")};
    directory(r, s.cwd);
    const char *kind = c->kind;
    if (!strcmp(kind, "missing-include")) {
        const char *file =
            text_file(r, path(&s, "missing-include.goat"), source(&s, "missing-include"));
        invoke(&s, ARGS(file, "--native", "off"), "", true, NULL, NULL);
    } else if (!strcmp(kind, "basic"))
        basic(&s);
    else if (!strncmp(kind, "invalid-descriptor-", 19))
        strict_descriptor(&s, (unsigned)strtoul(kind + 19, NULL, 10));
    else if (!strcmp(kind, "override-valid"))
        valid_override(&s);
    else if (!strcmp(kind, "native-overrides"))
        native_overrides(&s);
    else if (!strcmp(kind, "damaged-native"))
        damaged_native(&s);
    else if (!strcmp(kind, "search-precedence"))
        search_precedence(&s);
    else if (!strcmp(kind, "cycle"))
        cycle(&s);
    else if (!strcmp(kind, "descriptor-link"))
        descriptor_link(&s);
    else if (!strcmp(kind, "direct-source"))
        direct_source(&s);
    else if (!strcmp(kind, "descriptor-context"))
        descriptor_context(&s);
    else if (!strcmp(kind, "dependency-native-required"))
        dependency_native(&s);
    else if (!strcmp(kind, "reports"))
        reports(&s);
    else if (!strcmp(kind, "uppercase"))
        uppercase(&s);
    else if (!strcmp(kind, "source-collision"))
        source_collision(&s);
    else if (!strcmp(kind, "hardlink-output") || !strcmp(kind, "directory-output")
             || !strcmp(kind, "symlink-output"))
        unsafe_output(&s);
    else if (!strcmp(kind, "legacy"))
        legacy(&s);
    else if (!strncmp(kind, "raw-valid-", 10))
        raw(&s, false, (unsigned)strtoul(kind + 10, NULL, 10));
    else if (!strncmp(kind, "raw-invalid-", 12))
        raw(&s, true, (unsigned)strtoul(kind + 12, NULL, 10));
    else
        toolchain(&s);
}

int main(int argc, char **argv) {
#ifdef _WIN32
    fs_error_t args_error = {0};
    if (!fs_arguments(&argc, &argv, &args_error)) {
        clear_fs_error(&args_error);
        return 1;
    }
#endif
    int result = 1;
    bool keep = false, allow = false, valid = argc >= 7 && argc <= 9;
    for (int i = 7; i < argc; i++) {
        if (!strcmp(argv[i], "--keep-work") && !keep)
            keep = true;
        else if (!strcmp(argv[i], "--allow-unsupported-acl") && !allow)
            allow = true;
        else
            valid = false;
    }
    if (valid) {
        fs_error_t error = {0};
        char *goat = absolute_fs_path(argv[2], &error);
        clear_fs_error(&error);
        char *wrapper = absolute_fs_path(argv[3], &error);
        clear_fs_error(&error);
        char *cc = ALLOC(strlen(argv[4]) + 1);
        strcpy(cc, argv[4]);
        if (goat && wrapper && cc) {
            const char *fixed[] = {"missing-include",
                                   "basic",
                                   "override-valid",
                                   "native-overrides",
                                   "damaged-native",
                                   "search-precedence",
                                   "cycle",
                                   "descriptor-link",
                                   "direct-source",
                                   "descriptor-context",
                                   "listing",
                                   "listing-link-fail",
                                   "listing-missing",
                                   "listing-path",
                                   "listing-timeout",
                                   "auto-preferred",
                                   "auto-fallback",
                                   "listing-mixed",
                                   "explicit-no-fallback",
                                   "off-no-probe",
                                   "controlled-native",
                                   "descriptor-off",
                                   "default-auto",
                                   "dependency-native-required",
                                   "reports",
                                   "uppercase",
                                   "source-collision",
                                   "hardlink-output",
                                   "directory-output",
                                   "symlink-output",
                                   "legacy"};

            enum { FIXED = sizeof(fixed) / sizeof(fixed[0]), COUNT = FIXED + 11 + 4 + 3 + 18 };

            config_t configs[COUNT];
            scenario_definition_t definitions[COUNT];
            char kinds[COUNT][64], ids[COUNT][96];
            for (size_t i = 0; i < COUNT; i++) {
                if (i < FIXED)
                    snprintf(kinds[i], sizeof(kinds[i]), "%s", fixed[i]);
                else if (i < FIXED + 11)
                    snprintf(kinds[i], sizeof(kinds[i]), "invalid-descriptor-%zu", i - FIXED);
                else if (i < FIXED + 15)
                    snprintf(kinds[i], sizeof(kinds[i]), "invalid-timeout-%zu", i - FIXED - 11);
                else if (i < FIXED + 18)
                    snprintf(kinds[i], sizeof(kinds[i]), "raw-valid-%zu", i - FIXED - 15);
                else
                    snprintf(kinds[i], sizeof(kinds[i]), "raw-invalid-%zu", i - FIXED - 18);
                snprintf(ids[i], sizeof(ids[i]), "integration.module_cli.%s", kinds[i]);
                configs[i] = (config_t){goat, wrapper, cc, kinds[i], allow, false, false};
                definitions[i] =
                    (scenario_definition_t){ids[i],
                                            !strcmp(kinds[i], "listing-timeout") ? 30000 : 180000,
                                            body,
                                            configs + i,
                                            NULL};
            }
            scenario_options_t options = {.build_directory = argv[5], .data_directory = argv[6]};
            const char *selection[] = {argv[1], keep ? "--keep-work" : NULL};
            result = scenario_registry_main(definitions, COUNT, keep ? 2 : 1, selection, &options);
            if (!result)
                for (size_t i = 0; i < COUNT; i++)
                    if (configs[i].limited || configs[i].skipped)
                        result = 77;
        } else
            fputs("Cannot resolve CLI executables/compiler (supply absolute paths)\n", stderr);
        FREE(cc);
        FREE(wrapper);
        FREE(goat);
    } else
        fputs("usage: module_cli_scenarios ID GOAT WRAPPER CC BUILD DATA [--keep-work] "
              "[--allow-unsupported-acl]\n",
              stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
