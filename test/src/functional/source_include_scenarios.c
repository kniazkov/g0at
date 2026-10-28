/** @file source_include_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Literal include execution, saved images and source-map regressions.
 */
#ifndef _WIN32
#    define _POSIX_C_SOURCE 200809L
#endif
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/format_json.h"
#include "test/support/format_zip.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"

#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <windows.h>
#else
#    include <unistd.h>
#endif
#define N(a) (sizeof(a) / sizeof((a)[0]))

typedef struct {
    const char *name, *source;
} source_t;

typedef struct {
    const char *name;
    source_t files[3];
    size_t count;
    const char *expected;
    bool archive, descriptor;
} fixture_t;

static const fixture_t fixtures[] = {
    {"nested",
     {{"main.goat", "println(\"main\"); const a = include \"parts/a.goat\"; println(a.value);"},
      {"parts/a.goat",
       "println(\"a\"); const b = include \"deep/b.goat\"; public value = b.value + 1;"},
      {"parts/deep/b.goat", "println(\"b\"); public value = 41;"}},
     3,
     "main\na\nb\n42\n",
     true,
     false},
    {"repeat",
     {{"main.goat",
       "const a = include \"a.goat\"; const b = include \"./a.goat\"; println(a == b); a.count += "
       "1; println(b.count); println(a.next()); println(b.count);"},
      {"a.goat",
       "println(\"once\"); public var count = 0; public next = func() { count += 1; return count; "
       "};"}},
     2,
     "once\ntrue\n1\n2\n2\n",
     true,
     true},
    {"lexical",
     {{"main.goat",
       "const secret = 81; const load = func() { var local = 91; return include \"a.goat\"; }; "
       "const a = load(); println(a.secretValue); println(a.localValue); println(a.answer());"},
      {"a.goat",
       "public secretValue = secret; public localValue = local; const hidden = 7; public answer = "
       "func() { return hidden; };"}},
     2,
     "null\nnull\n7\n",
     true,
     false},
    {"private",
     {{"main.goat",
       "const a = include \"a.goat\"; try { println(a.hidden); } catch(e) { println(\"denied\"); } "
       "println(a.visible);"},
      {"a.goat", "const hidden = 1; public visible = 2;"}},
     2,
     "denied\n2\n",
     false,
     false},
    {"lazy",
     {{"main.goat",
       "const deferred = func() { return include \"a.goat\"; }; if(false) { include "
       "\"never.goat\"; } println(\"before\"); println(deferred().value); "
       "println(deferred().value);"},
      {"a.goat", "println(\"loaded\"); public value = 9;"},
      {"never.goat", "println(\"MUST NOT RUN\");"}},
     3,
     "before\nloaded\n9\n9\n",
     true,
     false},
    {"expression",
     {{"main.goat",
       "println(10 + (include \"a.goat\").value); println((include \"a.goat\").twice(3));"},
      {"a.goat", "public value = 4; public twice = func(x) { return 2 * x; };"}},
     2,
     "14\n6\n",
     true,
     false},
    {"arguments",
     {{"main.goat",
       "const combine = func(a, b, c) { return 100*a + 10*b + c; }; println(combine(1, (include "
       "\"a.goat\").value, 3)); println(combine((include \"b.goat\").value, 5, (include "
       "\"a.goat\").value));"},
      {"a.goat", "public value = 4;"},
      {"b.goat", "public value = 2;"}},
     3,
     "143\n254\n",
     true,
     false},
    {"closure-include",
     {{"main.goat",
       "const a = include \"parts/a.goat\"; const closure = a.make(5); println(closure(7)); "
       "println(closure(8));"},
      {"parts/a.goat",
       "public make = func(x) { return func(y) { const b = include \"sub/b.goat\"; return x + y + "
       "b.value; }; };"},
      {"parts/sub/b.goat", "println(\"closure helper once\"); public value = 30;"}},
     3,
     "closure helper once\n42\n43\n",
     true,
     false},
    {"failed-callback",
     {{"main.goat",
       "const a = include \"a.goat\"; println(a.call(func() { return 3; })); try { a.call(func() { "
       "throw \"callback\"; }); } catch(e) { println(e); } println(a.call(func() { return 4; }));"},
      {"a.goat", "public call = func(f) { return f() + 10; };"}},
     2,
     "13\ncallback\n14\n",
     true,
     false},
    {"same-content",
     {{"main.goat",
       "const a = include \"a.goat\"; const b = include \"b.goat\"; println(a == b); a.count = 4; "
       "println(b.count);"},
      {"a.goat", "println(\"init\"); public var count = 0;"},
      {"b.goat", "println(\"init\"); public var count = 0;"}},
     3,
     "init\ninit\nfalse\n0\n",
     false,
     false},
    {"cycle-ready",
     {{"main.goat", "public ready = 12; const a = include \"a.goat\"; println(a.value);"},
      {"a.goat", "const main = include \"main.goat\"; public value = main.ready + 1;"}},
     2,
     "13\n",
     true,
     false},
    {"cycle-early-caught",
     {{"main.goat", "const a = include \"a.goat\"; public later = 12; println(a.value);"},
      {"a.goat",
       "const main = include \"main.goat\"; try { println(main.later); } catch(e) { "
       "println(\"early\"); } public value = 4;"}},
     2,
     "early\n4\n",
     false,
     false},
    {"failed-repeated",
     {{"main.goat",
       "var first = null; try { include \"bad.goat\"; } catch(e) { first = e; println(e.message); "
       "} try { include \"bad.goat\"; } catch(e) { println(first == e); } println(\"done\");"},
      {"bad.goat", "println(\"one effect\"); throw { public message = \"failure\"; };"}},
     2,
     "one effect\nfailure\ntrue\ndone\n",
     true,
     false},
    {"finally",
     {{"main.goat",
       "try { include \"bad.goat\"; } catch(e) { println(e); } finally { println(\"outer "
       "cleanup\"); } println(\"after\");"},
      {"bad.goat", "try { throw \"saved\"; } finally { println(\"inner cleanup\"); }"}},
     2,
     "inner cleanup\nsaved\nouter cleanup\nafter\n",
     true,
     false},
    {"throwing-function",
     {{"main.goat",
       "const a = include \"a.goat\"; try { a.fail(); } catch(e) { println(e); } println((include "
       "\"a.goat\") == a);"},
      {"a.goat", "println(\"once\"); public fail = func() { throw \"function error\"; };"}},
     2,
     "once\nfunction error\ntrue\n",
     true,
     false},
    {"unicode",
     {{"main.goat", "println((include \"директория/😀.goat\").value);"},
      {"директория/😀.goat", "public value = \"готово\";"}},
     2,
     "готово\n",
     true,
     false},
    {"empty",
     {{"main.goat", "const x = include \"empty.goat\"; println(x == (include \"empty.goat\"));"},
      {"empty.goat", ""}},
     2,
     "true\n",
     false,
     false},
    {"outside",
     {{"main.goat",
       "const a = include \"../external/a/math.goat\"; const b = include "
       "\"../external/b/math.goat\"; println(a.value); println(b.value); println(a == b);"}},
     1,
     "1\n2\nfalse\n",
     true,
     true},
};

