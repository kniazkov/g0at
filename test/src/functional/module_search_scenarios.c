/** @file module_search_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Ordered search, immutable paths, filesystem failures and permission fixtures.
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

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <windows.h>
#    define SEP ";"
#else
#    include <sys/stat.h>
#    include <unistd.h>
#    define SEP ":"
#endif
#define N(a) (sizeof(a) / sizeof((a)[0]))

enum { OK, ARGUMENT, IO, NOT_FOUND, INVALID, IDENTITY, HASH };

typedef struct {
    const char *exe, *vectors;
    size_t count;
    yyjson_mut_doc *schema;
} configuration_t;

typedef struct {
    const unsigned char *data;
    size_t size;
    const char *hash;
} bytes_t;

typedef struct {
    const char *module_dir, *environment, *name, *version, *hash, *cwd, *after_cwd;
    const char *mode, *after_environment, *retarget;
    const char *const *process_environment;
    int status, preparation;
    bool unprivileged;
} request_t;

typedef struct {
    yyjson_val *prepared, *result;
} response_t;

static bool free_mut(void *p) {
    yyjson_mut_doc_free(p);
    return true;
}

static bool free_doc(void *p) {
    yyjson_doc_free(p);
    return true;
}

static bool free_malloc(void *p) {
    free(p);
    return true;
}

static void *owned(scenario_run_t *r, void *p) {
    scenario_require(r, p != NULL, "allocation");
    scenario_defer(r, free_malloc, p);
    return p;
}

static yyjson_val *get(yyjson_val *v, const char *key) {
    return yyjson_obj_get(v, key);
}

static const char *string(scenario_run_t *r, yyjson_val *v) {
    scenario_require(r, yyjson_is_str(v), "JSON string");
    return yyjson_get_str(v);
}

static yyjson_val *parse(scenario_run_t *r, const void *s, size_t n) {
    yyjson_doc *d = yyjson_read(s, n, 0);
    scenario_require(r, d != NULL, "JSON response");
    scenario_defer(r, free_doc, d);
    return yyjson_doc_get_root(d);
}

static yyjson_mut_doc *mut(scenario_run_t *r, const char *s, size_t n) {
    char error[256] = {0};
    yyjson_mut_doc *d = format_json_parse(s, n, error, sizeof(error));
    scenario_require(r, d != NULL, error);
    scenario_defer(r, free_mut, d);
    return d;
}

static const char *path(scenario_run_t *r, const char *s) {
    return language_join(r, scenario_work_directory(scenario_run_context(r)), s);
}

static const char *join(scenario_run_t *r, const char *a, const char *b) {
    return language_join(r, a, b);
}

static const char *concat(scenario_run_t *r, const char *a, const char *b, const char *c) {
    char *p = language_owned(r, ALLOC(strlen(a) + strlen(b) + strlen(c) + 1));
    strcpy(p, a);
    strcat(p, b);
    strcat(p, c);
    return p;
}

static const char *put(scenario_run_t *r, const char *p, const void *data, size_t n) {
    char *parent = language_owned(r, ALLOC(strlen(p) + 1));
    strcpy(parent, p);
    for (char *q = parent + 1; *q; q++)
        if (*q == '/') {
            *q = 0;
            fs_info_t info;
            fs_error_t e = {0};
            bool exists = inspect_fs_path(parent, true, &info, &e);
            clear_fs_error(&e);
            if (!exists)
                language_directory(r, parent);
            else
                scenario_require(r, info.kind == FS_DIRECTORY, "fixture parent");
            *q = '/';
        }
    language_write(r, p, data, n);
    return p;
}

static const char *empty(scenario_run_t *r, const char *p) {
    return put(r, p, "", 0);
}

static void write_archive(scenario_run_t *r, const char *p, bytes_t b) {
    put(r, p, b.data, b.size);
}

static void number(unsigned char *p, uint32_t n) {
    for (unsigned i = 0; i < 4; i++)
        p[i] = (unsigned char)(n >> (8 * i));
}

static bytes_t archive(scenario_run_t *r,
                       configuration_t *c,
                       const char *name,
                       const char *version,
                       const char *file) {
    unsigned char *image = owned(r, calloc(1, 208));
    memcpy(image, "GOATBC01", 8);
    const uint32_t header[] = {1, 64, 208, 6, 0, 1, 1, 2, 64, 112, 112, 128, 160, 208};
    for (size_t i = 0; i < N(header); i++)
        number(image + 8 + 4 * i, header[i]);
    const unsigned char ops[] = {1, 47, 5, 0, 7, 50};
    for (size_t i = 0; i < N(ops); i++)
        image[64 + i * 8] = ops[i];
    number(image + 68, 4);
    number(image + 116, 6);
    const uint32_t function[] = {1, 0, 1, 4, 0, 0, 0xffffffff, 0};
    for (size_t i = 0; i < N(function); i++)
        number(image + 128 + 4 * i, function[i]);
    const uint32_t locations[] = {0, 4, 0, 1, 1, 0, 4, 2, 0, 2, 1, 1};
    for (size_t i = 0; i < N(locations); i++)
        number(image + 160 + 4 * i, locations[i]);
    size_t n;
    const char *base = language_read(r, join(r, c->vectors, "empty.module.json"), &n);
    yyjson_mut_doc *d = mut(r, base, n);
    yyjson_mut_val *v = yyjson_mut_doc_get_root(d);
#define SET(obj, k, val)                                                                           \
    scenario_require(r, yyjson_mut_obj_put(obj, yyjson_mut_str(d, k), val), "metadata field")
    SET(v, "name", yyjson_mut_str(d, name));
    SET(v, "version", yyjson_mut_str(d, version));
    yyjson_mut_val *f = yyjson_mut_arr_get(yyjson_mut_obj_get(v, "files"), 0);
    SET(f, "name", yyjson_mut_str(d, file));
    const char *functions = "[{\"functionId\":1,\"file\":0,\"instruction\":1,\"parameters\":[]}]";
    yyjson_mut_doc *fd = mut(r, functions, strlen(functions));
    SET(v, "functions", yyjson_mut_val_mut_copy(d, yyjson_mut_doc_get_root(fd)));
    char *digest = owned(r, malloc(65)), error[256] = {0};
    scenario_require(r,
                     format_binary_main_hash(v,
                                             yyjson_mut_doc_get_root(c->schema),
                                             image,
                                             208,
                                             digest,
                                             error,
                                             sizeof(error)),
                     error);
    SET(v, "mainHash", yyjson_mut_str(d, digest));
#undef SET
    char *json = owned(r, yyjson_mut_write(d, 0, &n));
    format_zip_entry_t entries[2];
    format_zip_entry_init(&entries[0], "module.json", json, n, false);
    format_zip_entry_init(&entries[1], "bytecode.bin", image, 208, false);
    size_t size;
    unsigned char *zip = format_zip_build(entries, 2, NULL, 0, false, &size, error, sizeof(error));
    scenario_require(r, zip != NULL, error);
    owned(r, zip);
    return (bytes_t){zip, size, digest};
}

static bool same_path(const char *a, const char *b) {
#ifdef _WIN32
    for (; *a && *b; a++, b++) {
        unsigned char x = *a == '\\' ? '/' : (unsigned char)*a,
                      y = *b == '\\' ? '/' : (unsigned char)*b;
        if (tolower(x) != tolower(y))
            return false;
    }
    return !*a && !*b;
#else
    return !strcmp(a, b);
#endif
}

static void paths(scenario_run_t *r, yyjson_val *actual, const char *const *expected, size_t n) {
    scenario_require(r,
                     yyjson_is_arr(actual) && yyjson_arr_size(actual) == n,
                     "ordered path count");
    for (size_t i = 0; i < n; i++)
        scenario_require(r,
                         same_path(string(r, yyjson_arr_get(actual, i)), expected[i]),
                         "ordered path value");
}

static void result_path(scenario_run_t *r, response_t p, const char *expected) {
    scenario_require(r,
                     same_path(string(r, get(p.result, "archive")), expected),
                     "selected archive path");
}

static response_t run(scenario_run_t *r, configuration_t *c, const char *source, request_t q) {
#define OR(s, d) ((s) ? (s) : (d))
    const char *args[] = {c->exe,
                          source,
                          OR(q.module_dir, ""),
                          OR(q.environment, ""),
                          OR(q.name, "sample"),
                          OR(q.version, "1"),
                          OR(q.hash, ""),
                          OR(q.after_cwd, ""),
                          OR(q.mode, "explicit"),
                          OR(q.after_environment, ""),
                          OR(q.retarget, ""),
                          q.unprivileged ? "unprivileged" : "",
                          NULL};
#undef OR
    child_options_t options = {.executable = c->exe,
                               .arguments = args,
                               .working_directory = q.cwd,
                               .environment = q.process_environment,
                               .stdin_data = "\n",
                               .stdin_size = 1};
    scenario_command_result_t out = scenario_command_capture(r, &options);
    c->count++;
    scenario_require(r, out.exit_code == 0, "search host exit");
    const char *line = memchr(out.out, '\n', out.out_size);
    scenario_require(r, line != NULL, "preparation response line");
    response_t p = {parse(r, out.out, (size_t)(line - (const char *)out.out)), NULL};
    scenario_require(r,
                     yyjson_is_int(get(p.prepared, "status"))
                         && yyjson_get_int(get(p.prepared, "status")) == q.preparation,
                     "preparation status");
    if (!q.preparation) {
        size_t offset = (size_t)(line + 1 - (const char *)out.out);
        p.result = parse(r, line + 1, out.out_size - offset);
        scenario_require(r,
                         yyjson_is_int(get(p.result, "status"))
                             && yyjson_get_int(get(p.result, "status")) == q.status,
                         "search status");
        if (q.status) {
            const char *d = string(r, get(p.result, "diagnostic"));
            scenario_require(r, *d && strstr(d, "fixture.goat"), "source diagnostic");
            yyjson_val *a = get(p.result, "attempted");
            scenario_require(r, yyjson_is_arr(a), "attempted paths");
            for (size_t i = 0; i < yyjson_arr_size(a); i++)
                scenario_require(r,
                                 strstr(d, string(r, yyjson_arr_get(a, i))) != NULL,
                                 "attempted path diagnostic");
        }
    }
    return p;
}

static bool
symlink_fixture(scenario_run_t *r, const char *target, const char *alias, bool directory) {
#ifdef _WIN32
    int nt = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, target, -1, NULL, 0),
        na = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, alias, -1, NULL, 0);
    scenario_require(r, nt > 0 && na > 0, "UTF-8 symlink paths");
    wchar_t *t = language_owned(r, ALLOC((size_t)nt * sizeof(*t))),
            *a = language_owned(r, ALLOC((size_t)na * sizeof(*a)));
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, target, -1, t, nt);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, alias, -1, a, na);
    DWORD flags = directory ? 1 : 0;
    bool ok = CreateSymbolicLinkW(a, t, flags | 2) != 0;
    if (!ok && GetLastError() == ERROR_INVALID_PARAMETER)
        ok = CreateSymbolicLinkW(a, t, flags) != 0;
    if (!ok && GetLastError() == ERROR_PRIVILEGE_NOT_HELD) {
        puts("SKIP symlink fixtures: Windows privilege not held");
        return false;
    }
#else
    (void)directory;
    bool ok = symlink(target, alias) == 0;
#endif
    scenario_require(r, ok, "create symlink fixture");
    return true;
}

static const char *const *environment(scenario_run_t *r, const char *value) {
    const char *const *all =
        language_environment_override(r, "GOAT_MODULE_PATH", value ? value : "");
    if (value)
        return all;
    size_t n = 0;
    while (all[n])
        n++;
    const char **filtered = language_owned(r, ALLOC((n + 1) * sizeof(*filtered)));
    size_t used = 0;
    for (size_t i = 0; i < n; i++) {
#ifdef _WIN32
        bool match = !_strnicmp(all[i], "GOAT_MODULE_PATH=", 17);
#else
        bool match = !strncmp(all[i], "GOAT_MODULE_PATH=", 17);
#endif
        if (!match)
            filtered[used++] = all[i];
    }
    filtered[used] = NULL;
    return filtered;
}

/* Cleanup callbacks must not invoke scenario APIs. ACL restoration has its own
 * finite deadline outside the scenario deadline, and runs after any failure. */
