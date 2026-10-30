/** @file module_loading_lifecycle.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Real pipe barriers, independent process death and cleaner lifetime contracts.
 */
#include "module_loading_lifecycle.h"

#include "lib/files/file_system.h"
#include "test/support/language_execution.h"
#include "test/support/module_loading_owner.h"

#include <string.h>
#define REQUIRE(x) scenario_require(r, (x), #x)

typedef enum { FROZEN, CONTINUED, LATE, NONRETURNING, KILLED, HELD } lifecycle_kind_t;

typedef struct {
    const char *name;
    lifecycle_kind_t kind;
    const char *mode;
    const char *native_kind;
    bool explicit_exit;
} lifecycle_case_t;

static const lifecycle_case_t cases[] = {
    {"frozen", FROZEN, "required", NULL, false},
    {"continued-auto", CONTINUED, "auto", NULL, false},
    {"continued-auto-exit", CONTINUED, "auto", NULL, true},
    {"continued-required", CONTINUED, "required", NULL, false},
    {"continued-required-exit", CONTINUED, "required", NULL, true},
    {"late-worker", LATE, "required", NULL, false},
    {"nonreturning-loop-auto", NONRETURNING, "auto", "nonreturning-loop", true},
    {"nonreturning-loop-required", NONRETURNING, "required", "nonreturning-loop", true},
    {"nonreturning-block-auto", NONRETURNING, "auto", "nonreturning-block", true},
    {"nonreturning-block-required", NONRETURNING, "required", "nonreturning-block", true},
    {"killed-owner", KILLED, "required", NULL, false},
    {"held-auto", HELD, "auto", NULL, false},
    {"held-required", HELD, "required", NULL, false},
};

size_t loading_lifecycle_case_count(void) {
    return sizeof(cases) / sizeof(cases[0]);
}

const char *loading_lifecycle_case_name(size_t index) {
    return cases[index].name;
}

static void loaded(scenario_run_t *r, yyjson_val *result) {
    yyjson_val *nodes = yyjson_obj_get(result, "nodes");
    REQUIRE(yyjson_is_arr(nodes) && yyjson_arr_size(nodes) > 0);
    yyjson_val *first = yyjson_arr_get(nodes, 0);
    REQUIRE(yyjson_is_obj(first));
    REQUIRE(yyjson_is_true(yyjson_obj_get(first, "loaded")));
}

static void remove_directory(scenario_run_t *r, const char *path) {
    fs_error_t error = {0};
    bool ok = remove_fs_tree(path, &error);
    clear_fs_error(&error);
    REQUIRE(ok);
}

