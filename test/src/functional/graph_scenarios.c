/** @file graph_scenarios.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Graph SVG artifact validation and adversarial XML controls (#393).
 */
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test/support/format_svg.h"
#include "test/support/language_execution.h"
#include "test/support/registry.h"

#include <stdlib.h>
#include <string.h>
#define REQUIRE(x) scenario_require(r, (x), #x)
#define N(a) (sizeof(a) / sizeof((a)[0]))

typedef struct {
    const char *input;
} config_t;

static bool release(void *p) {
    free(p);
    return true;
}

static bool white(unsigned c) {
    return (c >= 9 && c <= 13) || (c >= 0x1c && c <= 0x20) || c == 0x85 || c == 0xa0 || c == 0x1680
           || (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f
           || c == 0x205f || c == 0x3000;
}

/* Python str.strip() semantics, including its Unicode whitespace set. */
static bool output_is(const char *s, size_t n, const char *expected) {
    size_t first = n, last = n;
    for (size_t i = 0; i < n;) {
        size_t begin = i;
        unsigned c = (unsigned char)s[i++], more = 0, minimum = 0;
        if (c >= 0xc2 && c <= 0xdf) {
            c &= 31;
            more = 1;
            minimum = 128;
        } else if (c >= 0xe0 && c <= 0xef) {
            c &= 15;
            more = 2;
            minimum = 2048;
        } else if (c >= 0xf0 && c <= 0xf4) {
            c &= 7;
            more = 3;
            minimum = 65536;
        } else if (c >= 128)
            return false;
        if (more > n - i)
            return false;
        while (more--) {
            unsigned b = (unsigned char)s[i++];
            if ((b & 0xc0) != 0x80)
                return false;
            c = (c << 6) | (b & 63);
        }
        if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
            return false;
        if (!white(c)) {
            if (first == n)
                first = begin;
            last = i;
        }
    }
    return last - first == strlen(expected) && !memcmp(s + first, expected, last - first);
}

static bool contract(const format_svg_t *v, bool replacement) {
    static const char *fib[] = {"C view:", "(integer)", "C=supported"};
    static const char *repl[] = {"expression replacement",
                                 "statement replacement",
                                 "original",
                                 "replacement",
                                 "if-else"};
    const char *const *tokens = replacement ? repl : fib;
    size_t n = replacement ? N(repl) : N(fib);
    for (size_t i = 0; i < n; i++)
        if (!strstr(v->text, tokens[i]))
            return false;
    return replacement ? v->purple_pair : v->green_stroke && v->green_fill;
}

static const char *copy_input(scenario_run_t *r, config_t *c, const char *name, size_t *size) {
    const char *input = language_join(r, c->input, name);
    fs_error_t e = {0};
    fs_info_t info;
    bool ok = inspect_fs_path(input, false, &info, &e);
    clear_fs_error(&e);
    scenario_require(r, ok && info.kind == FS_FILE, name);
    const char *bytes = language_read(r, input, size);
    const char *saved = language_join(r, scenario_work_directory(scenario_run_context(r)), name);
    language_write(r, saved, bytes, *size);
    return language_read(r, saved, size);
}

static void validate(scenario_run_t *r,
                     const char *svg,
                     const char *output,
                     const char *expected,
                     bool replacement) {
    size_t n;
    const char *work = scenario_work_directory(scenario_run_context(r));
    const char *s = language_read(r, language_join(r, work, output), &n);
    scenario_require(r, output_is(s, n, expected), output);
    s = language_read(r, language_join(r, work, svg), &n);
    format_svg_t v;
    char error[256];
    bool ok = format_svg_parse(s, n, &v, error, sizeof(error));
    scenario_require(r, ok, error);
    scenario_defer(r, release, v.text);
    scenario_require(r, contract(&v, replacement), svg);
}

static void body(scenario_run_t *r, void *argument) {
    config_t *c = argument;
    const char *files[] = {"fibonacci-analysis.svg",
                           "fibonacci-output.txt",
                           "replacements-analysis.svg",
                           "replacements-output.txt"};
    size_t n;
    for (size_t i = 0; i < N(files); i++)
        copy_input(r, c, files[i], &n);
    validate(r, "fibonacci-analysis.svg", "fibonacci-output.txt", "55", false);
    validate(r, "replacements-analysis.svg", "replacements-output.txt", "result: 14", true);
    puts("graph SVG artifacts passed");
}

static void
control(scenario_run_t *r, const char *s, size_t n, bool valid, const char *text, unsigned flags) {
    format_svg_t v;
    char error[256];
    bool ok = format_svg_parse(s, n, &v, error, sizeof(error));
    if (ok)
        scenario_defer(r, release, v.text);
    if (ok != valid) {
        fprintf(stderr,
                "XML control: expected %s, %s\ninput: %.*s\n",
                valid ? "valid" : "invalid",
                ok ? "accepted" : error,
                (int)n,
                s);
        REQUIRE(false);
    }
    if (ok) {
        REQUIRE(!strcmp(v.text, text));
        REQUIRE(v.green_stroke == ((flags & 1) != 0));
        REQUIRE(v.green_fill == ((flags & 2) != 0));
        REQUIRE(v.purple_pair == ((flags & 4) != 0));
    }
}

static void controls(scenario_run_t *r, void *argument) {
    (void)argument;
    const char *bad[] = {"",
                         "<svg>",
                         "<svg></g>",
                         "<svg/><g/>",
                         "junk<svg/>",
                         "<svg/>junk",
                         "<svg a='1'a='2'/>",
                         "<svg a='1' a='2'/>",
                         "<svg a=1/>",
                         "<svg a='<'/>",
                         "<svg a='&unknown;'/>",
                         "<svg>&unknown;</svg>",
                         "<svg>&amp</svg>",
                         "<svg>&#;</svg>",
                         "<svg>&#x;</svg>",
                         "<svg>&#0;</svg>",
                         "<svg>&#xD800;</svg>",
                         "<svg>&#x110000;</svg>",
                         "<svg>&#999999999999;</svg>",
                         "<svg>&#x1;</svg>",
                         "<svg>&#12;</svg>",
                         "<svg>&#xFFFE;</svg>",
                         "<svg>&#-1;</svg>",
                         "<svg>]]></svg>",
                         "<svg><!-- bad -- comment --></svg>",
                         "<svg><!--x---></svg>",
                         "<svg><![CDATA[x</svg>",
                         "<svg><?xml version='1.0'?></svg>",
                         "<svg><?p</svg>",
                         "<?x: version='1.0'?><svg/>",
                         "<?xml?><svg/>",
                         "<?xml version='1.1'?><svg/>",
                         "<?xml version='1.0' junk='x'?><svg/>",
                         "<?xml version='1.0'encoding='UTF-8'?><svg/>",
                         "<?xml version='1.0' encoding='UTF-16'?><svg/>",
                         "<?XML version='1.0'?><svg/>",
                         " <\x3fxml version='1.0'?><svg/>",
                         "<!DOCTYPE svg [<!ENTITY x 'forestgreen'>]><svg stroke='&x;'/>",
                         "<!DOCTYPE svg PUBLIC 'bad^' 'none'><svg/>",
                         "<!DOCTYPE svg SYSTEM 'none' [ ]><svg/>",
                         "<svg x:stroke='forestgreen'/>",
                         "<x:svg/>",
                         "<svg xlink:href='x'/>",
                         "<svg xmlns:xlink='wrong'/>",
                         "<svg xmlns:xml='wrong'/>",
                         "<svg xmlns:xlink='http://www.w3.org/1999/xlink' xlink:1='a'/>",
                         "<!DOCTYPE svg PUBLIC 'bad\t' 'none'><svg/>",
                         "<svg / >",
                         "<svg></svg extra>",
                         "<svg>\xc0\xaf</svg>",
                         "<svg>\xed\xa0\x80</svg>",
                         "<svg>\xf4\x90\x80\x80</svg>",
                         "<svg>\x01</svg>",
                         "<svg>\xff</svg>"};
    for (size_t i = 0; i < N(bad); i++)
        control(r, bad[i], strlen(bad[i]), false, NULL, 0);
    static const char nul[] = "<svg>\0</svg>";
    control(r, nul, sizeof(nul) - 1, false, NULL, 0);

    static const struct {
        const char *s, *text;
        unsigned flags;
    } good[] = {
        {"<svg/>", "", 0},
        {"<?xml-stylesheet href='x'?><svg/>", "", 0},
        {"<svg><g stroke='forestgreen'/><g fill='#f2faf2'/></svg>", "", 3},
        {"<svg><g stroke='purple'/><g fill='#f5efff'/></svg>", "", 0},
        {"<svg><g fill='&#35;f5efff' stroke='pur&#x70;le'/></svg>", "", 4},
        {"<svg stroke='forestgreen ' fill=' #f2faf2'>forestgreen #f2faf2</svg>",
         "forestgreen #f2faf2",
         0},
        {"<svg><g data-stroke='forestgreen' style='fill:#f2faf2'/><!--stroke='purple' "
         "fill='#f5efff'--></svg>",
         "",
         0},
        {"<svg xmlns:xlink='http://www.w3.org/1999/xlink' xlink:stroke='forestgreen' "
         "xlink:fill='#f2faf2'/>",
         "",
         0},
        {"<svg>C <g>vi</g>ew:<!--ignored--> (inte<?p ignore?>ger) C=supp<![CDATA[orted]]></svg>",
         "C view: (integer) C=supported",
         0},
        {"<svg>&lt;&gt;&amp;&apos;&quot;&#65;&#x1F600;</svg>", "<>&'\"A\xf0\x9f\x98\x80", 0},
        {"<svg a='a\t\r\nb&#9;c'>a\r\nb\rc</svg>", "a\nb\nc", 0},
        {"<?xml version='1.0' encoding='UTF-8' standalone='no'?>\n<!DOCTYPE svg PUBLIC "
         "'-//W3C//DTD SVG 1.1//EN' 'http://invalid.example/svg.dtd'><!--x--><svg "
         "xmlns='http://www.w3.org/2000/svg'>ok</svg><?p?>",
         "ok",
         0},
        {"\xef\xbb\xbf<svg><![CDATA[<x>&unknown;]]></svg>", "<x>&unknown;", 0}};

    for (size_t i = 0; i < N(good); i++)
        control(r, good[i].s, strlen(good[i].s), true, good[i].text, good[i].flags);
    REQUIRE(output_is("\xc2\xa0\xe3\x80\x80 55\r\n\xc2\x85", 12, "55"));
    REQUIRE(output_is("\x1cresult: 14\x1f", 12, "result: 14"));
    REQUIRE(!output_is("55\0", 3, "55"));
    REQUIRE(!output_is("55\xff", 3, "55"));
    REQUIRE(!output_is(" 5 5 ", 5, "55"));
    REQUIRE(!output_is("result: 15", 10, "result: 14"));
    REQUIRE(!output_is("", 0, "55"));
    format_svg_t v = {.text = "C view: (integer) C=supported",
                      .green_stroke = true,
                      .green_fill = true};
    REQUIRE(contract(&v, false));
    v.green_stroke = false;
    REQUIRE(!contract(&v, false));
    v.green_stroke = true;
    v.green_fill = false;
    REQUIRE(!contract(&v, false));
    v.green_fill = true;
    const char *missing_fib[] = {"(integer) C=supported",
                                 "C view: C=supported",
                                 "C view: (integer)"};
    for (size_t i = 0; i < N(missing_fib); i++) {
        v.text = (char *)missing_fib[i];
        REQUIRE(!contract(&v, false));
    }
    v = (format_svg_t){.text = "expression replacement statement replacement original if-else",
                       .purple_pair = true};
    REQUIRE(contract(&v, true));
    v.purple_pair = false;
    REQUIRE(!contract(&v, true));
    const char *missing_repl[] = {"statement replacement original if-else",
                                  "expression replacement original if-else",
                                  "expression replacement statement replacement if-else",
                                  "expression replacement statement replacement original"};
    v.purple_pair = true;
    for (size_t i = 0; i < N(missing_repl); i++) {
        v.text = (char *)missing_repl[i];
        REQUIRE(!contract(&v, true));
    }
    char nested[4000];
    size_t n = 0;
    for (unsigned i = 0; i < 257; i++) {
        memcpy(nested + n, "<g>", 3);
        n += 3;
    }
    for (unsigned i = 0; i < 257; i++) {
        memcpy(nested + n, "</g>", 4);
        n += 4;
    }
    control(r, nested, n, false, NULL, 0);
    puts("graph XML and semantic controls passed");
}

int main(int argc, char **argv) {
#ifdef _WIN32
    fs_error_t e = {0};
    if (!fs_arguments(&argc, &argv, &e)) {
        clear_fs_error(&e);
        return 1;
    }
#endif
    int result = 1;
    if (argc == 2 && !strcmp(argv[1], "--list")) {
        puts("integration.graph\nintegration.graph_controls");
        result = 0;
    } else if (argc == 5 || argc == 6) {
        config_t c = {.input = argv[4]};
        scenario_definition_t defs[] = {{"integration.graph", 30000, body, &c, NULL},
                                        {"integration.graph_controls", 30000, controls, &c, NULL}};
        scenario_options_t options = {.build_directory = argv[2], .data_directory = argv[3]};
        const char *selection[] = {argv[1], argc == 6 ? argv[5] : NULL};
        result = scenario_registry_main(defs, N(defs), argc == 6 ? 2 : 1, selection, &options);
    } else
        fputs("usage: graph_scenarios ID BUILD DATA INPUT [--keep-work]\n", stderr);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
