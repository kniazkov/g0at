/** @file module_loading_cases.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Real lazy-loading, terminal failure and per-module isolation contracts (#369).
 */
#include "module_loading_cases.h"

#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/language_execution.h"
#include "test/support/module_loading_owner.h"
#include "yyjson.h"

#include <string.h>
#define REQUIRE(x) scenario_require(r, (x), #x)

typedef enum {
    OFF,
    VALID,
    NO_MATCH,
    PLAIN,
    FOREIGN,
    AMBIGUOUS,
    DAMAGED,
    BAD_LIBRARY,
    BAD_ABI,
    WRITE,
    CLEANER,
    PRODUCER,
    UNICODE,
    STRING,
    DORMANT,
    CONCURRENT,
    DEPENDENCY_VALID,
    IMPORT_ONLY,
    DEPENDENCY_ONLY,
    DEPENDENCY_OPTIONAL,
    DEPENDENCY_FAILURE,
    DEPENDENCY_CORRUPT,
    ISOLATED,
    GLOBAL,
    INTEGRITY,
    ALL_SYMBOLS,
    PENDING,
    CONCURRENT_FAILURE,
    CAUGHT
} case_kind_t;

typedef struct {
    const char *name;
    case_kind_t kind;
    const char *mode, *detail;
} case_t;