typedef struct {
    const char *exe;
    size_t index;
} configuration_t;

typedef struct {
    const unsigned char *data;
    size_t size;
} bytes_t;

typedef struct {
    yyjson_val *json;
    bytes_t wire;
} metadata_t;

static bool free_doc(void *p) {
    yyjson_doc_free(p);
    return true;
}

static bool free_heap(void *p) {
    free(p);
    return true;
}

static const char *path(scenario_run_t *r, const char *name) {
    return language_join(r, scenario_work_directory(scenario_run_context(r)), name);
}

static const char *put(scenario_run_t *r, const char *p, const char *s) {
    char *parent = language_owned(r, ALLOC(strlen(p) + 1));
    strcpy(parent, p);
    for (char *q = parent + 1; *q; q++)
        if (*q == '/') {
            *q = 0;
            fs_error_t e = {0};
            fs_info_t info;
            bool exists = inspect_fs_path(parent, true, &info, &e);
            clear_fs_error(&e);
            if (!exists)
                language_directory(r, parent);
            else
                scenario_require(r, info.kind == FS_DIRECTORY, "fixture parent directory");
            *q = '/';
        }
    language_write(r, p, s, strlen(s));
    return p;
}

static void remove_tree(scenario_run_t *r, const char *p) {
    fs_error_t e = {0};
    bool ok = remove_fs_tree(p, &e);
    clear_fs_error(&e);
    scenario_require(r, ok, "remove source tree before binary execution");
}

