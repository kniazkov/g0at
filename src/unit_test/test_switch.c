/** @file test_switch.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Switch grammar boundaries and non-blocking constant-label warnings.
 */
#include "test_switch.h"

#include "analysis/analysis.h"
#include "analysis_test_support.h"
#include "cli/options.h"
#include "graph/switch.h"
#include "lib/allocate.h"
#include "test_macro.h"

#include <stdio.h>

bool test_switch_parser(void) {
    const goat_char_t *valid[] = {
        U"switch(x) {}",
        U"switch(x) {default:}",
        U"switch(x) {case 1:}",
        U"switch(x) {case 1,2,3,: default:}",
        U"switch(x) {case 1: case 1:}",
        U"switch(x) {case f(), a+1, obj.x, items[0]: print(1);}",
        U"switch(x) {case 1: var a=1; case 2: var a=2;}",
        U"switch(x) {case 1: switch(y) {case 2: println(3);} default:}",
        U"if(x) switch(y) {case 1: println(1);} else switch(z) {}",
        U"do switch(x) {case 1: continue; default: break;} while(x);",
        U"while(x) switch(x) {case 1: break; default: continue;}",
        U"switch(x) {case 1: while(x) break; default: do x--; while(x);}",
        U"switch(x) {case 1: try {throw x;} catch(e) {println(e);} default:}",
        U"try switch(x) {case 1: throw x;} catch(e) {println(e);}",
        U"switch(x) {case 1: const f=func(){switch(x){case 2:return 2;default:return 1;}};}",
        U"switch(x) {case 1: if(x) println(1); else println(2); case 2:}",
        U"switch(x) {case 1: do if(x) x--; else x++; while(x); default:}",
        U"var n=0; switch(n++) {case ++n, n++, : n=n+1;} n=n+1;",
        U"switch(x) {case {x=1;}: println(1);}",
        U"switch(x) {case 1: { var a=1; } case 2: ;}",
        U"switch(x) {case 1: return; default: return 2;}",
        U"for(;;) {switch(x){case 1: const f=func(){while(x)break;}; break;}}"};
    const goat_char_t *invalid[] = {
        U"switch",
        U"switch() {}",
        U"switch x {}",
        U"switch(x)",
        U"switch(x);",
        U"switch(x) []",
        U"switch(x,y) {}",
        U"switch(x) {println(1);}",
        U"switch(x) {case:}",
        U"switch(x) {case ,1:}",
        U"switch(x) {case 1,,2:}",
        U"switch(x) {case 1 2:}",
        U"switch(x) {case 1;}",
        U"switch(x) {case 1,}",
        U"switch(x) {default 1:}",
        U"switch(x) {default}",
        U"switch(x) {default: default:}",
        U"switch(x) {default: case 1:}",
        U"case 1:",
        U"default:",
        U"{case 1:}",
        U"var x=switch(y) {case 1:}",
        U"f(switch(x){})",
        U"switch(x) {case 1: break;}",
        U"switch(x) {default: continue;}",
        U"while(x) {switch(x){case 1: const f=func(){break;};}}",
        U"switch(x) {case 1: if(x) println(1); case 2: else println(2);}",
        U"switch(x) {case 1: {case 2:}}",
        U"switch(x) {case 1: :}",
        U"switch(x) {case 1: while(x) default:}",
        U"switch(x) {case 1: do x++; case 2: while(x);}",
        U"switch(x) {case 1: switch(y) {default: case 1:}}"};
    for (size_t pass = 0; pass < 2; pass++) {
        const goat_char_t **texts = pass ? invalid : valid;
        size_t count = pass ? sizeof(invalid) / sizeof(*invalid) : sizeof(valid) / sizeof(*valid);
        for (size_t i = 0; i < count; i++) {
            arena_t *arena = create_arena(8);
            parser_memory_t memory = {arena, arena, arena, arena};
            node_t *root = parse_analysis_test_program(
                &memory,
                (string_value_t){texts[i], goat_strlen(texts[i]), false});
            if (pass ? root != NULL : root == NULL)
                fprintf(stderr, "switch grammar %zu/%zu\n", pass, i);
            ASSERT(pass ? root == NULL : root != NULL);
            if (root) {
                string_value_t source = generate_goat_code_from_node(root);
                ASSERT(parse_analysis_test_program(&memory, source));
                FREE_STRING(source);
            }
            destroy_arena(arena);
        }
    }
    return true;
}

bool test_switch_warnings(void) {
    for (size_t mode = 0; mode < 4; mode++) {
        arena_t *arena = create_arena(8);
        parser_memory_t memory = {arena, arena, arena, arena};
        node_t *root = parse_analysis_test_program(
            &memory,
            STATIC_STRING(U"var x=2; switch(x){case 1+1,2: case 2.0: case true: case false: "
                          U"case \"a\"+\"b\",\"ab\": case 0.0,-0.0: case 'a','a': default:}"));
        ASSERT(root);
        options_t *options = create_options();
        options->enable_warnings = (mode & 1) != 0;
        options->optimization_level = (mode & 2) ? OPTIMIZATION_ALL : OPTIMIZATION_NONE;
        compilation_error_t *errors = analyze(root, &memory, options, NULL);
        size_t count = 0;
        for (compilation_error_t *error = errors; error; error = error->next) {
            ASSERT(error->severity == WARNING);
            ASSERT(error->position);
            count++;
        }
        ASSERT(count == ((mode & 1) ? 5 : 0));
        destroy_options(options);
        destroy_arena(arena);
    }
    return true;
}