static const case_t cases[] = {
    {"off", OFF, "off", ""},
    {"valid-auto", VALID, "auto", ""},
    {"valid-required", VALID, "required", ""},
    {"no-match-auto", NO_MATCH, "auto", ""},
    {"no-match-required", NO_MATCH, "required", ""},
    {"plain-auto", PLAIN, "auto", ""},
    {"plain-required", PLAIN, "required", ""},
    {"foreign-auto", FOREIGN, "auto", ""},
    {"foreign-required", FOREIGN, "required", ""},
    {"ambiguous-auto", AMBIGUOUS, "auto", ""},
    {"ambiguous-required", AMBIGUOUS, "required", ""},
    {"ambiguous-off", AMBIGUOUS, "off", ""},
    {"damaged-off", DAMAGED, "off", ""},
    {"damaged-auto", DAMAGED, "auto", ""},
    {"damaged-required", DAMAGED, "required", ""},
    {"os-auto", BAD_LIBRARY, "auto", "os"},
    {"os-required", BAD_LIBRARY, "required", "os"},
    {"descriptor-missing-auto", BAD_LIBRARY, "auto", "descriptor-missing"},
    {"descriptor-missing-required", BAD_LIBRARY, "required", "descriptor-missing"},
    {"descriptor-mismatch-auto", BAD_LIBRARY, "auto", "descriptor-mismatch"},
    {"descriptor-mismatch-required", BAD_LIBRARY, "required", "descriptor-mismatch"},
    {"descriptor-overflow-auto", BAD_LIBRARY, "auto", "descriptor-overflow"},
    {"descriptor-overflow-required", BAD_LIBRARY, "required", "descriptor-overflow"},
    {"descriptor-short-auto", BAD_LIBRARY, "auto", "descriptor-short"},
    {"descriptor-short-required", BAD_LIBRARY, "required", "descriptor-short"},
    {"descriptor-writable-auto", BAD_LIBRARY, "auto", "descriptor-writable"},
    {"descriptor-writable-required", BAD_LIBRARY, "required", "descriptor-writable"},
    {"symbol-missing-auto", BAD_LIBRARY, "auto", "symbol-missing"},
    {"symbol-missing-required", BAD_LIBRARY, "required", "symbol-missing"},
    {"bad-status-auto", BAD_ABI, "auto", "bad-status"},
    {"bad-status-required", BAD_ABI, "required", "bad-status"},
    {"bad-result-auto", BAD_ABI, "auto", "bad-result"},
    {"bad-result-required", BAD_ABI, "required", "bad-result"},
    {"write-before-auto", WRITE, "auto", "write-before"},
    {"write-before-required", WRITE, "required", "write-before"},
    {"write-partial-auto", WRITE, "auto", "write-partial"},
    {"write-partial-required", WRITE, "required", "write-partial"},
    {"cleaner-exit-auto", CLEANER, "auto", "exit"},
    {"cleaner-exit-required", CLEANER, "required", "exit"},
    {"cleaner-silent-auto", CLEANER, "auto", "silent"},
    {"cleaner-silent-required", CLEANER, "required", "silent"},
    {"real-producer-auto", PRODUCER, "auto", ""},
    {"real-producer-required", PRODUCER, "required", ""},
    {"unicode", UNICODE, "required", ""},
    {"allocated-string-auto", STRING, "auto", "allocated-string"},
    {"allocated-string-required", STRING, "required", "allocated-string"},
    {"static-string-auto", STRING, "auto", "static-string"},
    {"static-string-required", STRING, "required", "static-string"},
    {"dormant", DORMANT, "required", ""},
    {"concurrent-auto", CONCURRENT, "auto", ""},
    {"concurrent-required", CONCURRENT, "required", ""},
    {"dependency-valid-auto", DEPENDENCY_VALID, "auto", ""},
    {"dependency-valid-required", DEPENDENCY_VALID, "required", ""},
    {"import-only", IMPORT_ONLY, "required", ""},
    {"dependency-only", DEPENDENCY_ONLY, "required", ""},
    {"dependency-bytecode", DEPENDENCY_OPTIONAL, "required", "bytecode"},
    {"dependency-foreign", DEPENDENCY_OPTIONAL, "required", "foreign"},
    {"dep-failure-os-auto", DEPENDENCY_FAILURE, "auto", "os"},
    {"dep-failure-os-required", DEPENDENCY_FAILURE, "required", "os"},
    {"dep-failure-symbol-missing-auto", DEPENDENCY_FAILURE, "auto", "symbol-missing"},
    {"dep-failure-symbol-missing-required", DEPENDENCY_FAILURE, "required", "symbol-missing"},
    {"dep-failure-descriptor-mismatch-auto", DEPENDENCY_FAILURE, "auto", "descriptor-mismatch"},
    {"dep-failure-descriptor-mismatch-required",
     DEPENDENCY_FAILURE,
     "required",
     "descriptor-mismatch"},
    {"foreign-corrupt-dependency-off", DEPENDENCY_CORRUPT, "off", ""},
    {"foreign-corrupt-dependency-auto", DEPENDENCY_CORRUPT, "auto", ""},
    {"foreign-corrupt-dependency-required", DEPENDENCY_CORRUPT, "required", ""},
    {"isolated-write-before", ISOLATED, "auto", "write-before"},
    {"isolated-write-partial", ISOLATED, "auto", "write-partial"},
    {"global-exit", GLOBAL, "auto", "exit"},
    {"global-silent", GLOBAL, "auto", "silent"},
    {"integrity-auto", INTEGRITY, "auto", ""},
    {"integrity-required", INTEGRITY, "required", ""},
    {"all-symbols", ALL_SYMBOLS, "auto", ""},
    {"pending-exit-write-auto", PENDING, "auto", "exit-write"},
    {"pending-exit-write-required", PENDING, "required", "exit-write"},
    {"pending-exit-write-fail-auto", PENDING, "auto", "exit-write-fail"},
    {"pending-exit-write-fail-required", PENDING, "required", "exit-write-fail"},
    {"concurrent-write-fail", CONCURRENT_FAILURE, "auto", "write-partial"},
    {"concurrent-cleaner-fail", CONCURRENT_FAILURE, "auto", "silent"},
    {"caught-auto", CAUGHT, "auto", ""},
    {"caught-required", CAUGHT, "required", ""},
};

static uint64_t number(scenario_run_t *r, yyjson_val *obj, const char *key) {
    yyjson_val *value = yyjson_obj_get(obj, key);
    REQUIRE(yyjson_is_uint(value));
    return yyjson_get_uint(value);
}