void loading_lifecycle_case_run(scenario_run_t *r,
                                loading_fixture_t *fixture,
                                const char *runner,
                                size_t index) {
    REQUIRE(index < loading_lifecycle_case_count());
    const lifecycle_case_t *c = &cases[index];
    const char *root = loading_fixture_root(fixture);
    const char *archive;
    loading_owner_t *owner;
    process_watch_t *cleaner;
    const char *directory;
    yyjson_val *result;
    if (c->kind == FROZEN) {
        const char *valid = loading_custom(fixture, "valid", loading_base(fixture), "valid", false);
        archive = language_join(r, root, "replace.gbin");
        size_t size;
        const char *bytes = language_read(r, valid, &size);
        language_write(r, archive, bytes, size);
        owner = loading_owner_start(r, runner, root, c->name, archive, c->mode, "frozen", NULL);
        loading_owner_wait_line(owner, "READY");
        const char changed[] = "changed after snapshot";
        language_write(r, archive, changed, sizeof(changed) - 1);
        loading_owner_resume(owner);
        loading_owner_end(owner, 0, true, false);
        loading_owner_expect_output(owner, "READY\n49\n64\n");
        loading_owner_expect_calls(owner, "LCC");
    } else if (c->kind == CONTINUED) {
        archive = loading_build(
            fixture,
            c->name,
            c->explicit_exit
                ? "external f=func(x){return x*x;}; println(f(7)); println(\"READY\"); "
                  "input(); println(f(8));exit(23);"
                : "external f=func(x){return x*x;}; println(f(7)); println(\"READY\"); "
                  "input(); println(f(8));",
            "required",
            NULL);
        owner = loading_owner_start(r, runner, root, c->name, archive, c->mode, "run", NULL);
        loading_owner_wait_line(owner, "49");
        loading_owner_wait_line(owner, "READY");
        cleaner = loading_owner_watch_cleaner(owner);
        directory = loading_owner_extraction(owner);
        loading_owner_watch_kill(owner, cleaner);
        loading_owner_watch_wait(owner, cleaner);
        loading_owner_resume(owner);
        result = loading_owner_end(owner, c->explicit_exit ? 23 : 0, false, c->explicit_exit);
        loading_owner_expect_output(owner, "49\nREADY\n64\n");
        if (!c->explicit_exit)
            loaded(r, result);
        loading_owner_require_directory(owner, directory, false);
        remove_directory(r, directory);
    } else if (c->kind == LATE) {
        archive =
            loading_graft(fixture,
                          "late-worker",
                          "external f=func(x){return x*x;}; "
                          "const t=spawn(func(){input();println(f(7));}); println(\"ENTRY-END\");");
        owner =
            loading_owner_start(r, runner, root, c->name, archive, c->mode, "late-worker", NULL);
        loading_owner_wait_line(owner, "ENTRY-END");
        loading_owner_wait_line(owner, "ENTRY-FINISHED");
        loading_owner_absent(owner,
                             language_join(r, loading_owner_directory(owner), "cleaner.pid"));
        loading_owner_expect_calls(owner, "");
        loading_owner_resume(owner);
        result = loading_owner_end(owner, 0, true, false);
        loading_owner_expect_output(owner, "ENTRY-END\nENTRY-FINISHED\n49\n");
        loading_owner_expect_calls(owner, "LC");
        loaded(r, result);
    } else if (c->kind == NONRETURNING) {
        const char *base = loading_graft(fixture,
                                         "nonreturning",
                                         "external f=func(x){return x*x;}; "
                                         "spawn(f,7); println(\"EXIT-READY\"); input(); "
                                         "try { exit(23); } finally { println(\"UNREACHABLE\"); }");
        archive = loading_custom(fixture, c->native_kind, base, c->native_kind, false);
        owner = loading_owner_start(r, runner, root, c->name, archive, c->mode, "run", NULL);
        loading_owner_wait_line(owner, "EXIT-READY");
        loading_owner_wait_calls(owner, "LC");
        cleaner = loading_owner_watch_cleaner(owner);
        directory = loading_owner_extraction(owner);
        loading_owner_watch_alive(owner, cleaner);
        loading_owner_watch_alive(owner, loading_owner_watch_process(owner));
        loading_owner_resume(owner);
        loading_owner_end(owner, 23, false, true);
        loading_owner_expect_output(owner, "EXIT-READY\n");
        loading_owner_expect_calls(owner, "LC");
        loading_owner_watch_wait(owner, cleaner);
        REQUIRE(!strcmp(loading_owner_mark(owner, "cleaner.done"), "done"));
        loading_owner_absent(owner, directory);
    } else {
        archive = loading_custom(fixture, "valid", loading_base(fixture), "valid", false);
        owner = loading_owner_start(r, runner, root, c->name, archive, c->mode, "hold", NULL);
        loading_owner_wait_line(owner, "49");
        loading_owner_wait_line(owner, "64");
        loading_owner_wait_line(owner, "HOLD");
        directory = loading_owner_mark(owner, "directory");
        cleaner = loading_owner_watch_cleaner(owner);
        loading_owner_require_directory(owner, directory, c->kind == HELD);
        if (c->kind == KILLED) {
            loading_owner_kill_and_reap(owner);
            loading_owner_watch_wait(owner, cleaner);
            loading_owner_wait_absent(owner, directory);
        } else {
            loading_owner_watch_alive(owner, cleaner);
            loading_owner_watch_kill(owner, cleaner);
            loading_owner_watch_wait(owner, cleaner);
            loading_owner_require_directory(owner, directory, false);
            loading_owner_resume(owner);
            result = loading_owner_end(owner, 0, false, false);
            yyjson_val *status = yyjson_obj_get(result, "status");
            REQUIRE(yyjson_is_int(status) && yyjson_get_int(status) == 0);
            loading_owner_expect_calls(owner, "LCC");
            remove_directory(r, directory);
        }
    }
}
