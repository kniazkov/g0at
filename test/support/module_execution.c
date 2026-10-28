/** @file module_execution.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Scenario-owned equivalent of module_execution_support.py.
 */
#include "test/support/module_execution.h"

#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "yyjson.h"

#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <windows.h>
#else
extern char **environ;
#endif

struct module_execution_suite_t {
    scenario_run_t *run;
    const char *exe, *root, *unrelated;
    size_t count;
};

struct module_execution_project_t {
    module_execution_suite_t *suite;
    const char *root, *modules;
    const char **environment;
};

static bool release(void *p) {
    FREE(p);
    return true;
}

static bool release_doc(void *p) {
    yyjson_mut_doc_free(p);
    return true;
}

static bool release_read_doc(void *p) {
    yyjson_doc_free(p);
    return true;
}

static bool release_json(void *p) {
    free(p);
    return true;
}

static void *owned(scenario_run_t *run, size_t size) {
    void *p = CALLOC(size);
    scenario_defer(run, release, p);
    return p;
}

static const char *copy(scenario_run_t *run, const char *text) {
    char *p = owned(run, strlen(text) + 1);
    strcpy(p, text);
    return p;
}

static const char *join(scenario_run_t *run, const char *left, const char *right) {
    fs_error_t error = {0};
    char *p = join_fs_path(left, right, &error);
    clear_fs_error(&error);
    scenario_require(run, p != NULL, "cannot join fixture path");
    scenario_defer(run, release, p);
    return p;
}

static void directory(scenario_run_t *run, const char *path) {
    fs_error_t error = {0};
    bool ok = create_fs_directory(path, &error);
    if (!ok && error.status == FS_EXISTS) {
        fs_info_t info;
        clear_fs_error(&error);
        ok = inspect_fs_path(path, false, &info, &error) && info.kind == FS_DIRECTORY;
    }
    clear_fs_error(&error);
    scenario_require(run, ok, "cannot create fixture directory");
}

static bool relative(const char *path) {
    if (!path || !*path)
        return false;
    const char *start = path;
    for (const char *p = path;; ++p) {
        if (*p == '\\' || *p == ':')
            return false;
        if (*p == '/' || !*p) {
            size_t n = (size_t)(p - start);
            if (!n || (n == 1 && *start == '.') || (n == 2 && start[0] == '.' && start[1] == '.'))
                return false;
            if (!*p)
                return true;
            start = p + 1;
        }
    }
}

static const char *prepare(scenario_run_t *run, const char *root, const char *path) {
    scenario_require(run, relative(path), "invalid relative fixture path");
    char *part = (char *)copy(run, path);
    for (char *p = part; *p; ++p) {
        if (*p == '/') {
            *p = 0;
            directory(run, join(run, root, part));
            *p = '/';
        }
    }
    return join(run, root, path);
}

static const char *put(scenario_run_t *run, const char *root, const char *path, const char *text) {
    const char *full = prepare(run, root, path);
    FILE *file = FOPEN(full, "wb");
    scenario_require(run, file != NULL, "cannot open fixture file");
    size_t n = strlen(text);
    bool ok = fwrite(text, 1, n, file) == n;
    if (fclose(file))
        ok = false;
    scenario_require(run, ok, "cannot write fixture file");
    return full;
}

module_execution_suite_t *module_execution_suite(scenario_run_t *run, const char *executable) {
    module_execution_suite_t *suite = owned(run, sizeof(*suite));
    suite->run = run;
    fs_error_t error = {0};
    char *exe = absolute_fs_path(executable, &error);
    clear_fs_error(&error);
    scenario_require(run, exe != NULL, "cannot resolve module executable");
    scenario_defer(run, release, exe);
    suite->exe = exe;
    suite->root = scenario_work_directory(scenario_run_context(run));
    suite->unrelated = join(run, suite->root, "unrelated");
    directory(run, suite->unrelated);
    return suite;
}
#ifdef _WIN32
static bool release_environment_snapshot(void *opaque) {
    char **snapshot = opaque;
    for (size_t i = 0; snapshot[i]; ++i)
        FREE(snapshot[i]);
    FREE(snapshot);
    return true;
}
#endif
static bool module_variable(const char *text) {
#ifdef _WIN32
    return _strnicmp(text, "GOAT_MODULE_PATH=", 17) == 0;
#else
    return strncmp(text, "GOAT_MODULE_PATH=", 17) == 0;
#endif
}