static bool flag(scenario_run_t *r, yyjson_val *obj, const char *key) {
    yyjson_val *value = yyjson_obj_get(obj, key);
    REQUIRE(yyjson_is_bool(value));
    return yyjson_get_bool(value);
}

static yyjson_val *node(scenario_run_t *r, yyjson_val *result, size_t index) {
    yyjson_val *nodes = yyjson_obj_get(result, "nodes");
    REQUIRE(yyjson_is_arr(nodes) && yyjson_arr_size(nodes) > index);
    yyjson_val *value = yyjson_arr_get(nodes, index);
    REQUIRE(yyjson_is_obj(value));
    return value;
}

static void no_directory(scenario_run_t *r, yyjson_val *result) {
    yyjson_val *value = yyjson_obj_get(result, "directory");
    REQUIRE(yyjson_is_str(value) && yyjson_get_len(value) == 0);
}

static void no_cleaner(scenario_run_t *r, loading_owner_t *owner) {
    loading_owner_absent(owner, language_join(r, loading_owner_directory(owner), "cleaner.pid"));
}

static void no_native_call(scenario_run_t *r, loading_owner_t *owner) {
    loading_bytes_t calls = loading_owner_calls(owner);
    REQUIRE(!calls.size || !memchr(calls.data, 'C', calls.size));
}

static const char *copy(scenario_run_t *r, const char *from, const char *to) {
    size_t size;
    const char *bytes = language_read(r, from, &size);
    language_write(r, to, bytes, size);
    return to;
}

static const char *sibling(scenario_run_t *r, const char *archive, const char *relative) {
    const char *end = strrchr(archive, '/'), *backslash = strrchr(archive, '\\');
    if (backslash && (!end || backslash > end))
        end = backslash;
    REQUIRE(end && end > archive);
    size_t size = (size_t)(end - archive);
    char *directory = language_owned(r, ALLOC(size + 1));
    memcpy(directory, archive, size);
    directory[size] = 0;
    return language_join(r, directory, relative);
}

static const char *valid(loading_fixture_t *f) {
    return loading_custom(f, "valid", loading_base(f), "valid", false);
}

static const char concurrent_source[] =
    "external f=func(x){return x*x;}; "
    "const a=spawn(f,7); const b=spawn(f,8); println(join(a)); println(join(b));";

static const char *dependency(loading_fixture_t *f) {
    return loading_graft(f, "dependency", "external f=func(x){return x*x;};");
}

static const char *consumer(loading_fixture_t *f, const char *dep) {
    return loading_build(f,
                         "consumer",
                         "external own=func(x){return x*x;}; println(own(5)); "
                         "const dep=import \"dependency@1\"; println(dep.f(7)); println(dep.f(8));",
                         "required",
                         dep);
}

size_t loading_normal_case_count(void) {
    return sizeof(cases) / sizeof(cases[0]);
}

const char *loading_normal_case_name(size_t index) {
    return index < loading_normal_case_count() ? cases[index].name : NULL;
}