static void invoke(scenario_run_t *r,
                   configuration_t *c,
                   const char *mode,
                   const char *p,
                   const char *expected,
                   const char *output,
                   const char *cwd,
                   bool fail) {
    const char *args[] = {c->exe, mode, p, output, NULL};
    child_options_t options = {
        .executable = c->exe,
        .arguments = args,
        .working_directory = cwd,
        .environment =
            language_environment_override(r, "GOAT_MODULE_PATH", "/never/search/includes")};
    scenario_command_result_t result = scenario_command_capture(r, &options);
    scenario_require(r,
                     fail ? result.exit_code != 0 : result.exit_code == 0,
                     "include command status");
    if (expected)
        scenario_require_bytes(r,
                               "include stdout",
                               expected,
                               strlen(expected),
                               result.out,
                               result.out_size,
                               true);
    if (fail)
        scenario_require(r, result.err_size != 0, "failure must have diagnostic");
}

static uint32_t word(scenario_run_t *r, bytes_t b, size_t offset) {
    scenario_require(r, offset <= b.size && b.size - offset >= 4, "bounded bytecode field");
    return (uint32_t)b.data[offset] | (uint32_t)b.data[offset + 1] << 8
           | (uint32_t)b.data[offset + 2] << 16 | (uint32_t)b.data[offset + 3] << 24;
}

static uint32_t header(scenario_run_t *r, bytes_t b, size_t index) {
    return word(r, b, 4 + 4 * index);
}

static yyjson_val *get(yyjson_val *v, const char *key) {
    return yyjson_obj_get(v, key);
}

static uint64_t number(scenario_run_t *r, yyjson_val *v) {
    scenario_require(r, yyjson_is_uint(v), "metadata unsigned integer");
    return yyjson_get_uint(v);
}

static bool contains(bytes_t b, const char *s) {
    size_t n = strlen(s);
    for (size_t i = 0; n <= b.size && i <= b.size - n; i++)
        if (!memcmp(b.data + i, s, n))
            return true;
    return false;
}

static bytes_t entry(scenario_run_t *r, bytes_t zip, const char *name) {
    char error[256];
    format_zip_info_t info;
    scenario_require(r,
                     format_zip_inspect(zip.data, zip.size, SIZE_MAX, &info, error, sizeof(error)),
                     "archive directory");
    size_t count = info.count;
    for (size_t i = 0; i < count; i++) {
        scenario_require(r,
                         format_zip_inspect(zip.data, zip.size, i, &info, error, sizeof(error)),
                         "archive entry");
        if (info.name_size == strlen(name) && !memcmp(info.name, name, info.name_size)) {
            size_t size;
            unsigned char *data = format_zip_extract(zip.data,
                                                     zip.size,
                                                     i,
                                                     16 * 1024 * 1024,
                                                     &size,
                                                     error,
                                                     sizeof(error));
            scenario_require(r, data != NULL, "extract saved image");
            scenario_defer(r, free_heap, data);
            return (bytes_t){data, size};
        }
    }
    scenario_require(r, false, "required archive entry");
    return (bytes_t){0};
}