typedef struct {
    const char *path;
    unsigned mode;
    bool active;
} permission_t;

static bool restore_permission(void *user) {
    permission_t *p = user;
    if (!p->active)
        return true;
#ifdef _WIN32
    const char *args[] = {"icacls", p->path, "/remove:d", "*S-1-1-0", NULL};
    child_options_t o = {.executable = "icacls",
                         .arguments = args,
                         .deadline = child_deadline_after(10000),
                         .empty_stdin = true,
                         .capture_output = true};
    child_process_t *child = start_child_process(&o);
    if (!child)
        return false;
    const child_result_t *result = wait_child_process(child);
    bool ok = result->status == CHILD_EXITED && result->exit_code == 0 && !result->cleanup_error;
    if (!ok)
        fwrite(result->err.data, 1, result->err.size, stderr);
    destroy_child_process(child);
#else
    bool ok = chmod(p->path, p->mode) == 0;
#endif
    if (ok)
        p->active = false;
    return ok;
}

static permission_t *
restrict_permission(scenario_run_t *r, const char *p, bool directory, bool wrapper) {
    permission_t *s = owned(r, calloc(1, sizeof(*s)));
    s->path = p;
    s->mode = directory ? 0755 : 0644;
    scenario_defer(r, restore_permission, s);
    s->active = true;
#ifdef _WIN32
    const char *rights = wrapper     ? "*S-1-1-0:(RD)"
                         : directory ? "*S-1-1-0:(OI)(CI)(RD,RA,X)"
                                     : "*S-1-1-0:(RD,RA,X)";
    const char *args[] = {"icacls", p, "/deny", rights, NULL};
    scenario_require(r,
                     language_command(r, "icacls", args, NULL).exit_code == 0,
                     "restrict disposable ACL");
#else
    (void)wrapper;
    scenario_require(r, chmod(p, 0) == 0, "restrict fixture permissions");
#endif
    return s;
}