void loading_normal_case_run(scenario_run_t *r,
                             loading_fixture_t *f,
                             const char *runner,
                             size_t index) {
    REQUIRE(index < loading_normal_case_count());
    const case_t *c = cases + index;
    const char *archive = NULL, *output = "49\n64\n", *fault = "run", *cleaner_fault = NULL,
               *owner_name = c->name;
    bool required = !strcmp(c->mode, "required");
    int code = 0;
    switch (c->kind) {
        case OFF:
        case VALID:
            archive = valid(f);
            break;
        case NO_MATCH:
            archive = loading_custom(f, "no-match", loading_base(f), "valid", true);
            break;
        case PLAIN:
        case FOREIGN:
        case AMBIGUOUS:
        case DAMAGED: {
            const char *kind = c->kind == PLAIN       ? "plain"
                               : c->kind == FOREIGN   ? "foreign"
                               : c->kind == AMBIGUOUS ? "ambiguous"
                                                      : "damaged";
            archive = loading_transform(f, kind, loading_base(f), kind);
            if (c->kind == DAMAGED)
                code = 2;
            else if (required || (c->kind == AMBIGUOUS && !strcmp(c->mode, "auto")))
                code = 3;
            if (code)
                output = "";
            break;
        }
        case BAD_LIBRARY:
            archive = !strcmp(c->detail, "os")
                          ? loading_transform(f, "invalid-os", loading_base(f), "invalid-os")
                          : loading_custom(f, c->detail, loading_base(f), c->detail, false);
            if (required) {
                code = 1;
                output = "";
            }
            break;
        case BAD_ABI:
            archive = loading_custom(f, c->detail, loading_base(f), c->detail, false);
            code = 1;
            output = "";
            break;
        case WRITE:
        case CLEANER:
            archive = valid(f);
            if (c->kind == WRITE)
                fault = c->detail;
            else
                cleaner_fault = c->detail;
            if (required) {
                code = 1;
                output = "";
            }
            break;
        case PRODUCER:
            archive = loading_base(f);
            break;
        case UNICODE:
            archive = copy(r,
                           loading_base(f),
                           language_join(r, loading_fixture_root(f), "модуль-😀.gbin"));
            owner_name = "unicode-данные-😀";
            break;
        case STRING:
            if (!strcmp(c->detail, "allocated-string")) {
                archive = loading_build(
                    f,
                    c->detail,
                    "external text=func(x){return x+\"😀\";}; println(text(\"abc\"));",
                    "required",
                    NULL);
                output = "abc😀\n";
            } else {
                archive = loading_build(
                    f,
                    c->detail,
                    "external text=func(x){return \"literal 😀\";}; println(text(\"abc\"));",
                    "required",
                    NULL);
                output = "literal 😀\n";
            }
            break;
        case DORMANT:
            archive =
                loading_graft(f, "dormant", "external f=func(x){return x*x;}; println(\"DONE\");");
            output = "DONE\n";
            break;
        case CONCURRENT:
        case CONCURRENT_FAILURE:
            archive = loading_graft(f, "concurrent", concurrent_source);
            if (c->kind == CONCURRENT_FAILURE) {
                if (!strcmp(c->detail, "silent"))
                    cleaner_fault = "silent";
                else
                    fault = c->detail;
            }
            break;
        case DEPENDENCY_VALID:
        case IMPORT_ONLY:
        case DEPENDENCY_ONLY:
        case DEPENDENCY_OPTIONAL:
        case DEPENDENCY_FAILURE:
        case DEPENDENCY_CORRUPT:
        case ISOLATED:
        case GLOBAL: {
            const char *dep = dependency(f);
            if (c->kind == IMPORT_ONLY) {
                archive = loading_build(f,
                                        "import-only",
                                        "external own=func(x){return x*x;}; println(own(5)); "
                                        "const dep=import \"dependency@1\"; println(\"IMPORTED\");",
                                        "required",
                                        dep);
                output = "25\nIMPORTED\n";
                break;
            }
            archive = consumer(f, dep);
            output = "25\n49\n64\n";
            if (c->kind == DEPENDENCY_ONLY) {
                const char *plain = loading_transform(f, "dependency-only", archive, "plain");
                archive = copy(r, plain, sibling(r, archive, "dependency-only.gbin"));
                code = 3;
                output = "";
            } else if (c->kind == DEPENDENCY_OPTIONAL || c->kind == DEPENDENCY_FAILURE
                       || c->kind == DEPENDENCY_CORRUPT) {
                const char *changed;
                if (c->kind == DEPENDENCY_OPTIONAL) {
                    changed =
                        loading_transform(f,
                                          "changed-dependency",
                                          dep,
                                          !strcmp(c->detail, "bytecode") ? "plain" : "foreign");
                } else if (c->kind == DEPENDENCY_CORRUPT) {
                    changed = loading_transform(f, "changed-dependency", dep, "foreign-damaged");
                    code = 2;
                    output = "";
                } else {
                    changed = !strcmp(c->detail, "os")
                                  ? loading_transform(f,
                                                      "changed-dependency",
                                                      dep,
                                                      "invalid-shared-library")
                                  : loading_custom(f, "changed-dependency", dep, c->detail, false);
                    if (required) {
                        code = 1;
                        output = "25\n";
                    }
                }
                copy(r, changed, sibling(r, archive, "modules/dependency/1.gbin"));
            } else if (c->kind == ISOLATED)
                fault = c->detail;
            else if (c->kind == GLOBAL)
                cleaner_fault = c->detail;
            break;
        }
        case INTEGRITY:
            archive = valid(f);
            fault = "write-corrupt";
            code = 1;
            output = "";
            break;
        case ALL_SYMBOLS: {
            const char *two =
                loading_build(f,
                              "two-symbols",
                              "external f=func(x){return x*x;}; external g=func(x){return x+1;}; "
                              "println(f(7)); println(g(8));",
                              "required",
                              NULL);
            archive = loading_custom(f, "missing-last", two, "symbol-missing", false);
            output = "49\n9\n";
            break;
        }
        case PENDING:
            archive = valid(f);
            fault = c->detail;
            code = 23;
            output = "";
            break;
        case CAUGHT: {
            const char *caught = loading_graft(
                f,
                "caught",
                "external f=func(x){return x*x;}; "
                "try { println(f(7)); } catch(e) { println(\"CAUGHT\"); } println(\"AFTER\");");
            archive = loading_custom(f, "caught-status", caught, "bad-status", false);
            code = 1;
            output = "";
            break;
        }
    }
    REQUIRE(archive);
    loading_owner_t *owner = loading_owner_start(r,
                                                 runner,
                                                 loading_fixture_root(f),
                                                 owner_name,
                                                 archive,
                                                 c->mode,
                                                 fault,
                                                 cleaner_fault);
    yyjson_val *result = loading_owner_end(owner, code, true, false);
    REQUIRE(yyjson_is_obj(result));
    loading_owner_expect_output(owner, output);
    switch (c->kind) {
        case OFF:
            loading_owner_expect_calls(owner, "");
            no_directory(r, result);
            REQUIRE(number(r, result, "writes") == 0);
            no_cleaner(r, owner);
            break;
        case VALID:
            loading_owner_expect_calls(owner, "LCC");
            REQUIRE(number(r, result, "writes") == 1 && number(r, result, "reports") == 0);
            REQUIRE(number(r, node(r, result, 0), "loads") == 1
                    && number(r, node(r, result, 0), "extractions") == 1
                    && flag(r, node(r, result, 0), "loaded"));
            break;
        case NO_MATCH:
        case DORMANT:
            loading_owner_expect_calls(owner, "");
            no_directory(r, result);
            REQUIRE(number(r, result, "writes") == 0);
            break;
        case PLAIN:
            if (required) {
                REQUIRE(number(r, result, "prepare") != 0);
                no_cleaner(r, owner);
            } else {
                REQUIRE(number(r, result, "writes") == 0);
                no_directory(r, result);
            }
            break;
        case FOREIGN:
            if (!required)
                REQUIRE(number(r, result, "writes") == 0);
            break;
        case AMBIGUOUS:
            break;
        case DAMAGED:
            REQUIRE(number(r, result, "graph") != 0);
            no_cleaner(r, owner);
            break;
        case BAD_LIBRARY:
            no_native_call(r, owner);
            if (!required) {
                REQUIRE(number(r, result, "reports") == 1 && number(r, result, "writes") == 1);
                REQUIRE(flag(r, node(r, result, 0), "unavailable")
                        && number(r, node(r, result, 0), "loads") == 1);
            }
            break;
        case BAD_ABI:
        case CAUGHT:
            loading_owner_expect_calls(owner, "LC");
            REQUIRE(flag(r, result, "terminal"));
            break;
        case WRITE:
            REQUIRE(number(r, result, "writes") == 1);
            loading_owner_expect_calls(owner, "");
            REQUIRE(number(r, node(r, result, 0), "loads") == 0);
            if (!required)
                REQUIRE(number(r, result, "reports") == 1);
            break;
        case CLEANER:
            REQUIRE(number(r, result, "writes") == 0);
            loading_owner_expect_calls(owner, "");
            REQUIRE(number(r, result, "reports") == (required ? 0 : 1));
            break;
        case PRODUCER:
        case STRING:
            REQUIRE(flag(r, node(r, result, 0), "loaded")
                    && number(r, node(r, result, 0), "loads") == 1);
            if (c->kind == STRING)
                REQUIRE(number(r, result, "reports") == 0 && !flag(r, result, "terminal"));
            break;
        case UNICODE:
            REQUIRE(flag(r, node(r, result, 0), "loaded") && number(r, result, "writes") == 1);
            break;
        case CONCURRENT:
            loading_owner_expect_calls(owner, "LCC");
            REQUIRE(number(r, result, "writes") == 1);
            REQUIRE(number(r, node(r, result, 0), "loads") == 1
                    && number(r, result, "native_successes") == 2);
            break;
        case DEPENDENCY_VALID: {
            yyjson_val *nodes = yyjson_obj_get(result, "nodes");
            REQUIRE(yyjson_is_arr(nodes) && yyjson_arr_size(nodes) == 2);
            REQUIRE(flag(r, node(r, result, 0), "loaded") && flag(r, node(r, result, 1), "loaded"));
            REQUIRE(number(r, result, "writes") == 2);
            loading_owner_expect_calls(owner, "LCC");
            break;
        }
        case IMPORT_ONLY:
            REQUIRE(number(r, result, "writes") == 1 && !flag(r, node(r, result, 1), "loaded"));
            loading_owner_expect_calls(owner, "");
            break;
        case DEPENDENCY_ONLY:
            loading_owner_expect_calls(owner, "");
            no_cleaner(r, owner);
            break;
        case DEPENDENCY_OPTIONAL:
            REQUIRE(number(r, result, "writes") == 1);
            loading_owner_expect_calls(owner, "");
            break;
        case DEPENDENCY_FAILURE:
            REQUIRE(flag(r, node(r, result, 0), "loaded")
                    && flag(r, node(r, result, 1), "unavailable"));
            REQUIRE(number(r, node(r, result, 1), "loads") == 1);
            no_native_call(r, owner);
            break;
        case DEPENDENCY_CORRUPT:
            REQUIRE(number(r, result, "graph") != 0);
            loading_owner_expect_calls(owner, "");
            no_cleaner(r, owner);
            break;
        case ISOLATED:
            REQUIRE(number(r, result, "reports") == 1 && number(r, result, "writes") == 2);
            REQUIRE(flag(r, node(r, result, 0), "unavailable")
                    && flag(r, node(r, result, 1), "loaded"));
            loading_owner_expect_calls(owner, "LCC");
            break;
        case GLOBAL:
            REQUIRE(number(r, result, "reports") == 1 && number(r, result, "writes") == 0);
            loading_owner_expect_calls(owner, "");
            break;
        case INTEGRITY:
            REQUIRE(flag(r, result, "terminal") && number(r, node(r, result, 0), "loads") == 0);
            loading_owner_expect_calls(owner, "");
            break;
        case ALL_SYMBOLS:
            no_native_call(r, owner);
            REQUIRE(number(r, result, "reports") == 1);
            break;
        case PENDING:
            no_native_call(r, owner);
            REQUIRE(!flag(r, result, "terminal"));
            break;
        case CONCURRENT_FAILURE:
            REQUIRE(number(r, result, "reports") == 1);
            loading_owner_expect_calls(owner, "");
            REQUIRE(number(r, result, "writes") == (!strcmp(c->detail, "silent") ? 0 : 1));
            break;
    }
}