static const char **environment(scenario_run_t *run, const char *modules) {
    const char **result;
    size_t count = 0, i = 0;
#ifdef _WIN32
    wchar_t *block = GetEnvironmentStringsW();
    if (!block)
        scenario_require(run, false, "cannot read inherited environment");
    for (wchar_t *p = block; *p; p += wcslen(p) + 1)
        if (*p != L'=')
            count++;
    /* Release the native block before any fail-fast API boundary. */
    char **snapshot = CALLOC((count + 1) * sizeof(*snapshot));
    bool ok = true;
    for (wchar_t *p = block; *p; p += wcslen(p) + 1) {
        if (*p == L'=')
            continue;
        int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, p, -1, NULL, 0, NULL, NULL);
        if (!n) {
            ok = false;
            break;
        }
        snapshot[i] = ALLOC((size_t)n);
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, p, -1, snapshot[i], n, NULL, NULL))
            ok = false;
        i++;
    }
    FreeEnvironmentStringsW(block);
    scenario_defer(run, release_environment_snapshot, snapshot);
    scenario_require(run, ok, "invalid inherited Unicode environment");
    result = owned(run, (count + 2) * sizeof(*result));
    size_t out = 0;
    for (size_t j = 0; j < i; ++j) {
        if (!module_variable(snapshot[j]))
            result[out++] = copy(run, snapshot[j]);
    }
    i = out;
#else
    for (char **p = environ; p && *p; ++p)
        count++;
    result = owned(run, (count + 2) * sizeof(*result));
    for (char **p = environ; p && *p; ++p)
        if (!module_variable(*p))
            result[i++] = copy(run, *p);
#endif
    char *entry = owned(run, strlen(modules) + 18);
    strcpy(entry, "GOAT_MODULE_PATH=");
    strcat(entry, modules);
    result[i] = entry;
    return result;
}

module_execution_project_t *module_execution_project(module_execution_suite_t *suite,
                                                     const char *name) {
    scenario_run_t *run = suite->run;
    module_execution_project_t *project = owned(run, sizeof(*project));
    project->suite = suite;
    project->root = prepare(run, suite->root, name);
    directory(run, project->root);
    project->modules = join(run, project->root, "modules");
    directory(run, project->modules);
    project->environment = environment(run, project->modules);
    return project;
}

const char *module_execution_root(const module_execution_project_t *p) {
    return p->root;
}

const char *module_execution_modules(const module_execution_project_t *p) {
    return p->modules;
}

size_t module_execution_count(const module_execution_suite_t *s) {
    return s->count;
}

const char *module_execution_executable(const module_execution_suite_t *s) {
    return s->exe;
}

const char *module_execution_unrelated(const module_execution_suite_t *s) {
    return s->unrelated;
}

const char *const *module_execution_environment(const module_execution_project_t *p) {
    return p->environment;
}

const char *
module_execution_put(module_execution_project_t *p, const char *path, const char *text) {
    return put(p->suite->run, p->root, path, text);
}

