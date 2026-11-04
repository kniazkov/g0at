/** @file module_book_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Execute the published RU/EN tutorial and check Markdown navigation (#392).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "lib/strings/string_ext.h"
#include "test/support/format_json.h"
#include "test/support/format_zip.h"
#include "test/support/inspect_report.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(x) scenario_require(r, (x), #x)
#define ARGS(...) ((const char *const[]){__VA_ARGS__, NULL})
typedef yyjson_mut_val jval_t;

typedef struct {
    const char *goat, *inspect, *merge, *source;
} config_t;

typedef struct {
    scenario_run_t *r;
    config_t *c;
    const char *root;
    unsigned count;
} suite_t;

static const char *const commands[] = {
    "goat build geometry.gmod --native off",
    "goat build app.gmod --native off",
    "goat run app.gmod --native off",
    "goat run modules/tutorial/1.0.gbin --module-dir modules --native off",
    "goat-inspect --json --module-dir modules modules/tutorial/1.0.gbin",
    "goat-merge merged.gbin modules/geometry/1.0.gbin"};

static bool free_doc(void *p) {
    yyjson_mut_doc_free(p);
    return true;
}

static bool free_plain(void *p) {
    free(p);
    return true;
}

static bool free_entries(void *p) {
    clear_fs_entries(p);
    FREE(p);
    return true;
}

static char *slice(scenario_run_t *r, const char *s, size_t n) {
    char *p = language_owned(r, ALLOC(n + 1));
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

static fs_entries_t *entries(scenario_run_t *r, const char *path) {
    fs_entries_t *list = CALLOC(sizeof(*list));
    scenario_defer(r, free_entries, list);
    fs_error_t e = {0};
    bool ok = list_fs_directory(path, list, &e);
    clear_fs_error(&e);
    REQUIRE(ok);
    return list;
}

static bool exists(scenario_run_t *r, const char *path) {
    fs_error_t e = {0};
    fs_info_t info;
    bool ok = inspect_fs_path(path, true, &info, &e), missing = e.status == FS_NOT_FOUND;
    clear_fs_error(&e);
    REQUIRE(ok || missing);
    return ok;
}

static const char *text_file(scenario_run_t *r, const char *path) {
    size_t n;
    const char *raw = language_read(r, path, &n);
    string_value_t decoded = decode_utf8_n(raw, n);
    bool valid = decoded.data != NULL;
    FREE_STRING(decoded);
    REQUIRE(valid && strlen(raw) == n);
    /* Python read_text performs universal-newline conversion. */
    char *out = language_owned(r, ALLOC(n + 1));
    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        if (raw[i] == '\r') {
            out[j++] = '\n';
            if (i + 1 < n && raw[i + 1] == '\n')
                i++;
        } else
            out[j++] = raw[i];
    }
    out[j] = 0;
    return out;
}

/* Python's Unicode whitespace and splitlines sets, independent of the host locale.
 * Inputs have already been checked as UTF-8. */
static unsigned codepoint(const char *p, size_t *width) {
    unsigned char c = (unsigned char)*p;
    if (c < 0x80) {
        *width = 1;
        return c;
    }
    size_t n = c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
    unsigned value = c & (n == 2 ? 31 : n == 3 ? 15 : 7);
    for (size_t i = 1; i < n; i++)
        value = value * 64 + ((unsigned char)p[i] & 63);
    *width = n;
    return value;
}

