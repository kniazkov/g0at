/** @file child_process_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Process primitive contracts with an independent external OS watchdog (#375).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/format_json.h"
#include "test/support/language_execution.h"
#include "test/support/module_loading_owner.h"
#include "test/support/registry.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#define REQUIRE(x) scenario_require(r, (x), #x)
#define ARGS(...) ((const char *const[]){__VA_ARGS__, NULL})
typedef yyjson_mut_val jval_t;

typedef struct {
    const char *fixture, *goat, *kind;
    bool skipped;
} config_t;

typedef struct {
    scenario_run_t *r;
    config_t *c;
    const char *root;
    const char *const *env;
    unsigned serial;
} suite_t;

typedef struct {
    const void *data;
    size_t size;
} bytes_t;

typedef struct {
    uint64_t values[32];
    size_t count;
} pids_t;

static jval_t *get(jval_t *v, const char *k) {
    return yyjson_mut_obj_get(v, k);
}

static bool free_doc(void *p) {
    yyjson_mut_doc_free(p);
    return true;
}

static const char *text(scenario_run_t *r, jval_t *v) {
    const char *s = yyjson_mut_get_str(v);
    REQUIRE(s && strlen(s) == yyjson_mut_get_len(v));
    return s;
}

static jval_t *parse(scenario_run_t *r, loading_bytes_t bytes) {
    char error[256];
    yyjson_mut_doc *d = format_json_parse(bytes.data, bytes.size, error, sizeof(error));
    scenario_require(r, d != NULL, error);
    scenario_defer(r, free_doc, d);
    jval_t *v = yyjson_mut_doc_get_root(d);
    REQUIRE(yyjson_mut_is_obj(v));
    return v;
}

static const char *owner_name(suite_t *s, const char *mode) {
    scenario_run_t *r = s->r;
    char name[96];
    snprintf(name, sizeof(name), "%02u-%s", ++s->serial, mode);
    char *out = language_owned(r, ALLOC(strlen(name) + 1));
    strcpy(out, name);
    return out;
}

static loading_owner_t *start(suite_t *s,
                              const char *mode,
                              const void *input,
                              size_t size,
                              bool close_input,
                              const loading_owner_options_t *options) {
    return loading_owner_command_start_options(s->r,
                                               s->root,
                                               owner_name(s, mode),
                                               ARGS(s->c->fixture, mode),
                                               s->env,
                                               input,
                                               size,
                                               close_input,
                                               options);
}

static bytes_t fixture_bytes(scenario_run_t *r, const char *name) {
    const char *directory =
        language_join(r, scenario_data_directory(scenario_run_context(r)), "child-process");
    size_t n;
    const char *data = language_read(r, language_join(r, directory, name), &n);
    return (bytes_t){data, n};
}

static jval_t *run_fixture(suite_t *s, const char *mode) {
    scenario_run_t *r = s->r;
    bytes_t input = fixture_bytes(r, "stdin-q.bin");
    loading_owner_t *owner = start(s, mode, input.data, input.size, true, NULL);
    loading_owner_command_wait(owner, 0, 12000, true);
    jval_t *v = parse(r, loading_owner_output(owner));
    REQUIRE(yyjson_mut_is_int(get(v, "cleanup")) && yyjson_mut_get_sint(get(v, "cleanup")) == 0);
    if (strcmp(mode, "missing"))
        REQUIRE(yyjson_mut_is_int(get(v, "error")) && yyjson_mut_get_sint(get(v, "error")) == 0);
    return v;
}

static int64_t integer(scenario_run_t *r, jval_t *v, const char *key) {
    jval_t *x = get(v, key);
    REQUIRE(yyjson_mut_is_int(x));
    return yyjson_mut_get_sint(x);
}

static bool boolean(scenario_run_t *r, jval_t *v, const char *key) {
    jval_t *x = get(v, key);
    REQUIRE(yyjson_mut_is_bool(x));
    return yyjson_mut_get_bool(x);
}

static bytes_t unhex(scenario_run_t *r, jval_t *v) {
    const char *s = text(r, v);
    size_t n = strlen(s);
    REQUIRE(n % 2 == 0 && n <= 4 * 1024 * 1024);
    unsigned char *out = language_owned(r, ALLOC(n / 2 + 1));
    for (size_t i = 0; i < n; i += 2) {
        unsigned a = s[i] >= '0' && s[i] <= '9'   ? (unsigned)(s[i] - '0')
                     : s[i] >= 'a' && s[i] <= 'f' ? (unsigned)(s[i] - 'a' + 10)
                     : s[i] >= 'A' && s[i] <= 'F' ? (unsigned)(s[i] - 'A' + 10)
                                                  : 99;
        unsigned b = s[i + 1] >= '0' && s[i + 1] <= '9'   ? (unsigned)(s[i + 1] - '0')
                     : s[i + 1] >= 'a' && s[i + 1] <= 'f' ? (unsigned)(s[i + 1] - 'a' + 10)
                     : s[i + 1] >= 'A' && s[i + 1] <= 'F' ? (unsigned)(s[i + 1] - 'A' + 10)
                                                          : 99;
        REQUIRE(a < 16 && b < 16);
        out[i / 2] = (unsigned char)(a * 16 + b);
    }
    out[n / 2] = 0;
    return (bytes_t){out, n / 2};
}

static void basic(suite_t *s) {
    scenario_run_t *r = s->r;
    const char *kind = s->c->kind, *mode = kind;
    if (!strncmp(kind, "capture-", 8))
        mode = kind + 8;
    else if (!strncmp(kind, "timeout-", 8))
        mode = kind + 8;
    else if (!strcmp(kind, "arguments"))
        mode = "args";
    jval_t *v = run_fixture(s, mode);
    int64_t status = integer(r, v, "status"), code = integer(r, v, "code");
    if (!strcmp(kind, "empty") || !strcmp(kind, "exit") || !strcmp(kind, "stdin")
        || !strcmp(kind, "closed-input") || !strcmp(kind, "inherit") || !strcmp(kind, "parent")) {
        REQUIRE(status == 1 && code == (!strcmp(kind, "exit") ? 23 : 0));
        if (!strcmp(kind, "empty")) {
            REQUIRE(!*text(r, get(v, "out")) && !*text(r, get(v, "err")));
            REQUIRE(!boolean(r, v, "outTruncated") && !boolean(r, v, "errTruncated"));
        }
        if (!strcmp(kind, "stdin"))
            REQUIRE(integer(r, v, "parentInput") == 'Q');
    } else if (!strncmp(kind, "capture-", 8)) {
        REQUIRE(status == 1 && code == 0);
        bytes_t out = unhex(r, get(v, "out")), err = unhex(r, get(v, "err"));
        bytes_t expected = fixture_bytes(r, "capture-prefix.bin");
        REQUIRE(expected.size == 65536);
        scenario_require_bytes(r,
                               "exact stdout capture prefix",
                               expected.data,
                               expected.size,
                               out.data,
                               out.size,
                               false);
        scenario_require_bytes(r,
                               "exact stderr capture prefix",
                               expected.data,
                               expected.size,
                               err.data,
                               err.size,
                               false);
        bool truncated = strcmp(mode, "exact") != 0;
        REQUIRE(boolean(r, v, "outTruncated") == truncated
                && boolean(r, v, "errTruncated") == truncated);
    } else if (!strcmp(kind, "arguments")) {
        bytes_t actual = unhex(r, get(v, "result"));
        bytes_t expected = fixture_bytes(r, "arguments.bin");
        scenario_require_bytes(r,
                               "exact UTF-8 and embedded-NUL argv",
                               expected.data,
                               expected.size,
                               actual.data,
                               actual.size,
                               false);
    } else if (!strcmp(kind, "missing"))
        REQUIRE(status == 5);
    else if (!strcmp(kind, "expired"))
        REQUIRE(status == 3);
    else if (!strcmp(kind, "stop"))
        REQUIRE(status == 4);
    else if (!strncmp(kind, "timeout-", 8)) {
        REQUIRE(status == 3 && integer(r, v, "ms") < 6000);
        if (!strcmp(mode, "continuous"))
            REQUIRE(*text(r, get(v, "out")) && *text(r, get(v, "err")));
    } else {
        if (!strcmp(kind, "tree-exit"))
            REQUIRE(status == 1 && code == 0);
        else if (!strcmp(kind, "tree-fail"))
            REQUIRE(status == 1 && code == 23);
        else
            REQUIRE(status == 2 || (status == 1 && code != 0));
        REQUIRE(*text(r, get(v, "result")));
    }
}

static void stdin_case(suite_t *s) {
    scenario_run_t *r = s->r;
    const char *kind = s->c->kind;
    loading_owner_options_t options = {0};
    bytes_t fixture = fixture_bytes(r, "stdin-q.bin");
    const void *input = fixture.data;
    size_t size = fixture.size;
    bool close_input = false;
    if (!strcmp(kind, "stdin-file")) {
        const char *path = language_join(r, s->root, "parent-input.bin");
        bytes_t contents = fixture_bytes(r, "stdin-file.bin");
        language_write(r, path, contents.data, contents.size);
        options.stdin_file = path;
        input = NULL;
        size = 0;
        close_input = true;
    } else if (!strcmp(kind, "stdin-pty")) {
#ifdef _WIN32
        puts("SKIP: POSIX terminal stdin scenario");
        s->c->skipped = true;
        return;
#else
        options.stdin_pty = true;
        fixture = fixture_bytes(r, "stdin-terminal.bin");
        input = fixture.data;
        size = fixture.size;
#endif
    }
    loading_owner_t *owner = start(s, "stdin", input, size, close_input, &options);
    loading_owner_command_wait(owner, 0, 8000, false);
    jval_t *v = parse(r, loading_owner_output(owner));
    REQUIRE(integer(r, v, "code") == 0 && integer(r, v, "parentInput") == 'Q');
}

static bytes_t strip_line(loading_bytes_t line) {
    const unsigned char *p = line.data;
    size_t n = line.size;
    while (n && (p[n - 1] == '\n' || p[n - 1] == '\r'))
        n--;
    return (bytes_t){p, n};
}

static bool decimal(bytes_t b, uint64_t *value) {
    const unsigned char *p = b.data;
    uint64_t out = 0;
    if (!b.size)
        return false;
    for (size_t i = 0; i < b.size; i++) {
        if (p[i] < '0' || p[i] > '9' || out > (UINT64_MAX - (p[i] - '0')) / 10)
            return false;
        out = out * 10 + (p[i] - '0');
    }
    if (!out)
        return false;
    *value = out;
    return true;
}

static void add_pid(scenario_run_t *r, pids_t *p, uint64_t value) {
    REQUIRE(p->count < sizeof(p->values) / sizeof(p->values[0]));
    p->values[p->count++] = value;
}

static pids_t ready(suite_t *s, loading_owner_t *owner, const char *marker, bool pin) {
    scenario_run_t *r = s->r;
    pids_t result = {0};
    for (;;) {
        loading_bytes_t line;
        REQUIRE(loading_owner_next_line(owner, 8000, &line));
        bytes_t trimmed = strip_line(line);
        if (trimmed.size == strlen(marker) && !memcmp(trimmed.data, marker, trimmed.size))
            return result;
        uint64_t pid;
        REQUIRE(decimal(trimmed, &pid));
        add_pid(r, &result, pid);
        if (pin)
            loading_owner_pin_pid(owner, pid);
    }
}

static pids_t result_pids(scenario_run_t *r, jval_t *v) {
    bytes_t b = unhex(r, get(v, "result"));
    const unsigned char *p = b.data;
    pids_t result = {0};
    size_t begin = 0;
    for (size_t i = 0; i <= b.size; i++)
        if (i == b.size || p[i] == '\n') {
            size_t n = i - begin;
            while (n && p[begin + n - 1] == '\r')
                n--;
            if (n) {
                uint64_t pid;
                REQUIRE(decimal((bytes_t){p + begin, n}, &pid));
                add_pid(r, &result, pid);
            }
            begin = i + 1;
        }
    return result;
}

static void gone(loading_owner_t *observer, pids_t *p) {
    uint64_t end = process_watch_now_ms() + 6000;
    for (size_t i = 0; i < p->count; i++) {
        uint64_t now = process_watch_now_ms();
        loading_owner_pid_gone(observer, p->values[i], end > now ? end - now : 0);
    }
}

static void alive(scenario_run_t *r, loading_owner_t *observer, pids_t *p) {
    for (size_t i = 0; i < p->count; i++)
        REQUIRE(loading_owner_pid_alive(observer, p->values[i]));
}

static void owner_case(suite_t *s) {
    scenario_run_t *r = s->r;
    const char *kind = s->c->kind;
    loading_owner_options_t options = {0};
    if (!strcmp(kind, "owner-group-term")) {
#ifdef _WIN32
        puts("SKIP: POSIX process-group signal scenario");
        s->c->skipped = true;
        return;
#else
        options.new_session = true;
#endif
    }
    const char *mode = !strcmp(kind, "parent-death")         ? "watch-owner"
                       : !strncmp(kind, "launch-death-", 13) ? "launch-owner"
                                                             : "owner";
    loading_owner_t *owner = start(s, mode, NULL, 0, true, &options);
    if (!strcmp(kind, "parent-death")) {
        ready(s, owner, "WATCH_READY", false);
        loading_owner_terminate(owner, false, 8000);
        loading_bytes_t line;
        REQUIRE(loading_owner_next_line(owner, 8000, &line));
        scenario_require_bytes(r,
                               "stable parent death notification",
                               "PARENT_GONE\n",
                               12,
                               line.data,
                               line.size,
                               false);
        REQUIRE(!loading_owner_next_line(owner, 8000, &line));
        loading_owner_wait_eof(owner, 8000);
        return;
    }
    if (!strncmp(kind, "launch-death-", 13)) {
        ready(s, owner, "LAUNCHING", false);
        loading_owner_terminate(owner, false, 8000);
        pids_t observed = {0};
        loading_bytes_t line;
        while (loading_owner_next_line(owner, 8000, &line)) {
            bytes_t b = strip_line(line);
            const unsigned char *p = b.data;
            size_t begin = 0;
            while (begin < b.size && isspace(p[begin]))
                begin++;
            while (b.size > begin && isspace(p[b.size - 1]))
                b.size--;
            uint64_t pid;
            if (decimal((bytes_t){p + begin, b.size - begin}, &pid))
                add_pid(r, &observed, pid);
        }
        gone(owner, &observed);
        loading_owner_wait_eof(owner, 8000);
        return;
    }
    pids_t owned = ready(s, owner, "READY", true);
    if (!strcmp(kind, "owner-group-term")) {
        loading_owner_terminate(owner, true, 8000);
        gone(owner, &owned);
        loading_bytes_t line;
        REQUIRE(!loading_owner_next_line(owner, 8000, &line));
        loading_owner_wait_eof(owner, 8000);
        return;
    }
    loading_owner_t *other = start(s, "owner", NULL, 0, true, NULL);
    pids_t independent = ready(s, other, "READY", true);
    REQUIRE(owned.count == 3 && independent.count == 3);
    const char *modes[] = {"tree-exit", "tree-fail", "tree-crash", "tree"};
    for (size_t i = 0; i < 4; i++) {
        jval_t *v = run_fixture(s, modes[i]);
        pids_t terminated = result_pids(r, v);
        gone(owner, &terminated);
        alive(r, owner, &owned);
        alive(r, other, &independent);
    }
    loading_owner_terminate(owner, false, 8000);
    gone(owner, &owned);
    loading_bytes_t line;
    REQUIRE(!loading_owner_next_line(owner, 8000, &line));
    alive(r, other, &independent);
    loading_owner_terminate(other, false, 8000);
    gone(other, &independent);
    loading_owner_wait_eof(owner, 8000);
    loading_owner_wait_eof(other, 8000);
}

static void extra_case(suite_t *s) {
    scenario_run_t *r = s->r;
    const char *kind = s->c->kind;
    if (!strcmp(kind, "compiler-probe")) {
        loading_owner_t *owner = start(s, kind, NULL, 0, true, NULL);
        loading_owner_command_wait(owner, 0, 8000, true);
        return;
    }
    const char *names[] = {"zero", "negative", "decimal", "empty", "plus", "overflow", "huge"},
               *values[] = {"0", "-1", "1.5", "", "+2", "4294967296", "999999999999999999999"};
    const char *value = NULL;
    for (size_t i = 0; i < 7; i++)
        if (!strcmp(kind + 16, names[i]))
            value = values[i];
    REQUIRE(value);
    loading_owner_t *owner = loading_owner_command_start(
        r,
        s->root,
        owner_name(s, kind),
        ARGS(s->c->goat, "--native-probe-timeout", value, "missing.goat"),
        s->env,
        NULL,
        0,
        true);
    loading_owner_reap(owner, 8000);
    loading_owner_wait_eof(owner, 8000);
    const child_result_t *result = loading_owner_command_poll(owner);
    REQUIRE(result->status == CHILD_EXITED && result->exit_code != 0);
    loading_bytes_t error = loading_owner_error(owner);
    const unsigned char *p = error.data;
    bool found = false;
    for (size_t i = 0; i + 7 <= error.size; i++) {
        bool match = true;
        for (size_t j = 0; j < 7; j++)
            match &= tolower(p[i + j]) == "timeout"[j];
        found |= match;
    }
    REQUIRE(found);
}

static void body(scenario_run_t *r, void *argument) {
    config_t *c = argument;
    suite_t s = {.r = r, .c = c, .root = scenario_work_directory(scenario_run_context(r))};
    s.env = language_environment_override(r, "TMPDIR", s.root);
    if (!strncmp(c->kind, "stdin-", 6))
        stdin_case(&s);
    else if (!strncmp(c->kind, "owner-", 6) || !strcmp(c->kind, "parent-death")
             || !strncmp(c->kind, "launch-death-", 13))
        owner_case(&s);
    else if (!strcmp(c->kind, "compiler-probe") || !strncmp(c->kind, "invalid-timeout-", 16))
        extra_case(&s);
    else
        basic(&s);
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
    if (argc == 6 || (argc == 7 && !strcmp(argv[6], "--keep-work"))) {
        char *paths[6] = {0};
        bool valid = true;
        for (size_t i = 2; i < 6; i++) {
            fs_error_t e = {0};
            paths[i] = absolute_fs_path(argv[i], &e);
            clear_fs_error(&e);
            valid &= paths[i] != NULL;
        }
        const char *names[] = {"empty",
                               "exit",
                               "stdin",
                               "closed-input",
                               "inherit",
                               "parent",
                               "stdin-open-pipe",
                               "stdin-file",
                               "stdin-pty",
                               "capture-exact",
                               "capture-over",
                               "capture-large",
                               "arguments",
                               "missing",
                               "expired",
                               "stop",
                               "timeout-pause",
                               "timeout-continuous",
                               "timeout-budget",
                               "timeout-tree",
                               "crash",
                               "tree-exit",
                               "tree-fail",
                               "tree-crash",
                               "owner-isolation",
                               "owner-group-term",
                               "parent-death",
                               "launch-death-00",
                               "launch-death-01",
                               "launch-death-02",
                               "launch-death-03",
                               "launch-death-04",
                               "launch-death-05",
                               "launch-death-06",
                               "launch-death-07",
                               "launch-death-08",
                               "launch-death-09",
                               "launch-death-10",
                               "launch-death-11",
                               "launch-death-12",
                               "launch-death-13",
                               "launch-death-14",
                               "launch-death-15",
                               "launch-death-16",
                               "launch-death-17",
                               "launch-death-18",
                               "launch-death-19",
                               "compiler-probe",
                               "invalid-timeout-zero",
                               "invalid-timeout-negative",
                               "invalid-timeout-decimal",
                               "invalid-timeout-empty",
                               "invalid-timeout-plus",
                               "invalid-timeout-overflow",
                               "invalid-timeout-huge"};

        enum { COUNT = sizeof(names) / sizeof(names[0]) };

        config_t configs[COUNT];
        scenario_definition_t definitions[COUNT];
        char ids[COUNT][128];
        for (size_t i = 0; i < COUNT; i++) {
            snprintf(ids[i], sizeof(ids[i]), "integration.child_process.%s", names[i]);
            configs[i] = (config_t){paths[2], paths[3], names[i], false};
            definitions[i] =
                (scenario_definition_t){ids[i],
                                        !strcmp(names[i], "owner-isolation") ? 120000 : 30000,
                                        body,
                                        configs + i,
                                        NULL};
        }
        scenario_options_t options = {.build_directory = paths[4], .data_directory = paths[5]};
        const char *selection[] = {argv[1], argc == 7 ? "--keep-work" : NULL};
        if (valid)
            result =
                scenario_registry_main(definitions, COUNT, argc == 7 ? 2 : 1, selection, &options);
        if (!result)
            for (size_t i = 0; i < COUNT; i++)
                if (configs[i].skipped)
                    result = 77;
        for (size_t i = 2; i < 6; i++)
            FREE(paths[i]);
    } else
        fputs("usage: child_process_scenarios ID FIXTURE GOAT BUILD DATA [--keep-work]\n", stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