/* Python stdout.decode('utf-8') accepts NUL, but rejects malformed sequences. */
static bool utf8(const unsigned char *p, size_t n) {
    for (size_t i = 0; i < n;) {
        unsigned c = p[i++], value, remaining, minimum;
        if (c < 128)
            continue;
        if (c >= 0xc2 && c <= 0xdf) {
            value = c & 31;
            remaining = 1;
            minimum = 128;
        } else if (c >= 0xe0 && c <= 0xef) {
            value = c & 15;
            remaining = 2;
            minimum = 2048;
        } else if (c >= 0xf0 && c <= 0xf4) {
            value = c & 7;
            remaining = 3;
            minimum = 65536;
        } else
            return false;
        if (remaining > n - i)
            return false;
        while (remaining--) {
            c = p[i++];
            if ((c & 0xc0) != 0x80)
                return false;
            value = (value << 6) | (c & 63);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
            return false;
    }
    return true;
}

scenario_command_result_t module_execution_invoke(module_execution_project_t *p,
                                                  const char *mode,
                                                  const char *path,
                                                  const char *output,
                                                  const void *expected,
                                                  size_t expected_size,
                                                  bool fail) {
    module_execution_suite_t *s = p->suite;
    const char *args[] = {s->exe, mode, path, output, NULL};
    child_options_t options = {0};
    options.executable = s->exe;
    options.arguments = args;
    options.working_directory = s->unrelated;
    options.environment = p->environment;
    s->count++;
    scenario_command_result_t result = scenario_command_capture(s->run, &options);
    scenario_require(s->run,
                     fail ? result.exit_code != 0 : result.exit_code == 0,
                     "unexpected module command exit code");
    scenario_require(s->run, utf8(result.out, result.out_size), "module stdout is not UTF-8");
    unsigned char *normalized = owned(s->run, result.out_size + 1);
    const unsigned char *raw = result.out;
    size_t n = 0;
    for (size_t i = 0; i < result.out_size; ++i) {
        if (raw[i] == '\r' && i + 1 < result.out_size && raw[i + 1] == '\n')
            i++;
        normalized[n++] = raw[i];
    }
    scenario_require_bytes(s->run, "module stdout", expected, expected_size, normalized, n, false);
    if (fail)
        scenario_require(s->run, result.err_size != 0, "missing module diagnostic");
    return result;
}

const char *module_execution_compile(module_execution_project_t *p,
                                     const char *name,
                                     const char *code,
                                     const char *version,
                                     const module_execution_file_t *files,
                                     size_t file_count,
                                     const char *dependencies_json,
                                     bool fail) {
    scenario_run_t *run = p->suite->run;
    if (!version)
        version = "1";
    scenario_require(run,
                     relative(name) && !strchr(name, '/') && relative(version)
                         && !strchr(version, '/'),
                     "invalid module name/version path");
    char *relative_dir = owned(run, strlen(name) + strlen(version) + 10);
    sprintf(relative_dir, "sources/%s-%s", name, version);
    const char *dir = prepare(run, p->root, relative_dir);
    directory(run, dir);
    put(run, dir, "main.goat", code);
    for (size_t i = 0; i < file_count; ++i)
        put(run, dir, files[i].name, files[i].text);
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    scenario_require(run, doc != NULL, "cannot allocate module descriptor");
    scenario_defer(run, release_doc, doc);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    bool ok = root && yyjson_mut_obj_add_uint(doc, root, "formatVersion", 1)
              && yyjson_mut_obj_add_strcpy(doc, root, "name", name)
              && yyjson_mut_obj_add_strcpy(doc, root, "version", version)
              && yyjson_mut_obj_add_str(doc, root, "main", "main.goat");
    if (dependencies_json) {
        yyjson_doc *dependencies = yyjson_read(dependencies_json, strlen(dependencies_json), 0);
        scenario_require(run, dependencies != NULL, "invalid dependencies JSON");
        scenario_defer(run, release_read_doc, dependencies);
        yyjson_mut_val *value = yyjson_val_mut_copy(doc, yyjson_doc_get_root(dependencies));
        ok = ok && value && yyjson_mut_obj_add_val(doc, root, "dependencies", value);
    }
    scenario_require(run, ok, "cannot construct module descriptor");
    char *json = yyjson_mut_write(doc, 0, NULL);
    scenario_require(run, json != NULL, "cannot serialize module descriptor");
    scenario_defer(run, release_json, json);
    const char *source = put(run, dir, "module.gmod", json);
    const char *artifact;
    if (!strcmp(name, "app"))
        artifact = join(run, p->root, "app.gbin");
    else {
        char *path = owned(run, strlen(name) + strlen(version) + 8);
        sprintf(path, "%s/%s.gbin", name, version);
        artifact = prepare(run, p->modules, path);
    }
    module_execution_invoke(p, "compile", source, artifact, "", 0, fail);
    if (fail) {
        fs_error_t error = {0};
        fs_info_t info;
        bool exists = inspect_fs_path(artifact, false, &info, &error);
        bool absent = !exists && error.status == FS_NOT_FOUND;
        clear_fs_error(&error);
        scenario_require(run, absent, "failed compile left an artifact");
    }
    return artifact;
}

const char *module_execution_run(module_execution_project_t *p,
                                 const char *code,
                                 const void *expected,
                                 size_t expected_size) {
    const char *artifact = module_execution_compile(p, "app", code, NULL, NULL, 0, NULL, false);
    module_execution_invoke(p, "binary", artifact, NULL, expected, expected_size, false);
    return artifact;
}