static void body(scenario_run_t *r, void *user) {
    configuration_t *c = user;
    size_t size;
    const char *schema = language_read(r, join(r, c->vectors, "../schema-source.json"), &size);
    c->schema = mut(r, schema, size);
    const char *api[] = {c->exe, NULL};
    scenario_require(r, language_command(r, c->exe, api, NULL).exit_code == 0, "search API checks");
    const char *root = scenario_work_directory(scenario_run_context(r)),
               *project = path(r, "проект"), *launch = path(r, "launch"),
               *elsewhere = path(r, "cwd");
    language_directory(r, project);
    language_directory(r, launch);
    language_directory(r, elsewhere);
    const char *source = empty(r, join(r, project, "app.goat"));
    const char *local = join(r, project, "modules"), *explicit = path(r, "output"),
               *env1 = path(r, "env1"), *env2 = path(r, "env2");
    const char *directories[] = {local, explicit, env1, env2}, *candidates[4];
    bytes_t good = archive(r, c, "sample", "1", "main.goat"),
            other = archive(r, c, "sample", "1", "other.goat");
    for (size_t i = 0; i < 4; i++) {
        candidates[i] = join(r, directories[i], "sample/1.gbin");
        write_archive(r, candidates[i], good);
    }
    const char *env = concat(r, env1, SEP, env2);
    request_t basic = {.module_dir = explicit, .environment = env};
    response_t p;
    const char *suffixes[] = {"app.gmod", "app.goat", "app.gbin"};
    for (size_t i = 0; i < N(suffixes); i++) {
        const char *entry = empty(r, join(r, project, suffixes[i]));
        p = run(r, c, entry, basic);
        paths(r, get(p.prepared, "directories"), directories, 4);
        paths(r, get(p.result, "attempted"), candidates, 1);
        result_path(r, p, candidates[0]);
        scenario_require(r,
                         !strcmp(string(r, get(p.result, "hash")), good.hash),
                         "independent archive hash");
    }
    for (size_t i = 0; i < 4; i++) {
        p = run(r, c, source, basic);
        paths(r, get(p.result, "attempted"), candidates, i + 1);
        result_path(r, p, candidates[i]);
        language_remove(r, candidates[i]);
    }
    request_t q = basic;
    q.status = NOT_FOUND;
    p = run(r, c, source, q);
    paths(r, get(p.result, "attempted"), candidates, 4);
    const char *absence = string(r, get(p.result, "diagnostic")), *failures[4];
    write_archive(r, join(r, local, "nested/sample/1.gbin"), good);
    write_archive(r, join(r, local, "sample/2.gbin"), good);
    write_archive(r, join(r, env2, "modules/sample/1.gbin"), good);
    run(r, c, source, q);
    write_archive(r, candidates[1], good);
    bytes_t bad[] = {{(const unsigned char *)"broken zip", 10, NULL},
                     archive(r, c, "wrong", "1", "main.goat"),
                     archive(r, c, "sample", "2", "main.goat"),
                     other};
    const int statuses[] = {INVALID, IDENTITY, IDENTITY, HASH};
    for (size_t i = 0; i < N(bad); i++) {
        write_archive(r, candidates[0], bad[i]);
        q = basic;
        q.hash = good.hash;
        q.status = statuses[i];
        p = run(r, c, source, q);
        paths(r, get(p.result, "attempted"), candidates, 1);
        scenario_require(r,
                         yyjson_is_null(get(p.result, "archive")),
                         "bad first candidate has no archive");
        failures[i] = string(r, get(p.result, "diagnostic"));
        scenario_require(r,
                         !strstr(failures[i], candidates[1]),
                         "bad first candidate stops fallback");
    }
    const char *diagnostics[] = {absence, failures[0], failures[1], failures[2], failures[3]};
    size_t distinct = 0;
    for (size_t i = 0; i < N(diagnostics); i++) {
        bool seen = false;
        for (size_t j = 0; j < i; j++)
            if (!strcmp(diagnostics[i], diagnostics[j]))
                seen = true;
        if (!seen)
            distinct++;
    }
    scenario_require(r, distinct >= 4, "distinct failure diagnostics");
    write_archive(r, candidates[0], good);
    q = basic;
    q.hash = good.hash;
    run(r, c, source, q);
    q.hash = "bad";
    q.status = ARGUMENT;
    run(r, c, source, q);
    q = basic;
    q.name = "../sample";
    q.status = ARGUMENT;
    run(r, c, source, q);
    q = basic;
    q.version = "../1";
    q.status = ARGUMENT;
    run(r, c, source, q);
    language_remove(r, candidates[0]);
    language_directory(r, candidates[0]);
    q = basic;
    q.status = IO;
    p = run(r, c, source, q);
    paths(r, get(p.result, "attempted"), candidates, 1);
    language_remove(r, candidates[0]);
#ifndef _WIN32
    scenario_require(r, mkfifo(candidates[0], 0600) == 0, "FIFO fixture");
    run(r, c, source, q);
    language_remove(r, candidates[0]);
#endif
    const char *aliases = concat(r, SEP, local, SEP);
    aliases = concat(r, aliases, explicit, SEP);
    aliases = concat(r, aliases, join(r, explicit, "."), SEP SEP);
    aliases = concat(r, aliases, env1, SEP);
    p = run(r, c, source, (request_t){.module_dir = local, .environment = aliases});
    const char *dedup[] = {local, explicit, env1};
    paths(r, get(p.prepared, "directories"), dedup, 3);
    const char *relative = join(r, elsewhere, "relative-окружение"),
               *relative_candidate = join(r, relative, "sample/1.gbin");
    write_archive(r, relative_candidate, good);
    p = run(r,
            c,
            source,
            (request_t){.module_dir = "./relative-окружение",
                        .environment = ".relative-missing" SEP "./relative-окружение",
                        .cwd = elsewhere,
                        .after_cwd = root});
    const char *relative_dirs[] = {local, relative, join(r, elsewhere, ".relative-missing")};
    paths(r, get(p.prepared, "directories"), relative_dirs, 3);
    result_path(r, p, relative_candidate);
    put(r, join(r, elsewhere, "replacement/sample/1.gbin"), "must not reread environment", 26);
    p = run(r,
            c,
            source,
            (request_t){.cwd = elsewhere,
                        .after_cwd = root,
                        .mode = "process",
                        .process_environment = environment(r, "./relative-окружение"),
                        .after_environment = "replacement"});
    paths(r, get(p.prepared, "directories"), relative_dirs, 2);
    result_path(r, p, relative_candidate);
    put(r, join(r, elsewhere, "sample/1.gbin"), "empty elements must not mean cwd", 31);
    const char *values[] = {NULL, "", SEP SEP SEP};
    for (size_t i = 0; i < N(values); i++) {
        p = run(r,
                c,
                source,
                (request_t){.cwd = elsewhere,
                            .mode = "process",
                            .process_environment = environment(r, values[i]),
                            .status = NOT_FOUND});
        paths(r, get(p.prepared, "directories"), directories, 1);
        paths(r, get(p.result, "attempted"), candidates, 1);
    }
    const char *modes[] = {"absent", "explicit"};
    for (size_t i = 0; i < N(modes); i++) {
        p = run(r, c, source, (request_t){.cwd = elsewhere, .mode = modes[i], .status = NOT_FOUND});
        paths(r, get(p.prepared, "directories"), directories, 1);
    }
#ifndef _WIN32
    const char *literal = path(r, "one;two");
    write_archive(r, join(r, literal, "sample/1.gbin"), good);
    run(r, c, source, (request_t){.environment = literal});
#endif
    const char *literals[] = {"~", "$GOAT_MODULE_PATH", "%GOAT_MODULE_PATH%"};
    for (size_t i = 0; i < N(literals); i++) {
        const char *candidate = join(r, join(r, elsewhere, literals[i]), "sample/1.gbin");
        write_archive(r, candidate, good);
        p = run(r, c, source, (request_t){.environment = literals[i], .cwd = elsewhere});
        result_path(r, p, candidate);
    }
    p = run(r, c, source, (request_t){.environment = SEP SEP SEP, .status = NOT_FOUND});
    paths(r, get(p.prepared, "directories"), directories, 1);
#ifndef _WIN32
    const char *missing = path(r, "missing/a/../b"), *missing_b = path(r, "missing/b");
    p = run(r,
            c,
            source,
            (request_t){.module_dir = missing, .environment = missing_b, .status = NOT_FOUND});
    const char *missing_dirs[] = {local, missing, missing_b};
    paths(r, get(p.prepared, "directories"), missing_dirs, 3);
    empty(r, path(r, "file"));
    write_archive(r, path(r, "vendor/sample/1.gbin"), good);
    run(r, c, source, (request_t){.module_dir = path(r, "file/../vendor"), .status = IO});
    run(r,
        c,
        source,
        (request_t){.module_dir = path(r, "nonexistent/../vendor"), .status = NOT_FOUND});
#else
    const char *incomplete[] = {"C:relative", "/relative", "\\relative"};
    for (size_t i = 0; i < N(incomplete); i++)
        run(r, c, source, (request_t){.module_dir = incomplete[i], .preparation = IO});
#endif
    p = run(r,
            c,
            source,
            (request_t){.module_dir = join(r, project, "./modules"),
                        .environment = concat(r, project, "/./modules//", ""),
                        .status = NOT_FOUND});
    paths(r, get(p.prepared, "directories"), directories, 1);
    p = run(r,
            c,
            source,
            (request_t){.module_dir = path(r, "missing"),
                        .environment = path(r, "also-missing"),
                        .status = NOT_FOUND});
    scenario_require(r,
                     yyjson_arr_size(get(p.prepared, "directories")) == 3,
                     "unused missing directories legal");
    run(r, c, path(r, "absent.goat"), (request_t){.preparation = IO});
    const char *alias = join(r, launch, "app.goat");
    if (symlink_fixture(r, source, alias, false)) {
        put(r, join(r, launch, "modules/sample/1.gbin"), "not the real project", 20);
        write_archive(r, candidates[0], good);
        const char *links[] = {"app.gmod", "app.gbin"};
        for (size_t i = 0; i < N(links); i++) {
            const char *target = join(r, project, links[i]), *link = join(r, launch, links[i]);
            scenario_require(r, symlink_fixture(r, target, link, false), "subsequent symlink");
            p = run(r, c, link, basic);
            scenario_require(r,
                             same_path(string(r, get(p.prepared, "root")), target),
                             "canonical linked root");
            result_path(r, p, candidates[0]);
        }
        const char *replacement = empty(r, join(r, elsewhere, "replacement.goat"));
        put(r, join(r, elsewhere, "modules/sample/1.gbin"), "wrong project", 13);
        q = basic;
        q.retarget = replacement;
        q.after_cwd = elsewhere;
        p = run(r, c, alias, q);
        scenario_require(r,
                         same_path(string(r, get(p.prepared, "root")), source),
                         "root snapshotted before retarget");
        result_path(r, p, candidates[0]);
        const char *directory_alias = path(r, "modules-alias");
        scenario_require(r, symlink_fixture(r, local, directory_alias, true), "directory alias");
        p = run(r, c, source, (request_t){.module_dir = directory_alias, .environment = local});
        paths(r, get(p.prepared, "directories"), directories, 1);
        language_remove(r, candidates[0]);
        scenario_require(r,
                         symlink_fixture(r, path(r, "no-such-module"), candidates[0], false),
                         "dangling module");
        q = basic;
        q.status = IO;
        p = run(r, c, source, q);
        paths(r, get(p.result, "attempted"), candidates, 1);
        language_remove(r, candidates[0]);
    }
    const char *non_directory = empty(r, path(r, "not-a-directory")),
               *non_child = join(r, non_directory, "child");
    p = run(r,
            c,
            source,
            (request_t){.environment = concat(r, non_child, SEP, explicit), .status = IO});
    scenario_require(r,
                     yyjson_is_int(get(p.result, "filesystem"))
                         && yyjson_get_int(get(p.result, "filesystem")) != 2
                         && yyjson_get_uint(get(p.result, "native_code")),
                     "not-directory native failure");
    const char *non_attempts[] = {candidates[0], join(r, non_child, "sample/1.gbin")};
    paths(r, get(p.result, "attempted"), non_attempts, 2);
    const char *broken_parent = path(r, "broken-parent");
    if (symlink_fixture(r, path(r, "missing-target"), broken_parent, true)) {
        const char *child = join(r, broken_parent, "child");
        p = run(r,
                c,
                source,
                (request_t){.environment = concat(r, child, SEP, explicit), .status = IO});
        const char *attempts[] = {candidates[0], join(r, child, "sample/1.gbin")};
        paths(r, get(p.result, "attempted"), attempts, 2);
    }
    const char *wrapper = path(r, "denied-wrapper"), *denied = join(r, wrapper, "denied"),
               *denied_candidate = join(r, denied, "sample/1.gbin");
    write_archive(r, denied_candidate, good);
    write_archive(r, candidates[1], good);
#ifndef _WIN32
    if (geteuid() == 0)
        scenario_require(r,
                         chmod(root, 0755) == 0,
                         "root fixture traversable for unprivileged host");
#endif
    const char *blocked_paths[] = {denied_candidate, denied};
    for (size_t i = 0; i < N(blocked_paths); i++) {
        const char *blocked = blocked_paths[i];
        bool directory = i == 1;
        permission_t *restriction = restrict_permission(r, blocked, directory, false),
                     *parent_restriction = NULL;
#ifdef _WIN32
        if (directory)
            parent_restriction = restrict_permission(r, wrapper, true, true);
#endif
        const char *probe[] =
            {c->exe, "--probe", blocked, directory ? "directory" : "file", "unprivileged", NULL};
        scenario_command_result_t probe_result = language_command(r, c->exe, probe, NULL);
        scenario_require(r, probe_result.exit_code == 0, "independent permission probe exit");
        const char *expected;
#ifdef _WIN32
        expected = "5\n";
#else
        if (probe_result.out_size == 2 && !memcmp(probe_result.out, "0\n", 2)) {
            scenario_require(r, !getenv("CI"), "CI must enforce access-denied search fixtures");
            puts("SKIP access-denied fixture: filesystem ignores mode bits");
            scenario_require(r,
                             restore_permission(restriction),
                             "restore unsupported permission fixture");
            continue;
        }
        char error_number[32];
        snprintf(error_number, sizeof(error_number), "%d\n", EACCES);
        expected = error_number;
#endif
        scenario_require_bytes(r,
                               "independent access denied",
                               expected,
                               strlen(expected),
                               probe_result.out,
                               probe_result.out_size,
                               true);
        write_archive(r, candidates[0], good);
        p = run(r, c, source, (request_t){.environment = denied, .unprivileged = true});
        paths(r, get(p.result, "attempted"), candidates, 1);
        language_remove(r, candidates[0]);
        const char *denied_environment = concat(r, denied, SEP, explicit);
        p = run(r,
                c,
                source,
                (request_t){.environment = denied_environment, .unprivileged = true, .status = IO});
        scenario_require(r,
                         yyjson_is_int(get(p.result, "filesystem"))
                             && yyjson_get_int(get(p.result, "filesystem")) == 3
                             && yyjson_get_uint(get(p.result, "native_code")),
                         "real permission failure native code");
        scenario_require(r,
                         *string(r, get(p.result, "operation"))
                             && *string(r, get(p.result, "error_path")),
                         "permission operation and path");
        const char *attempts[] = {candidates[0], denied_candidate};
        paths(r, get(p.result, "attempted"), attempts, 2);
        const char *diagnostic = string(r, get(p.result, "diagnostic"));
        scenario_require(r,
                         !strstr(diagnostic, candidates[1]),
                         "permission failure stops fallback");
        for (size_t j = 0; j < N(diagnostics); j++)
            scenario_require(r,
                             strcmp(diagnostic, diagnostics[j]) != 0,
                             "permission diagnostic distinct");
        if (directory) {
            write_archive(r,
                          join(r, explicit, "missing/1.gbin"),
                          archive(r, c, "missing", "1", "main.goat"));
            p = run(r,
                    c,
                    source,
                    (request_t){.name = "missing",
                                .environment = denied_environment,
                                .unprivileged = true,
                                .status = IO});
            scenario_require(r,
                             yyjson_is_int(get(p.result, "filesystem"))
                                 && yyjson_get_int(get(p.result, "filesystem")) == 3
                                 && yyjson_get_uint(get(p.result, "native_code")),
                             "uninspectable missing child is denial");
            const char *missing_attempts[] = {join(r, local, "missing/1.gbin"),
                                              join(r, denied, "missing/1.gbin")};
            paths(r, get(p.result, "attempted"), missing_attempts, 2);
        }
        if (parent_restriction)
            scenario_require(r, restore_permission(parent_restriction), "restore parent ACL");
        scenario_require(r, restore_permission(restriction), "restore child permissions");
    }
#ifndef _WIN32
    write_archive(r,
                  join(r, explicit, "missing/1.gbin"),
                  archive(r, c, "missing", "1", "main.goat"));
    permission_t *search_only = owned(r, calloc(1, sizeof(*search_only)));
    *search_only = (permission_t){denied, 0755, true};
    scenario_defer(r, restore_permission, search_only);
    scenario_require(r, chmod(denied, 0111) == 0, "search-only directory fixture");
    p = run(r,
            c,
            source,
            (request_t){.name = "missing",
                        .environment = concat(r, denied, SEP, explicit),
                        .unprivileged = true});
    const char *search_attempts[] = {join(r, local, "missing/1.gbin"),
                                     join(r, denied, "missing/1.gbin"),
                                     join(r, explicit, "missing/1.gbin")};
    paths(r, get(p.result, "attempted"), search_attempts, 3);
    scenario_require(r, restore_permission(search_only), "restore search-only fixture");
#endif
    printf("Module directory search: %zu fixtures passed\n", c->count);
}