static metadata_t metadata(scenario_run_t *r, const char *artifact) {
    size_t n;
    const char *raw = language_read(r, artifact, &n);
    bytes_t zip = {(const unsigned char *)raw, n};
    bytes_t json = entry(r, zip, "module.json"), wire = entry(r, zip, "bytecode.bin");
    const char *root = scenario_work_directory(scenario_run_context(r));
    scenario_require(r,
                     !contains(json, root) && !contains(wire, root),
                     "no absolute build path leak");
    char *normalized = language_owned(r, ALLOC(strlen(root) + 1));
    strcpy(normalized, root);
    for (char *p = normalized; *p; p++)
        if (*p == '\\')
            *p = '/';
    scenario_require(r,
                     !contains(json, normalized) && !contains(wire, normalized),
                     "no normalized absolute path leak");
    yyjson_doc *doc = yyjson_read((const char *)json.data, json.size, 0);
    scenario_require(r, doc != NULL, "saved metadata JSON");
    scenario_defer(r, free_doc, doc);
    yyjson_val *v = yyjson_doc_get_root(doc), *files = get(v, "files"),
               *functions = get(v, "functions");
    size_t count = yyjson_arr_size(files);
    scenario_require(r,
                     yyjson_is_arr(files) && number(r, get(v, "main")) < count,
                     "main file index");
    scenario_require(r,
                     wire.size >= 64 && !memcmp(wire.data, "GOATBC01", 8),
                     "bytecode header magic");
    scenario_require(r, count == header(r, wire, 6), "metadata and bytecode file count");
    for (size_t i = 0; i < count; i++) {
        yyjson_val *f = yyjson_arr_get(files, i), *name = get(f, "name"),
                   *external = get(f, "external");
        uint64_t e = number(r, get(f, "entry"));
        const char *s = yyjson_get_str(name);
        scenario_require(r,
                         yyjson_obj_size(f) == 3 && s && yyjson_is_bool(external)
                             && e <= UINT32_MAX,
                         "file metadata fields");
        scenario_require(r,
                         *s != '/' && *s != '\\' && !(s[0] && s[1] == ':'),
                         "logical relative file name");
        if (yyjson_get_bool(external))
            scenario_require(r, !strchr(s, '/') && !strchr(s, '\\'), "external basename only");
        size_t at = header(r, wire, 11) + 16 * i;
        uint32_t first = word(r, wire, at), length = word(r, wire, at + 4);
        scenario_require(r,
                         e == word(r, wire, at + 8) && first <= e && e < (uint64_t)first + length,
                         "file entry range");
    }
    scenario_require(r, yyjson_arr_size(functions) == header(r, wire, 7), "function count");
    for (size_t i = 0; i < yyjson_arr_size(functions); i++) {
        yyjson_val *f = yyjson_arr_get(functions, i);
        const char *keys[] = {"functionId", "file", "instruction"};
        for (size_t j = 0; j < N(keys); j++)
            scenario_require(r,
                             number(r, get(f, keys[j]))
                                 == word(r, wire, header(r, wire, 12) + 32 * i + 4 * j),
                             "function wire mapping");
        scenario_require(r, number(r, get(f, "file")) < count, "function source file");
    }
    return (metadata_t){v, wire};
}

static void names(scenario_run_t *r, metadata_t m, const char *const *expected, size_t n) {
    yyjson_val *files = get(m.json, "files");
    scenario_require(r, yyjson_arr_size(files) == n, "file name count");
    for (size_t i = 0; i < n; i++)
        scenario_require(
            r,
            !strcmp(yyjson_get_str(get(yyjson_arr_get(files, i), "name")), expected[i]),
            "logical file name");
}