static bool space(unsigned c) {
    return (c >= 9 && c <= 13) || (c >= 0x1c && c <= 0x20) || c == 0x85 || c == 0xa0 || c == 0x1680
           || (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f
           || c == 0x205f || c == 0x3000;
}

static bool linebreak(unsigned c) {
    return (c >= 10 && c <= 13) || (c >= 0x1c && c <= 0x1e) || c == 0x85 || c == 0x2028
           || c == 0x2029;
}

static const char *skip_space(const char *p) {
    while (*p) {
        size_t n;
        if (!space(codepoint(p, &n)))
            break;
        p += n;
    }
    return p;
}

/* Extract exactly one marker followed by whitespace and an accepted fenced language.
 * This deliberately reads the published source rather than a duplicate fixture. */
static const char *marked(scenario_run_t *r,
                          const char *text,
                          const char *marker,
                          const char *lang,
                          const char *alternative) {
    const char *found = NULL, *p = text;
    size_t matches = 0;
    while ((p = strstr(p, marker))) {
        p += strlen(marker);
        p = skip_space(p);
        size_t n = strlen(lang);
        if (strncmp(p, lang, n)) {
            if (!alternative || strncmp(p, alternative, strlen(alternative)))
                continue;
            n = strlen(alternative);
        }
        const char *end = strstr(p + n, "```");
        if (!end)
            continue;
        found = slice(r, p + n, (size_t)(end - p - n));
        matches++;
        p = end + 3;
    }
    return matches == 1 ? found : NULL;
}

static bool tutorial(scenario_run_t *r, const char *text) {
    const char *block = marked(r, text, "<!-- module-book-cli -->", "```sh\n", NULL);
    if (!block)
        return false;
    size_t index = 0;
    for (const char *p = block; *p;) {
        const char *end = p;
        size_t width = 0;
        while (*end) {
            unsigned c = codepoint(end, &width);
            if (linebreak(c))
                break;
            end += width;
        }
        const char *start = skip_space(p), *trim = start;
        if (start > end)
            start = end;
        for (const char *q = start; q < end;) {
            size_t n;
            unsigned c = codepoint(q, &n);
            q += n;
            if (!space(c))
                trim = q;
        }
        if (trim > end)
            trim = end;
        if (trim > start) {
            if (index == sizeof(commands) / sizeof(commands[0])
                || strlen(commands[index]) != (size_t)(trim - start)
                || memcmp(start, commands[index], (size_t)(trim - start)))
                return false;
            index++;
        }
        p = *end ? end + width : end;
    }
    return index == sizeof(commands) / sizeof(commands[0]);
}

static int hex(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static bool external(const char *s) {
    if (*s == '#')
        return true;
    if (!isalpha((unsigned char)*s))
        return false;
    s++;
    while (isalnum((unsigned char)*s) || *s == '+' || *s == '.' || *s == '-')
        s++;
    return *s == ':';
}

/* urllib.parse.unquote decodes malformed percent-encoded UTF-8 with replacement. */
static const char *replace_invalid_utf8(scenario_run_t *r, const char *bytes, size_t size) {
    char *out = language_owned(r, ALLOC(size * 3 + 1));
    size_t used = 0;
    for (size_t i = 0; i < size;) {
        unsigned char c = (unsigned char)bytes[i];
        size_t width = c < 0x80                 ? 1
                       : c >= 0xc2 && c <= 0xdf ? 2
                       : c >= 0xe0 && c <= 0xef ? 3
                       : c >= 0xf0 && c <= 0xf4 ? 4
                                                : 0;
        size_t consumed = 1;
        bool valid = width != 0;
        for (size_t j = 1; valid && j < width; j++) {
            if (i + j == size) {
                valid = false;
                break;
            }
            unsigned char next = (unsigned char)bytes[i + j];
            unsigned char low = 0x80, high = 0xbf;
            if (j == 1) {
                if (c == 0xe0)
                    low = 0xa0;
                if (c == 0xed)
                    high = 0x9f;
                if (c == 0xf0)
                    low = 0x90;
                if (c == 0xf4)
                    high = 0x8f;
            }
            if (next < low || next > high) {
                valid = false;
                break;
            }
            consumed++;
        }
        if (valid) {
            memcpy(out + used, bytes + i, width);
            used += width;
            i += width;
        } else {
            memcpy(out + used, "\xef\xbf\xbd", 3);
            used += 3;
            i += consumed;
        }
    }
    out[used] = 0;
    return out;
}

/* Pure Windows path joining is exercised on every host without touching a UNC
 * location. Percent decoding can introduce a drive prefix after URL filtering. */
static size_t windows_drive_size(const char *path) {
    if (isalpha((unsigned char)path[0]) && path[1] == ':')
        return 2;
    if (path[0] != '/' || path[1] != '/')
        return 0;
    const char *server_end = strchr(path + 2, '/');
    if (!server_end)
        return strlen(path);
    const char *share_end = strchr(server_end + 1, '/');
    return share_end ? (size_t)(share_end - path) : strlen(path);
}

static const char *
windows_link_path(scenario_run_t *r, const char *directory, const char *filename) {
    char *parent = slice(r, directory, strlen(directory));
    char *child = slice(r, filename, strlen(filename));
    for (char *p = parent; *p; p++)
        if (*p == '\\')
            *p = '/';
    for (char *p = child; *p; p++)
        if (*p == '\\')
            *p = '/';
    size_t parent_drive = windows_drive_size(parent), child_drive = windows_drive_size(child);
    const char *tail = child;
    if (child_drive) {
        bool same_drive = child_drive == parent_drive;
        for (size_t i = 0; same_drive && i < child_drive; i++)
            same_drive = tolower((unsigned char)child[i]) == tolower((unsigned char)parent[i]);
        if (!same_drive || child[child_drive] == '/' || child_drive != 2)
            return child;
        /* A same-drive relative name retains the parent directory, but pathlib
         * uses the drive spelling supplied by the right operand. */
        memcpy(parent, child, child_drive);
        tail = child + child_drive;
    } else if (child[0] == '/') {
        size_t n = strlen(child);
        char *out = language_owned(r, ALLOC(parent_drive + n + 1));
        memcpy(out, parent, parent_drive);
        memcpy(out + parent_drive, child, n + 1);
        return out;
    }
    if (!*tail)
        return parent;
    size_t n = strlen(parent), m = strlen(tail);
    bool separator = n && parent[n - 1] != '/' && parent[n - 1] != ':';
    char *out = language_owned(r, ALLOC(n + separator + m + 1));
    memcpy(out, parent, n);
    if (separator)
        out[n++] = '/';
    memcpy(out + n, tail, m + 1);
    return out;
}

static const char *link_path(scenario_run_t *r, const char *directory, const char *filename) {
#ifdef _WIN32
    return windows_link_path(r, directory, filename);
#else
    if (filename[0] == '/')
        return filename;
    return language_join(r, directory, filename);
#endif
}

static bool links(scenario_run_t *r, const char *text, const char *directory) {
    unsigned fences = 0;
    for (const char *p = text; *p;) {
        if (!strncmp(p, "```", 3))
            fences++;
        while (*p) {
            size_t n;
            unsigned c = codepoint(p, &n);
            p += n;
            if (linebreak(c))
                break;
        }
    }
    if (fences % 2)
        return false;
    /* The Python oracle strips fenced and inline code before matching links. */
    char *prose = language_owned(r, ALLOC(strlen(text) + 1));
    size_t n = 0;
    for (const char *p = text; *p;) {
        if (!strncmp(p, "```", 3)) {
            const char *end = strstr(p + 3, "```");
            if (end) {
                p = end + 3;
                continue;
            }
        }
        prose[n++] = *p++;
    }
    prose[n] = 0;
    char *plain = language_owned(r, ALLOC(n + 1));
    n = 0;
    for (const char *p = prose; *p;) {
        if (*p == '`') {
            const char *end = strchr(p + 1, '`');
            if (end) {
                p = end + 1;
                continue;
            }
        }
        plain[n++] = *p++;
    }
    plain[n] = 0;
    for (const char *p = plain; (p = strchr(p, '[')); p++) {
        const char *label = p + 1, *end = label;
        while (*end && *end != ']' && *end != '\n')
            end++;
        if (end == label || *end != ']' || end[1] != '(')
            continue;
        const char *start = end + 2;
        end = start;
        while (*end && *end != ')') {
            size_t width;
            if (space(codepoint(end, &width)))
                break;
            end += width;
        }
        if (end == start || *end != ')')
            continue;
        const char *target = slice(r, start, (size_t)(end - start));
        p = end; /* re.findall resumes after the whole match, not nested brackets. */
        if (external(target))
            continue;
        const char *anchor = strchr(target, '#');
        size_t length = anchor ? (size_t)(anchor - target) : strlen(target);
        char *filename = language_owned(r, ALLOC(length + 1));
        size_t j = 0;
        for (size_t i = 0; i < length; i++) {
            if (target[i] == '%' && i + 2 < length && hex(target[i + 1]) >= 0
                && hex(target[i + 2]) >= 0) {
                filename[j++] = (char)(hex(target[i + 1]) * 16 + hex(target[i + 2]));
                i += 2;
            } else
                filename[j++] = target[i];
        }
        filename[j] = 0;
        if (strlen(filename) != j
            || !exists(r, link_path(r, directory, replace_invalid_utf8(r, filename, j))))
            return false;
    }
    return true;
}

static bool suffix(const char *name, const char *ending) {
    size_t n = strlen(name), length = strlen(ending);
    if (n < length)
        return false;
#ifdef _WIN32
    return !_stricmp(name + n - length, ending);
#else
    return !strcmp(name + n - length, ending);
#endif
}

static bool markdown(const char *name) {
    return suffix(name, ".md");
}

static bool paired(fs_entries_t *a, fs_entries_t *b) {
    for (size_t pass = 0; pass < 2; pass++) {
        fs_entries_t *x = pass ? b : a, *y = pass ? a : b;
        for (size_t i = 0; i < x->count; i++)
            if (markdown(x->names[i])) {
                bool found = false;
                for (size_t j = 0; j < y->count; j++)
                    if (!strcmp(x->names[i], y->names[j]))
                        found = true;
                if (!found)
                    return false;
            }
    }
    return true;
}

static bool same_snippet(const char *a, const char *b) {
    return a && b && !strcmp(a, b);
}

static void books(scenario_run_t *r, config_t *c, const char **snippets) {
    const char *dirs[] = {language_join(r, c->source, "docs/book/ru"),
                          language_join(r, c->source, "docs/book/en")};
    fs_entries_t *pages[] = {entries(r, dirs[0]), entries(r, dirs[1])};
    REQUIRE(paired(pages[0], pages[1]));
    for (size_t i = 0; i < 2; i++) {
        REQUIRE(tutorial(r, text_file(r, language_join(r, dirs[i], "30-modules.md"))));
        for (size_t j = 0; j < pages[i]->count; j++)
            if (markdown(pages[i]->names[j])) {
                const char *path = language_join(r, dirs[i], pages[i]->names[j]);
                scenario_require(r, links(r, text_file(r, path), dirs[i]), path);
            }
    }
    const char *readmes[] = {"README.md", "README.ru.md"};
    const char *markers[] = {"<!-- readme-closure -->", "<!-- readme-loop -->"};
    for (size_t i = 0; i < 2; i++) {
        const char *path = language_join(r, c->source, readmes[i]), *text = text_file(r, path);
        REQUIRE(tutorial(r, text));
        scenario_require(r, links(r, text, c->source), path);
        for (size_t j = 0; j < 2; j++) {
            const char *snippet = marked(r, text, markers[j], "```text\n", "```goat\n");
            REQUIRE(snippet != NULL);
            if (!i)
                snippets[j] = snippet;
            else
                REQUIRE(same_snippet(snippets[j], snippet));
        }
    }
}

static void copy_file(scenario_run_t *r, const char *source, const char *destination) {
    size_t n;
    const char *data = language_read(r, source, &n);
    language_write(r, destination, data, n);
}

static void copy_tree(scenario_run_t *r, const char *source, const char *destination) {
    language_directory(r, destination);
    fs_entries_t *list = entries(r, source);
    for (size_t i = 0; i < list->count; i++) {
        const char *from = language_join(r, source, list->names[i]),
                   *to = language_join(r, destination, list->names[i]);
        fs_error_t e = {0};
        fs_info_t info;
        bool ok = inspect_fs_path(from, true, &info, &e);
        clear_fs_error(&e);
        REQUIRE(ok);
        if (info.kind == FS_DIRECTORY)
            copy_tree(r, from, to);
        else {
            REQUIRE(info.kind == FS_FILE);
            copy_file(r, from, to);
        }
    }
}

static yyjson_mut_doc *parse(scenario_run_t *r, const char *data, size_t size) {
    char error[512];
    yyjson_mut_doc *doc = inspect_report_parse(data, size, error, sizeof(error));
    if (doc)
        scenario_defer(r, free_doc, doc);
    scenario_require(r, doc != NULL, error);
    return doc;
}

static jval_t *get(jval_t *v, const char *key) {
    return yyjson_mut_obj_get(v, key);
}

static bool eq(jval_t *v, const char *text) {
    return yyjson_mut_is_str(v) && yyjson_mut_get_len(v) == strlen(text)
           && !memcmp(yyjson_mut_get_str(v), text, strlen(text));
}

static jval_t *metadata(scenario_run_t *r, const char *path) {
    size_t n;
    const unsigned char *data = (const unsigned char *)language_read(r, path, &n);
    format_zip_info_t info;
    char error[512];
    REQUIRE(format_zip_inspect(data, n, SIZE_MAX, &info, error, sizeof(error)));
    size_t count = info.count;
    jval_t *result = NULL;
    for (size_t i = 0; i < count; i++) {
        REQUIRE(format_zip_inspect(data, n, i, &info, error, sizeof(error)));
        if (info.name_size != 11 || memcmp(info.name, "module.json", 11))
            continue;
        REQUIRE(result == NULL);
        size_t size;
        unsigned char *part =
            format_zip_extract(data, n, i, 16 * 1024 * 1024, &size, error, sizeof(error));
        if (part)
            scenario_defer(r, free_plain, part);
        scenario_require(r, part != NULL, error);
        result = yyjson_mut_doc_get_root(parse(r, (const char *)part, size));
    }
    REQUIRE(result != NULL);
    return result;
}

static bool successful_report(jval_t *report) {
    jval_t *version = get(report, "reportVersion"), *modules = get(report, "modules");
    if (!yyjson_mut_is_int(version) || yyjson_mut_get_sint(version) != 1
        || !yyjson_mut_is_true(get(report, "success")) || !yyjson_mut_is_arr(modules))
        return false;
    bool geometry = false, tutorial_seen = false;
    for (size_t i = 0; i < yyjson_mut_arr_size(modules); i++) {
        jval_t *m = yyjson_mut_arr_get(modules, i), *name = get(get(m, "metadata"), "name");
        if (!eq(get(m, "validation"), "passed") || !yyjson_mut_is_arr(get(m, "native"))
            || yyjson_mut_arr_size(get(m, "native")))
            return false;
        if (eq(name, "geometry"))
            geometry = true;
        else if (eq(name, "tutorial"))
            tutorial_seen = true;
        else
            return false;
    }
    return geometry && tutorial_seen;
}

static bool failed_report(jval_t *report) {
    jval_t *code = get(report, "exitCode");
    return yyjson_mut_is_false(get(report, "success")) && yyjson_mut_is_int(code)
           && yyjson_mut_get_sint(code) != 0;
}

static jval_t *report(suite_t *s, scenario_command_result_t v, bool success) {
    scenario_run_t *r = s->r;
    jval_t *value = yyjson_mut_doc_get_root(parse(r, v.out, v.out_size));
    if (success) {
        REQUIRE(v.err_size == 0);
        const char *dir = language_join(r, s->c->source, "docs/module-format-v1");
        size_t n;
        const char *raw = language_read(r, language_join(r, dir, "inspect-report.schema.json"), &n);
        jval_t *schema = yyjson_mut_doc_get_root(parse(r, raw, n));
        char error[512];
        scenario_require(r,
                         inspect_report_schema(value, schema, schema, dir, error, sizeof(error)),
                         error);
        REQUIRE(successful_report(value));
    } else
        REQUIRE(failed_report(value));
    return value;
}

typedef struct {
    child_options_t options;
    scenario_command_result_t result;
} command_t;

static void command_body(scenario_run_t *r, void *p) {
    command_t *c = p;
    c->result = scenario_command_capture(r, &c->options);
}

static bool output_matches(const char *expected, const void *data, size_t size) {
    const unsigned char *bytes = data;
    size_t j = 0, n = strlen(expected);
    for (size_t i = 0; i < size; i++) {
        if (bytes[i] == '\r' && i + 1 < size && bytes[i + 1] == '\n')
            continue;
        if (j == n || bytes[i] != (unsigned char)expected[j++])
            return false;
    }
    return j == n;
}

static scenario_command_result_t
invoke(suite_t *s, const char *exe, const char *const *args, bool fail, const char *expected) {
    scenario_run_t *r = s->r;
    size_t n = 0;
    while (args[n])
        n++;
    const char **argv = language_owned(r, CALLOC((n + 2) * sizeof(*argv)));
    argv[0] = exe;
    for (size_t i = 0; i < n; i++)
        argv[i + 1] = args[i];
    command_t c = {
        .options = {.executable = exe,
                    .working_directory = s->root,
                    .arguments = argv,
                    .environment = language_environment_override(r, "GOAT_MODULE_PATH", ""),
                    .empty_stdin = true}};
    scenario_with_timeout(r, 60000, command_body, &c);
    s->count++;
    REQUIRE((c.result.exit_code != 0) == fail);
    if (expected) {
        bool matches = output_matches(expected, c.result.out, c.result.out_size);
        scenario_require_bytes(r,
                               "book stdout",
                               expected,
                               strlen(expected),
                               c.result.out,
                               c.result.out_size,
                               true);
        REQUIRE(matches);
    }
    return c.result;
}

/* Tokenization is intentionally constrained to the six exact, already checked commands. */
static scenario_command_result_t
tutorial_command(suite_t *s, size_t index, bool fail, const char *expected) {
    scenario_run_t *r = s->r;
    REQUIRE(index < sizeof(commands) / sizeof(commands[0]));
    char *text = slice(r, commands[index], strlen(commands[index]));
    const char *args[16] = {0};
    size_t n = 0;
    char *p = text;
    while (*p) {
        REQUIRE(n < 15);
        args[n++] = p;
        char *end = strchr(p, ' ');
        if (!end)
            break;
        *end = 0;
        p = end + 1;
    }
    const char *exe = !strcmp(args[0], "goat")           ? s->c->goat
                      : !strcmp(args[0], "goat-inspect") ? s->c->inspect
                                                         : s->c->merge;
    return invoke(s, exe, args + 1, fail, expected);
}

static bool dependency(jval_t *app, jval_t *geometry) {
    jval_t *deps = get(app, "dependencies");
    if (!yyjson_mut_is_arr(deps) || yyjson_mut_arr_size(deps) != 1)
        return false;
    jval_t *dep = yyjson_mut_arr_get(deps, 0);
    return yyjson_mut_obj_size(dep) == 3 && eq(get(dep, "name"), "geometry")
           && eq(get(dep, "version"), "1.0")
           && format_json_equal(get(dep, "hash"), get(geometry, "mainHash"));
}

static void body(scenario_run_t *r, void *user) {
    config_t *c = user;
    const char *snippets[2] = {0};
    books(r, c, snippets);
    suite_t s = {
        r,
        c,
        language_join(r, scenario_work_directory(scenario_run_context(r)), "module tutorial"),
        0};
    copy_tree(r, language_join(r, c->source, "docs/book/examples/30-modules"), s.root);
    const char *names[] = {"closure.goat", "loop.goat"};
    for (size_t i = 0; i < 2; i++) {
        language_write(r, language_join(r, s.root, names[i]), snippets[i], strlen(snippets[i]));
        invoke(&s, c->goat, ARGS(names[i], "--native", "off"), false, i ? "10\n" : "5\n");
    }
    copy_file(r,
              language_join(r, c->source, "example/hello_world.goat"),
              language_join(r, s.root, "hello.goat"));
    invoke(&s, c->goat, ARGS("hello.goat"), false, "it works!\n");
    copy_file(r,
              language_join(r, c->source, "docs/book/examples/22-native-module.goat"),
              language_join(r, s.root, "program.goat"));
    invoke(&s, c->goat, ARGS("--compile", "program.goat"), false, "");
    language_remove(r, language_join(r, s.root, "program.goat"));
    invoke(&s, c->goat, ARGS("--run", "program.gbin"), false, "7\n6\n4.0\n");
    for (size_t i = 0; i < 6; i++) {
        scenario_command_result_t v = tutorial_command(&s,
                                                       i,
                                                       false,
                                                       i < 2   ? ""
                                                       : i < 4 ? "25\n"
                                                               : NULL);
        if (i == 4)
            report(&s, v, true);
    }
    const char *geometry_path = language_join(r, s.root, "modules/geometry/1.0.gbin");
    const char *app_path = language_join(r, s.root, "modules/tutorial/1.0.gbin");
    jval_t *geometry = metadata(r, geometry_path), *app = metadata(r, app_path);
    REQUIRE(format_json_equal(geometry, metadata(r, language_join(r, s.root, "merged.gbin"))));
    REQUIRE(yyjson_mut_is_arr(get(geometry, "files"))
            && yyjson_mut_arr_size(get(geometry, "files")) == 2);
    REQUIRE(get(get(geometry, "externals"), "square") != NULL);
    REQUIRE(dependency(app, geometry));
    fs_error_t e = {0};
    bool ok = remove_fs_tree(language_join(r, s.root, "src"), &e);
    clear_fs_error(&e);
    REQUIRE(ok);
    fs_entries_t *list = entries(r, s.root);
    for (size_t i = 0; i < list->count; i++) {
        if (suffix(list->names[i], ".gmod"))
            language_remove(r, language_join(r, s.root, list->names[i]));
    }
    tutorial_command(&s, 3, false, "25\n");
    language_remove(r, geometry_path);
    tutorial_command(&s, 3, true, "");
    report(&s, tutorial_command(&s, 4, true, NULL), false);
    invoke(&s, c->merge, ARGS("standalone-copy.gbin", "modules/tutorial/1.0.gbin"), false, NULL);
    REQUIRE(format_json_equal(metadata(r, language_join(r, s.root, "standalone-copy.gbin")),
                              metadata(r, app_path)));
    REQUIRE(s.count == 15);
    printf("Module book checks passed: %u commands, matching RU/EN README/book tutorial and local "
           "links\n",
           s.count);
}

/* Negative controls call the very same predicates as the runtime path. They do
 * not rely on assert(), Python or executing deliberately malformed production input. */
static void controls(scenario_run_t *r, void *user) {
    config_t *c = user;
    const char *snippets[2] = {0};
    books(r, c, snippets);
    const char *readme = text_file(r, language_join(r, c->source, "README.md"));
    REQUIRE(tutorial(r, readme));
    char *bad = slice(r, readme, strlen(readme));
    char *command = strstr(bad, commands[0]);
    REQUIRE(command != NULL);
    command[0] = 'x';
    REQUIRE(!tutorial(r, bad));
    REQUIRE(
        !tutorial(r,
                  "<!-- module-book-cli -->\n```sh\ngoat build geometry.gmod --native off\n```\n"));
    size_t size = strlen(readme);
    char *duplicate = language_owned(r, ALLOC(size * 2 + 1));
    memcpy(duplicate, readme, size);
    memcpy(duplicate + size, readme, size + 1);
    REQUIRE(!tutorial(r, duplicate));
    REQUIRE(marked(r, duplicate, "<!-- readme-loop -->", "```text\n", "```goat\n") == NULL);
    REQUIRE(marked(r,
                   "<!-- readme-loop -->\n```goat\nmissing end",
                   "<!-- readme-loop -->",
                   "```text\n",
                   "```goat\n")
            == NULL);
    REQUIRE(same_snippet(snippets[0], snippets[0]));
    REQUIRE(!same_snippet(snippets[0], "print(999);\n"));
    REQUIRE(!same_snippet(snippets[0], NULL));
    char *stale_readme = slice(r, readme, strlen(readme));
    char *snippet_start = strstr(stale_readme, snippets[0]);
    REQUIRE(snippet_start && *snippet_start);
    *snippet_start = *snippet_start == '?' ? '!' : '?';
    const char *stale_snippet =
        marked(r, stale_readme, "<!-- readme-closure -->", "```text\n", "```goat\n");
    REQUIRE(stale_snippet != NULL);
    REQUIRE(!same_snippet(snippets[0], stale_snippet));
    REQUIRE(!links(r, "```goat\nunfinished\n", c->source));
    REQUIRE(!links(r, "[bad](missing-module-book-control.md)\n", c->source));
    REQUIRE(links(r,
                  "[ok](README.md#anchor) [ok](%52EADME.md) [web](https://invalid.example/) "
                  "[anchor](#only)\n",
                  c->source));
    REQUIRE(links(r,
                  "```text\n[ignore](missing-module-book-control.md)\n```\n`[ignore](missing-"
                  "module-book-control.md)`\n",
                  c->source));
    REQUIRE(links(r, "[x](https://host/[nested](missing))", c->source));
    REQUIRE(links(r, "[not-a-link](missing\xc2\xa0space)\n", c->source));
    char unicode_tutorial[1024] = "<!-- module-book-cli -->\xc2\xa0```sh\n";
    for (size_t i = 0; i < 6; i++) {
        strcat(unicode_tutorial, "\xe3\x80\x80");
        strcat(unicode_tutorial, commands[i]);
        strcat(unicode_tutorial, "\xc2\xa0\xe2\x80\xa8");
    }
    strcat(unicode_tutorial, "```");
    REQUIRE(tutorial(r, unicode_tutorial));
    const char *work = scenario_work_directory(scenario_run_context(r));
    const char *absolute = language_join(r, work, "absolute target.md");
    language_write(r, absolute, "", 0);
    size_t absolute_size = strlen(absolute);
    char *absolute_link = language_owned(r, ALLOC(absolute_size * 3 + 16));
    strcpy(absolute_link, "[absolute](");
    size_t used = strlen(absolute_link);
    for (size_t i = 0; i < absolute_size; i++) {
        sprintf(absolute_link + used, "%%%02X", (unsigned char)absolute[i]);
        used += 3;
    }
    strcpy(absolute_link + used, ")");
    REQUIRE(links(r, absolute_link, c->source));
    language_write(r, language_join(r, work, "\xef\xbf\xbd"), "", 0);
    REQUIRE(links(r, "[replacement](%ff)", work));
    REQUIRE(links(r, "[replacement](%e2%82)", work));
    language_write(r, language_join(r, work, "hash#name"), "", 0);
    REQUIRE(links(r, "[decoded anchor](hash%23name#ignored)", work));
    REQUIRE(!strcmp(windows_link_path(r, "C:/repo/docs", "C:README.md"), "C:/repo/docs/README.md"));
    REQUIRE(!strcmp(windows_link_path(r, "C:/repo/docs", "c:README.md"), "c:/repo/docs/README.md"));
    REQUIRE(!strcmp(windows_link_path(r, "C:/repo/docs", "C:"), "C:/repo/docs"));
    REQUIRE(!strcmp(windows_link_path(r, "C:/repo/docs", "D:README.md"), "D:README.md"));
    REQUIRE(!strcmp(windows_link_path(r, "C:/repo/docs", "C:/README.md"), "C:/README.md"));
    REQUIRE(!strcmp(windows_link_path(r, "C:/repo/docs", "/README.md"), "C:/README.md"));
    REQUIRE(!strcmp(windows_link_path(r, "//server/share/repo/docs", "/README.md"),
                    "//server/share/README.md"));
    REQUIRE(!strcmp(windows_link_path(r, "\\\\server\\share\\repo\\docs", "\\README.md"),
                    "//server/share/README.md"));
    REQUIRE(!strcmp(windows_link_path(r, "//server/share/repo/docs", "//other/share/README.md"),
                    "//other/share/README.md"));
    const char *names_a[] = {"index.md", "30-modules.md", "ignore.txt"};
    const char *names_b[] = {"30-modules.md", "index.md"};
    fs_entries_t a = {(char **)names_a, 3}, b = {(char **)names_b, 2};
    REQUIRE(paired(&a, &b));
    b.count = 1;
    REQUIRE(!paired(&a, &b));
    REQUIRE(!paired(&b, &a));

    REQUIRE(output_matches("25\n", "25\r\n", 4));
    REQUIRE(!output_matches("25\n", "26\n", 3));
    REQUIRE(!output_matches("25\n", "25\nextra", 8));
    REQUIRE(!output_matches("25\n", "25", 2));
    REQUIRE(!output_matches("", "\0", 1));
    const char *success_json =
        "{\"reportVersion\":1,\"success\":true,\"modules\":[{\"metadata\":{\"name\":\"geometry\"},"
        "\"validation\":\"passed\",\"native\":[]},{\"metadata\":{\"name\":\"tutorial\"},"
        "\"validation\":\"passed\",\"native\":[]}]}";
    yyjson_mut_doc *sdoc = parse(r, success_json, strlen(success_json));
    jval_t *success = yyjson_mut_doc_get_root(sdoc);
    REQUIRE(successful_report(success));
    jval_t *first = yyjson_mut_arr_get(get(success, "modules"), 0);
    REQUIRE(yyjson_mut_arr_add_str(sdoc, get(first, "native"), "unexpected"));
    REQUIRE(!successful_report(success));
    REQUIRE(yyjson_mut_arr_clear(get(first, "native")));
    REQUIRE(yyjson_mut_obj_remove_str(first, "validation") != NULL);
    REQUIRE(yyjson_mut_obj_add_str(sdoc, first, "validation", "failed"));
    REQUIRE(!successful_report(success));
    const char *dir = language_join(r, c->source, "docs/module-format-v1");
    size_t n;
    const char *raw = language_read(r, language_join(r, dir, "inspect-report.schema.json"), &n);
    jval_t *schema = yyjson_mut_doc_get_root(parse(r, raw, n));
    const char *minimal = "{\"reportVersion\":1,\"success\":true,\"exitCode\":0,\"input\":null,"
                          "\"target\":null,\"modules\":[],\"diagnostics\":[]}";
    yyjson_mut_doc *doc = parse(r, minimal, strlen(minimal));
    jval_t *v = yyjson_mut_doc_get_root(doc);
    char error[512];
    REQUIRE(inspect_report_schema(v, schema, schema, dir, error, sizeof(error)));
    REQUIRE(!successful_report(v));
    REQUIRE(yyjson_mut_obj_add_str(doc, v, "unexpected", "reject"));
    REQUIRE(!inspect_report_schema(v, schema, schema, dir, error, sizeof(error)));
    REQUIRE(yyjson_mut_obj_remove_str(v, "unexpected") != NULL);
    REQUIRE(yyjson_mut_obj_remove_str(v, "diagnostics") != NULL);
    REQUIRE(!inspect_report_schema(v, schema, schema, dir, error, sizeof(error)));
    const char *reports[] = {"{\"success\":true,\"exitCode\":1}",
                             "{\"success\":false,\"exitCode\":0}",
                             "{\"success\":false,\"exitCode\":\"1\"}",
                             "{\"success\":false,\"exitCode\":1}"};
    for (size_t i = 0; i < 4; i++)
        REQUIRE(failed_report(yyjson_mut_doc_get_root(parse(r, reports[i], strlen(reports[i]))))
                == (i == 3));
    const char *good =
        "{\"dependencies\":[{\"name\":\"geometry\",\"version\":\"1.0\",\"hash\":\"abc\"}]}";
    const char *geometry = "{\"mainHash\":\"abc\",\"files\":[{},{}],\"externals\":{\"square\":{}}}";
    jval_t *app = yyjson_mut_doc_get_root(parse(r, good, strlen(good)));
    yyjson_mut_doc *gdoc = parse(r, geometry, strlen(geometry));
    jval_t *g = yyjson_mut_doc_get_root(gdoc);
    REQUIRE(dependency(app, g));
    jval_t *copy = yyjson_mut_doc_get_root(parse(r, geometry, strlen(geometry)));
    REQUIRE(format_json_equal(g, copy));
    REQUIRE(yyjson_mut_obj_remove_str(g, "mainHash") != NULL);
    REQUIRE(yyjson_mut_obj_add_str(gdoc, g, "mainHash", "stale"));
    REQUIRE(!dependency(app, g));
    REQUIRE(!format_json_equal(g, copy));
    puts("Module book negative controls passed: docs, pairing, marked commands/snippets, links, "
         "schema and metadata");
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
    if (argc == 7 || (argc == 8 && !strcmp(argv[7], "--keep-work"))) {
        char *paths[8] = {0};
        bool valid = true;
        for (int i = 2; i <= 6; i++) {
            fs_error_t e = {0};
            paths[i] = absolute_fs_path(argv[i], &e);
            clear_fs_error(&e);
            valid &= paths[i] != NULL;
        }
        if (valid) {
            fs_error_t e = {0};
            paths[7] = join_fs_path(paths[6], "test/data", &e);
            clear_fs_error(&e);
            valid = paths[7] != NULL;
        }
        if (valid) {
            config_t c = {paths[2], paths[3], paths[4], paths[6]};
            scenario_definition_t definitions[] = {
                {"integration.module_book", 900000, body, &c, NULL},
                {"integration.module_book.controls", 60000, controls, &c, NULL}};
            scenario_options_t options = {.build_directory = paths[5], .data_directory = paths[7]};
            const char *selection[] = {argv[1], argc == 8 ? "--keep-work" : NULL};
            result = scenario_registry_main(definitions, 2, argc == 8 ? 2 : 1, selection, &options);
        }
        for (size_t i = 2; i <= 7; i++)
            FREE(paths[i]);
    } else
        fputs(
            "usage: module_book_scenarios ID GOAT INSPECT MERGE BUILD SOURCE_ROOT [--keep-work]\n",
            stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
