/** @file interactive_cases.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Redirected interactive sessions, recovery and incompatible options.
 */
#include "lib/memory/allocate.h"
#include "test/support/language_execution.h"

#include <string.h>

static scenario_command_result_t
session(scenario_run_t *run, const char *exe, const char *input, const char *a, const char *b) {
    const char *args[] = {exe, "--lang", "en", "--interactive", a, b, NULL};
    child_options_t options = {.executable = exe,
                               .arguments = args,
                               .stdin_data = input,
                               .stdin_size = strlen(input),
                               .empty_stdin = true};
    return scenario_command_capture(run, &options);
}

/* Normalize CRLF only, then apply precisely the old prompt/empty-line filter. */
static char *text(scenario_run_t *run, const void *bytes, size_t size) {
    char *result = language_owned(run, ALLOC(size + 1));
    const unsigned char *p = bytes;
    size_t n = 0;
    for (size_t i = 0; i < size; ++i) {
        scenario_require(run, p[i] != 0, "unexpected NUL in REPL output");
        if (p[i] == '\r' && i + 1 < size && p[i + 1] == '\n')
            continue;
        result[n++] = (char)p[i];
    }
    result[n] = 0;
    return result;
}

void interactive_cases(scenario_run_t *run, const char *exe) {
    static const struct {
        const char *name, *input, *output, *errors[2];
        bool no_optimization;
    } cases[] = {
        {"previous-line updates and for loop",
         "var x=1;\nx++;\nprintln(x=3);\nfor(;x<5;x++) println(x);\nprintln(x);\n",
         "3\n3\n4\n5",
         {NULL, NULL},
         false},
        {"implicit constant remains immutable",
         "x=1;\nx=2;\nprintln(x);\n",
         "1",
         {"PROPERTY_IS_CONSTANT", NULL},
         false},
        {"explicit constant remains immutable",
         "const x=7;\nx++;\nprintln(x);\n",
         "7",
         {"PROPERTY_IS_CONSTANT", NULL},
         false},
        {"closure identity after an exception",
         "const f=func(){return y;};\nthrow 1;\nvar y=42;\nprintln(f());\n",
         "42",
         {"Uncaught exception: 1", NULL},
         false},
        {"repeated exceptions and captured writes",
         "var x=1; const f=func(){x=x+1; return x;};\nthrow 0;\nprintln(f());\nthrow "
         "1;\nprintln(f());\n",
         "2\n3",
         {"Uncaught exception: 0", "Uncaught exception: 1"},
         false},
        {"nested closure survives collection",
         "const make=func(n){return func(){return n;};};\nconst f=make(42);\nthrow "
         "1;\nprintln(f());\n",
         "42",
         {"Uncaught exception: 1", NULL},
         false},
        {"exception inside a call preserves session",
         "var x=1; const f=func(){var t=3; throw t;};\nf();\nx++;\nprintln(x);\n",
         "2",
         {"Uncaught exception: 3", NULL},
         false},
        {"syntax error recovery",
         "var x=1;\nconst broken = ;\nx++;\nprintln(x);\n",
         "2",
         {"'<interactive>', 2.16:", NULL},
         false},
        {"blank lines count in diagnostics",
         "\n\n@\nprintln(9);\n",
         "9",
         {"'<interactive>', 3.1:", NULL},
         false},
        {"continuation diagnostics and recovery",
         "println(\\\nmissing + );\nprintln(9);\n",
         "9",
         {"'<interactive>', 2.9:", NULL},
         false},
        {"continuation keeps line comment boundaries",
         "var x=1; // comment \\\nx=2;\nprintln(x);\n",
         "2",
         {NULL, NULL},
         false},
        {"multiline function",
         "const f=func(n){ \\\nreturn n+1; \\\n};\nprintln(f(4));\n",
         "5",
         {NULL, NULL},
         false},
        {"Unicode session names and values",
         "var имя=\"Привет 😀\";\nprintln(имя);\n",
         "Привет 😀",
         {NULL, NULL},
         false},
        {"accumulated bytecode retains old function addresses",
         "const f=func(n){return n+1;};\nvar v0=0;\nvar v1=1;\nvar v2=2;\nvar v3=3;\nvar "
         "v4=4;\nvar v5=5;\nvar v6=6;\nvar v7=7;\nvar v8=8;\nvar v9=9;\nvar v10=10;\nvar "
         "v11=11;\nvar v12=12;\nvar v13=13;\nvar v14=14;\nvar v15=15;\nvar v16=16;\nvar "
         "v17=17;\nvar v18=18;\nvar v19=19;\nvar v20=20;\nvar v21=21;\nvar v22=22;\nvar "
         "v23=23;\nvar v24=24;\nvar v25=25;\nvar v26=26;\nvar v27=27;\nvar v28=28;\nvar "
         "v29=29;\nvar v30=30;\nvar v31=31;\nvar v32=32;\nvar v33=33;\nvar v34=34;\nvar "
         "v35=35;\nvar v36=36;\nvar v37=37;\nvar v38=38;\nvar v39=39;\nvar v40=40;\nvar "
         "v41=41;\nvar v42=42;\nvar v43=43;\nvar v44=44;\nvar v45=45;\nvar v46=46;\nvar "
         "v47=47;\nvar v48=48;\nvar v49=49;\nvar v50=50;\nvar v51=51;\nvar v52=52;\nvar "
         "v53=53;\nvar v54=54;\nvar v55=55;\nvar v56=56;\nvar v57=57;\nvar v58=58;\nvar "
         "v59=59;\nvar v60=60;\nvar v61=61;\nvar v62=62;\nvar v63=63;\nvar v64=64;\nvar "
         "v65=65;\nvar v66=66;\nvar v67=67;\nvar v68=68;\nvar v69=69;\nvar v70=70;\nvar "
         "v71=71;\nvar v72=72;\nvar v73=73;\nvar v74=74;\nvar v75=75;\nvar v76=76;\nvar "
         "v77=77;\nvar v78=78;\nvar v79=79;\nvar v80=80;\nvar v81=81;\nvar v82=82;\nvar "
         "v83=83;\nvar v84=84;\nvar v85=85;\nvar v86=86;\nvar v87=87;\nvar v88=88;\nvar "
         "v89=89;\nvar v90=90;\nvar v91=91;\nvar v92=92;\nvar v93=93;\nvar v94=94;\nvar "
         "v95=95;\nvar v96=96;\nvar v97=97;\nvar v98=98;\nvar v99=99;\nprintln(f(5));\n",
         "6",
         {NULL, NULL},
         false},
        {"EOF without newline", "println(8);", "8", {NULL, NULL}, false},
        {"empty input", "", "", {NULL, NULL}, false},
        {"explicit optimization none", "var x=1;\nx++;\nprintln(x);\n", "2", {NULL, NULL}, true},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
        scenario_command_result_t result = session(run,
                                                   exe,
                                                   cases[i].input,
                                                   cases[i].no_optimization ? "--optimize" : NULL,
                                                   cases[i].no_optimization ? "none" : NULL);
        scenario_require(run, result.exit_code == 0, cases[i].name);
        char *out = text(run, result.out, result.out_size);
        char *filtered = language_owned(run, ALLOC(result.out_size + 1));
        size_t length = 0;
        for (char *p = out; *p;) {
            char *end = strchr(p, '\n');
            if (end)
                *end = 0;
            if (!strncmp(p, "> ", 2))
                p += 2;
            if (!strncmp(p, "  ", 2))
                p += 2;
            if (*p) {
                if (length)
                    filtered[length++] = '\n';
                size_t n = strlen(p);
                memcpy(filtered + length, p, n);
                length += n;
            }
            if (!end)
                break;
            p = end + 1;
        }
        scenario_require_bytes(run,
                               cases[i].name,
                               cases[i].output,
                               strlen(cases[i].output),
                               filtered,
                               length,
                               false);
        char *err = text(run, result.err, result.err_size);
        size_t count = 0;
        for (char *p = err; *p;) {
            char *end = strchr(p, '\n');
            if (end)
                *end = 0;
            scenario_require(run,
                             count < 2 && cases[i].errors[count]
                                 && strstr(p, cases[i].errors[count]),
                             cases[i].name);
            ++count;
            if (!end)
                break;
            p = end + 1;
        }
        size_t expected = (cases[i].errors[0] != NULL) + (cases[i].errors[1] != NULL);
        scenario_require(run, count == expected, "REPL diagnostic line count");
        printf("[ ok ] %s\n", cases[i].name);
    }
    language_check(run,
                   session(run, exe, "print(\"x\");\nprintln(\"y\");\n", NULL, NULL),
                   0,
                   "> x\n> y\n> ",
                   10,
                   "",
                   0);
    const char *artifact =
        language_join(run, scenario_work_directory(scenario_run_context(run)), "unexpected.svg");
    const char *conflicts[][2] = {{"--compile", NULL},
                                  {"--run", NULL},
                                  {"--print-c", NULL},
                                  {"--save-c", NULL},
                                  {"--save-library", NULL},
                                  {"--print-source-code", NULL},
                                  {"--print-analysis", NULL},
                                  {"--save-analysis", artifact},
                                  {"--save-graph", artifact},
                                  {"--print-native", NULL},
                                  {"--save-native", artifact},
                                  {"--native", "required"}};
    for (size_t i = 0; i < sizeof(conflicts) / sizeof(*conflicts); ++i) {
        scenario_command_result_t result =
            session(run, exe, "println(\"must not run\");\n", conflicts[i][0], conflicts[i][1]);
        scenario_require(run,
                         result.exit_code != 0 && !result.out_size,
                         "interactive conflicting option accepted");
        char *err = text(run, result.err, result.err_size);
        scenario_require(run,
                         strstr(err, "The --interactive mode cannot be combined") != NULL,
                         "interactive conflict diagnostic missing");
        scenario_require(run,
                         !language_exists(run, artifact),
                         "interactive conflict created artifact");
    }
}