static bool link_file(scenario_run_t *r,
                      const char *target,
                      const char *alias,
                      bool symbolic,
                      bool privilege_skip) {
#ifdef _WIN32
    int nt = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, target, -1, NULL, 0),
        na = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, alias, -1, NULL, 0);
    scenario_require(r, nt > 0 && na > 0, "UTF-8 link paths");
    wchar_t *t = language_owned(r, ALLOC((size_t)nt * sizeof(*t))),
            *a = language_owned(r, ALLOC((size_t)na * sizeof(*a)));
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, target, -1, t, nt);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, alias, -1, a, na);
    /* Match Python: permit developer-mode unprivileged symlinks where supported. */
    bool ok = symbolic ? CreateSymbolicLinkW(a, t, 2) != 0 : CreateHardLinkW(a, t, NULL) != 0;
    if (!ok && symbolic && GetLastError() == ERROR_INVALID_PARAMETER)
        ok = CreateSymbolicLinkW(a, t, 0) != 0;
    if (!ok && symbolic && privilege_skip && GetLastError() == ERROR_PRIVILEGE_NOT_HELD) {
        puts("SKIP symlink fixtures: Windows privilege not held");
        return false;
    }
#else
    (void)privilege_skip;
    bool ok = (symbolic ? symlink(target, alias) : link(target, alias)) == 0;
#endif
    scenario_require(r, ok, "create link fixture");
    return true;
}

static void ordinary(scenario_run_t *r, configuration_t *c, const fixture_t *f) {
    const char *directory = path(r, f->name), *unrelated = path(r, "unrelated");
    language_directory(r, unrelated);
    if (!strcmp(f->name, "outside")) {
        put(r, path(r, "external/a/math.goat"), "public value = 1;");
        put(r, path(r, "external/b/math.goat"), "public value = 2;");
    }
    for (size_t i = 0; i < f->count; i++)
        put(r, language_join(r, directory, f->files[i].name), f->files[i].source);
    const char *source = language_join(r, directory, "main.goat");
    if (f->descriptor)
        source = put(r,
                     language_join(r, directory, "app.gmod"),
                     "{\"formatVersion\":1,\"name\":\"include-tests\",\"version\":\"1.0.0\","
                     "\"main\":\"main.goat\"}");
    invoke(r, c, "run", source, f->expected, NULL, unrelated, false);
    if (!f->archive)
        return;
    const char *artifact = path(r, "image.gbin");
    invoke(r, c, "compile", source, "", artifact, unrelated, false);
    metadata_t m = metadata(r, artifact);
    yyjson_val *files = get(m.json, "files"), *functions = get(m.json, "functions");
    if (!strcmp(f->name, "nested")) {
        const char *expected[] = {"main.goat", "parts/a.goat", "parts/deep/b.goat"};
        names(r, m, expected, N(expected));
        for (size_t i = 0; i < 3; i++)
            scenario_require(r,
                             !yyjson_get_bool(get(yyjson_arr_get(files, i), "external")),
                             "nested local file");
    }
    if (!strcmp(f->name, "repeat"))
        scenario_require(r,
                         yyjson_arr_size(functions) == 1
                             && number(r, get(yyjson_arr_get(functions, 0), "file")) == 1,
                         "repeat function source");
    if (!strcmp(f->name, "lexical")) {
        unsigned bits = 0;
        for (size_t i = 0; i < yyjson_arr_size(functions); i++) {
            uint64_t file = number(r, get(yyjson_arr_get(functions, i), "file"));
            scenario_require(r, file < 2, "lexical source file");
            bits |= 1u << file;
        }
        scenario_require(r, bits == 3, "lexical functions from both sources");
    }
    if (!strcmp(f->name, "outside")) {
        const char *expected[] = {"main.goat", "math.goat", "math.goat"};
        names(r, m, expected, N(expected));
        for (size_t i = 0; i < 3; i++)
            scenario_require(r,
                             yyjson_get_bool(get(yyjson_arr_get(files, i), "external")) == (i != 0),
                             "external source flags");
    }
    remove_tree(r, directory);
    invoke(r, c, "binary", artifact, f->expected, NULL, unrelated, false);
}

