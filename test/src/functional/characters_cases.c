/** @file characters_cases.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Character scalar, round-trip and native uint32 storage regressions.
 */
#include "test/support/language_execution.h"

#include <string.h>
#ifndef CHARACTER_CC
#    define CHARACTER_CC "gcc"
#endif
#ifndef CHARACTER_ROOT
#    define CHARACTER_ROOT "."
#endif
#define RUN(...)                                                                                   \
    language_command(run, exe, (const char *const[]){exe, "--lang", "en", __VA_ARGS__, NULL}, NULL)
#define CHECK(r, s) language_check(run, r, 0, s, strlen(s), "", 0)

void characters_cases(scenario_run_t *run, const char *exe) {
    const char *work = scenario_work_directory(scenario_run_context(run));
    const char *source = language_join(run, work, "unicode.goat");
    const char *code = "const zero=chr(0); println(ord(zero));\n"
                       "const c='😀'; println(c); println(c + \"x\");\n"
                       "println(c + \"\" == \"😀\"); println(ord(c));\n"
                       "const high=chr(4294967295); println(ord(high));\n";
    const char *expected = "0\n😀\n😀x\ntrue\n128512\n4294967295\n";
    language_write(run, source, code, strlen(code));
    for (int i = 0; i < 2; ++i) {
        const char *opt = i ? "all" : "none";
        CHECK(RUN("--optimize", opt, source), expected);
        scenario_command_result_t printed =
            RUN("--compile", "--optimize", opt, "--print-source-code", source);
        scenario_require(run,
                         printed.exit_code == 0 && !printed.err_size,
                         "character regeneration failed");
        const char *restored =
            language_join(run, work, i ? "regenerated-all.goat" : "regenerated-none.goat");
        language_write(run, restored, printed.out, printed.out_size);
        CHECK(RUN(restored), expected);
        CHECK(RUN("--compile", "--optimize", opt, source), "");
        CHECK(RUN("--run", "--native", "off", language_join(run, work, "unicode.gbin")), expected);
    }
    const char *native = language_join(run, work, "native.goat");
    code = "const f=func(n){return ord(chr(n));};\n"
           "println(f(0));\nprintln(f(65));\nprintln(f(65535));\nprintln(f(65536));\n"
           "println(f(128512));\nprintln(f(1114111));\nprintln(f(2147483648));\n"
           "println(f(4294967295));\nprintln(f(-1));\n";
    expected = "0\n65\n65535\n65536\n128512\n1114111\n2147483648\n4294967295\n4294967295\n";
    language_write(run, native, code, strlen(code));
    CHECK(RUN("--optimize", "none", native), expected);
    const char *report = language_join(run, work, "native.report");
    CHECK(RUN("--native", "required", "--save-native", report, native), expected);
    size_t size;
    const char *fields = language_read(run, report, &size);
    /* Report reader returns binary data; do not assume a terminating NUL. */
    bool succeeded = false, retries = false;
    for (size_t i = 0; i < size; ++i) {
        if (i && fields[i - 1] != '\n')
            continue;
        if (size - i >= 12 && !memcmp(fields + i, "succeeded=9\n", 12))
            succeeded = true;
        if (size - i >= 10 && !memcmp(fields + i, "retries=0\n", 10))
            retries = true;
        if (size - i >= 13 && !memcmp(fields + i, "succeeded=9\r\n", 13))
            succeeded = true;
        if (size - i >= 11 && !memcmp(fields + i, "retries=0\r\n", 11))
            retries = true;
    }
    scenario_require(run, succeeded && retries, "character native execution/report mismatch");
    CHECK(RUN("--compile", "--native", "required", native), "");
    CHECK(RUN("--run", "--native", "required", language_join(run, work, "native.gbin")), expected);
    CHECK(RUN("--save-c", native), "");
    const char *host = language_join(run, work, "host.c");
    code =
        "#include \"native.c\"\n#include \"test_allocator.h\"\n"
        "int main(void) {\n"
        "goat_native_context_v2_t allocator=test_context();\n"
        "const goat_native_module_v2_t *m=goat_native_query_v2(GOAT_NATIVE_ABI_VERSION);\n"
        "if (!m || m->entry_count != 1) return 1;\n"
        "int64_t values[]={0,65,65535,65536,128512,1114111,2147483648LL,4294967295LL,-1};\n"
        "for(unsigned i=0;i<sizeof(values)/sizeof(*values);i++){\n"
        "goat_native_value_v2_t a={0},r={0}; a.type=GOAT_NATIVE_I64; a.value.integer=values[i];\n"
        "if(m->entries[0].invoke(GOAT_NATIVE_ABI_VERSION,&allocator,1,&a,&r)!=GOAT_NATIVE_OK) "
        "return 2;\n"
        "if(r.type!=GOAT_NATIVE_I64 || r.value.integer!=(int64_t)(uint32_t)values[i]) return 3;\n"
        "}return 0;}\n";
    language_write(run, host, code, strlen(code));
    for (int i = 0; i < 2; ++i) {
        const char *binary = language_join(run, work, i ? "short-wchar.exe" : "host.exe");
        const char *args[] = {CHARACTER_CC,
                              "-std=c11",
                              "-I" CHARACTER_ROOT "/include",
                              "-I" CHARACTER_ROOT "/test/src/functional/native_abi",
                              i ? "-fshort-wchar" : "-Wall",
                              host,
                              "-o",
                              binary,
                              NULL};
        scenario_command_result_t built = language_command(run, CHARACTER_CC, args, NULL);
        scenario_require(run, built.exit_code == 0, "character native host compilation failed");
        CHECK(language_command(run, binary, (const char *const[]){binary, NULL}, NULL), "");
    }
}