int main(int argc, char **argv) {
#ifdef _WIN32
    fs_error_t arg_error = {0};
    if (!fs_arguments(&argc, &argv, &arg_error)) {
        clear_fs_error(&arg_error);
        return 1;
    }
#endif
    int result = 1;
    if (argc == 2 && !strcmp(argv[1], "--list")) {
        puts("integration.module_search");
        result = 0;
    } else if (argc == 6 || argc == 7) {
        fs_error_t e = {0};
        char *exe = absolute_fs_path(argv[2], &e);
        clear_fs_error(&e);
        char *vectors = absolute_fs_path(argv[5], &e);
        clear_fs_error(&e);
        if (exe && vectors) {
            configuration_t c = {.exe = exe, .vectors = vectors};
            scenario_definition_t def = {"integration.module_search", 180000, body, &c, NULL};
            scenario_options_t o = {.build_directory = argv[3], .data_directory = argv[4]};
            const char *selection[] = {argv[1], argc == 7 ? argv[6] : NULL};
            result = scenario_registry_main(&def, 1, argc == 7 ? 2 : 1, selection, &o);
        }
        FREE(exe);
        FREE(vectors);
    } else
        fputs("usage: module_search_scenarios ID HOST BUILD DATA VECTORS [--keep-work]\n", stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