static void invalid(scenario_run_t *r, configuration_t *c) {
    const char *statements[] = {"include path;",
                                "include (\"a.goat\");",
                                "include \"a\" + \".goat\";",
                                "include \"\";",
                                "include \"a\\0.goat\";",
                                "include 3;",
                                "include;",
                                "if(false) { include path; }",
                                "const f = func() { include path; };",
                                "if(false) { include \"missing.goat\"; }"};
    const char *source = path(r, "main.goat");
    put(r, path(r, "a.goat"), "");
    for (size_t i = 0; i < N(statements); i++) {
        char text[256];
        snprintf(text, sizeof(text), "println(\"MUST NOT RUN\"); %s", statements[i]);
        put(r, source, text);
        invoke(r, c, "run", source, "", NULL, NULL, true);
    }
    put(r, source, "println(\"MUST NOT RUN\"); if(false) { include \"a.goat\"; }");
    put(r, path(r, "a.goat"), "var broken = ;");
    invoke(r, c, "run", source, "", NULL, NULL, true);
}

static void hardlinks(scenario_run_t *r, configuration_t *c) {
    const char *a = put(r,
                        path(r, "hardlinks/a/math.goat"),
                        "println(\"math init\"); const h = include \"helper.goat\"; public value = "
                        "h.value; public var count = 0;");
    language_directory(r, path(r, "hardlinks/b"));
    link_file(r, a, path(r, "hardlinks/b/math.goat"), false, false);
    put(r, path(r, "hardlinks/a/helper.goat"), "public value = \"a\";");
    put(r, path(r, "hardlinks/b/helper.goat"), "public value = \"b\";");
    const char *main =
        put(r,
            path(r, "hardlinks/main.goat"),
            "const a = include \"a/math.goat\"; const b = include \"b/math.goat\"; println(a == "
            "b); println(a.value); println(b.value); a.count = 7; println(b.count);");
    const char *expected = "math init\nmath init\nfalse\na\nb\n0\n",
               *artifact = path(r, "hardlinks.gbin"), *cwd = path(r, "unrelated");
    language_directory(r, cwd);
    invoke(r, c, "run", main, expected, NULL, cwd, false);
    invoke(r, c, "compile", main, "", artifact, NULL, false);
    metadata_t m = metadata(r, artifact);
    const char *labels[] = {"main.goat",
                            "a/math.goat",
                            "a/helper.goat",
                            "b/math.goat",
                            "b/helper.goat"};
    names(r, m, labels, N(labels));
    remove_tree(r, path(r, "hardlinks"));
    invoke(r, c, "binary", artifact, expected, NULL, NULL, false);
}

static void symlinks(scenario_run_t *r, configuration_t *c) {
    const char *main = put(r,
                           path(r, "real/main.goat"),
                           "const a = include \"math.goat\"; const b = include \"again.goat\"; "
                           "println(a == b); println(a.value);");
    const char *math =
        put(r,
            path(r, "real/math.goat"),
            "println(\"real once\"); public value = (include \"helper.goat\").value;");
    put(r, path(r, "real/helper.goat"), "public value = \"real helper\";");
    put(r, path(r, "aliases/helper.goat"), "public value = \"WRONG helper\";");
    put(r, path(r, "aliases/math.goat"), "println(\"WRONG math\");");
    if (!link_file(r, math, path(r, "real/again.goat"), true, true)
        || !link_file(r, main, path(r, "aliases/main.goat"), true, true)
        || !link_file(r, math, path(r, "aliases/linked-math.goat"), true, true))
        return;
    const char *expected = "real once\ntrue\nreal helper\n", *cwd = path(r, "unrelated");
    language_directory(r, cwd);
    invoke(r, c, "run", path(r, "aliases/main.goat"), expected, NULL, cwd, false);
    const char *via = put(r,
                          path(r, "aliases/via.goat"),
                          "const a = include \"linked-math.goat\"; const b = include "
                          "\"../real/math.goat\"; println(a == b); println(a.value);");
    invoke(r, c, "run", via, expected, NULL, cwd, false);
    const char *artifact = path(r, "symlinks.gbin");
    invoke(r, c, "compile", path(r, "aliases/main.goat"), "", artifact, NULL, false);
    metadata_t m = metadata(r, artifact);
    scenario_require(r, yyjson_arr_size(get(m.json, "files")) == 3, "symlink canonical file count");
    remove_tree(r, path(r, "real"));
    remove_tree(r, path(r, "aliases"));
    invoke(r, c, "binary", artifact, expected, NULL, cwd, false);
}

static void label(scenario_run_t *r, bytes_t b, size_t record, const char *expected) {
    uint32_t index = word(r, b, record + 24);
    if (!expected) {
        scenario_require(r, index == UINT32_MAX, "anonymous function label");
        return;
    }
    scenario_require(r, index < header(r, b, 5), "function label data index");
    size_t at = header(r, b, 10) + 16 * (size_t)index;
    uint32_t offset = word(r, b, at), size = word(r, b, at + 4), kind = word(r, b, at + 8),
             count = word(r, b, at + 12);
    scenario_require(r,
                     kind == 1 && size == 4 * ((uint64_t)count + 1) && count == strlen(expected),
                     "UTF-32 function label record");
    for (size_t i = 0; i < count; i++)
        scenario_require(r,
                         word(r, b, header(r, b, 14) + (size_t)offset + 4 * i)
                             == (unsigned char)expected[i],
                         "function label text");
}

static void diagnostic(scenario_run_t *r, configuration_t *c) {
    const char *main_text =
        "const outer = func() {\n  const inner = func() {};\n  return func() { return 1; "
        "};\n};\nconst alias = outer;\nconst other = include \"other.goat\";\n";
    char other[16384] = "public outer = func() {\n  throw \"diagnostic-payload\";\n};\n";
    size_t used = strlen(other);
    for (unsigned i = 0; i < 160; i++)
        used += (size_t)snprintf(other + used,
                                 sizeof(other) - used,
                                 "const f%u = func() { return %u; };\n",
                                 i,
                                 i);
    const char *main = put(r, path(r, "diagnostic-mapping/main.goat"), main_text);
    put(r, path(r, "diagnostic-mapping/other.goat"), other);
    const char *artifact = path(r, "diagnostic-mapping.gbin");
    invoke(r, c, "compile", main, "", artifact, NULL, false);
    metadata_t m = metadata(r, artifact);
    bytes_t b = m.wire;
    size_t base = header(r, b, 12);
    scenario_require(r, header(r, b, 7) == 164, "diagnostic function count");
    const char *first[] = {"outer", "inner", NULL, "outer"};
    for (size_t i = 0; i < 164; i++) {
        size_t at = base + 32 * i;
        scenario_require(r,
                         word(r, b, at) == i + 1 && word(r, b, at + 4) == (i < 3 ? 0 : 1),
                         "source ordered function IDs");
        if (i < 4)
            label(r, b, at, first[i]);
        else {
            char name[32];
            snprintf(name, sizeof(name), "f%zu", i - 4);
            label(r, b, at, name);
        }
    }
    uint32_t empty = word(r, b, base + 32 + 12);
    bool found = false, thrown = false;
    for (size_t i = 0; i < header(r, b, 8); i++) {
        size_t at = header(r, b, 13) + 24 * i;
        uint32_t start = word(r, b, at), length = word(r, b, at + 4), file = word(r, b, at + 8),
                 row = word(r, b, at + 12), column = word(r, b, at + 16),
                 function = word(r, b, at + 20);
        if (!found && start <= empty && empty < (uint64_t)start + length) {
            scenario_require(r,
                             file == 0 && row == 2 && column == 17 && function == 2,
                             "empty body source location");
            found = true;
        }
        if (file == 1 && function == 4 && row == 2) {
            thrown = true;
            scenario_require(r, column > 0, "throw source column");
        }
    }
    scenario_require(r, found && thrown, "empty and throw locations present");
    const char *relocated = put(r, path(r, "relocated-mapping/main.goat"), main_text);
    put(r, path(r, "relocated-mapping/other.goat"), other);
    const char *second = path(r, "relocated-mapping.gbin"), *cwd = path(r, "unrelated");
    language_directory(r, cwd);
    invoke(r, c, "compile", relocated, "", second, cwd, false);
    size_t na, nb;
    const char *a = language_read(r, artifact, &na), *d = language_read(r, second, &nb);
    scenario_require_bytes(r, "relocated artifact identity", a, na, d, nb, false);
}

static void deep(scenario_run_t *r, configuration_t *c) {
    for (unsigned i = 0; i < 100; i++) {
        char name[64], source[160];
        snprintf(name, sizeof(name), "deep/%u.goat", i);
        if (i + 1 < 100)
            snprintf(source,
                     sizeof(source),
                     "const next = include \"%u.goat\"; public value = next.value + 1;",
                     i + 1);
        else
            strcpy(source, "public value = 1;");
        put(r, path(r, name), source);
    }
    const char *main = put(r, path(r, "deep/main.goat"), "println((include \"0.goat\").value);");
    invoke(r, c, "run", main, "100\n", NULL, NULL, false);
    const char *artifact = path(r, "deep.gbin");
    invoke(r, c, "compile", main, "", artifact, NULL, false);
    metadata_t m = metadata(r, artifact);
    scenario_require(r, yyjson_arr_size(get(m.json, "files")) == 101, "deep chain source count");
    remove_tree(r, path(r, "deep"));
    invoke(r, c, "binary", artifact, "100\n", NULL, NULL, false);
}

static void body(scenario_run_t *r, void *user) {
    configuration_t *c = user;
    if (c->index < N(fixtures))
        ordinary(r, c, &fixtures[c->index]);
    else
        switch (c->index - N(fixtures)) {
            case 0:
                invalid(r, c);
                break;
            case 1:
                hardlinks(r, c);
                break;
            case 2:
                symlinks(r, c);
                break;
            case 3:
                diagnostic(r, c);
                break;
            case 4:
                deep(r, c);
                break;
        }
}

int main(int argc, char **argv) {
#ifdef _WIN32
    fs_error_t arg_error = {0};
    if (!fs_arguments(&argc, &argv, &arg_error)) {
        clear_fs_error(&arg_error);
        return 1;
    }
#endif
    const char *extra[] = {"invalid", "hardlinks", "symlinks", "diagnostic-mapping", "deep"};
    char ids[N(fixtures) + N(extra)][96];
    configuration_t configs[N(fixtures) + N(extra)];
    scenario_definition_t defs[N(fixtures) + N(extra)];
    for (size_t i = 0; i < N(defs); i++) {
        snprintf(ids[i],
                 sizeof(ids[i]),
                 "integration.source_include.%s",
                 i < N(fixtures) ? fixtures[i].name : extra[i - N(fixtures)]);
        configs[i] = (configuration_t){.index = i};
        defs[i] = (scenario_definition_t){ids[i], 180000, body, &configs[i], NULL};
    }
    int result = 1;
    if (argc == 2 && !strcmp(argv[1], "--list")) {
        for (size_t i = 0; i < N(defs); i++)
            puts(ids[i]);
        result = 0;
    } else if (argc == 5 || argc == 6) {
        fs_error_t e = {0};
        char *exe = absolute_fs_path(argv[2], &e);
        clear_fs_error(&e);
        if (exe) {
            for (size_t i = 0; i < N(defs); i++)
                configs[i].exe = exe;
            scenario_options_t options = {.build_directory = argv[3], .data_directory = argv[4]};
            const char *selection[] = {argv[1], argc == 6 ? argv[5] : NULL};
            result = scenario_registry_main(defs, N(defs), argc == 6 ? 2 : 1, selection, &options);
        }
        FREE(exe);
    } else
        fputs("usage: source_include_scenarios ID HOST BUILD DATA [--keep-work]\n", stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
