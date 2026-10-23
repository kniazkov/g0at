/** @file test_access_analysis.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Definite denial is independent of optimization; uncertain identities stay checked.
 */
#include "test_access_analysis.h"

#include "analysis/analysis.h"
#include "analysis_test_support.h"
#include "cli/options.h"
#include "graph/declarations.h"
#include "lib/allocate.h"
#include "test_macro.h"

#include <stdio.h>

static size_t proofs(const node_t *node, access_proof_t proof) {
    size_t result = node->access_proof == proof;
    for (size_t i = 0; i < get_node_child_count(node); i++)
        result += proofs(get_node_child(node, i), proof);
    return result;
}

bool test_access_analysis(void) {
    const gchar_t *denied[] = {U"const a={private x=1;}; a.x;",
                               U"const a={private var x=1;}; a.x=2;",
                               U"const a={private var x=1;}; a.x+=2;",
                               U"const a={private x=1;}; a[\"x\"];",
                               U"const a={private var x=1;}; a[\"x\"]*=2;",
                               U"const a={private x=1;}; const b=a; const c=b; c.x;",
                               U"const a={private x=1;}; const key=\"x\"; a[key];",
                               U"({private x=1;}).x;",
                               U"const a={protected x=1;}; a.x;",
                               U"const a={private x=1;}; try a.x; catch(e) println(e);",
                               U"const a={private x=1;}; if(false) println(a.x);"};
    const gchar_t *unknown[] = {
        U"private x=7; const a={private x=1;}; a.x;",
        U"private x=7; const a={protected x=1;}; a[\"x\"];",
        U"const a={private x=1;}; const f=func(){return a.x;};",
        U"var a={private x=1;}; try a.x; catch(e) println(e);",
        U"const a={private x=1;}; var key=\"x\"; try a[key]; catch(e) println(e);",
        U"const make=func(){return {private x=1;};}; const a=make(); a.x;",
        U"const a={private x=1;}; const b=a.clone(); b.x;",
        U"const a={private x=1;}; const b=a.flat(); b.x;",
        U"const a={private x=1;}; const b=a->{}; b.x;",
        U"const a={protected x=1;}; const b=a->{public f=func(){return a.x;};}; b.f();",
        U"const a={private x=1; public f=func(value){return value.x;};}; a.f(a);",
        U"const a={public x=1;}; a.missing;",
        U"const f=func(){private x=1; return this;}; f();"};
    for (size_t optimize = 0; optimize < 2; optimize++) {
        for (size_t group = 0; group < 2; group++) {
            const gchar_t **texts = group ? unknown : denied;
            size_t count =
                group ? sizeof(unknown) / sizeof(*unknown) : sizeof(denied) / sizeof(*denied);
            for (size_t i = 0; i < count; i++) {
                arena_t *arena = create_arena(8);
                parser_memory_t memory = {arena, arena, arena, arena};
                node_t *root = parse_analysis_test_program(
                    &memory,
                    (string_value_t){texts[i], STRLEN(texts[i]), false});
                if (!root)
                    fprintf(stderr, "access syntax case %zu/%zu\n", group, i);
                ASSERT(root);
                options_t *options = create_options();
                options->optimization_level = optimize ? OPTIMIZATION_ALL : OPTIMIZATION_NONE;
                compilation_error_t *errors = analyze(root, &memory, options, NULL);
                if (!group) {
                    ASSERT(get_most_severe_compilation_error(errors) >= ERROR);
                    ASSERT(proofs(root, ACCESS_PROOF_DENIED) == 1);
                    ASSERT(errors->position && STRSTR(errors->message.data, U"x"));
                } else {
                    ASSERT(get_most_severe_compilation_error(errors) < ERROR);
                    ASSERT(!proofs(root, ACCESS_PROOF_DENIED));
                    ASSERT(proofs(root, ACCESS_PROOF_UNKNOWN));
                    ASSERT(has_access_modifiers(root));
                }
                destroy_options(options);
                destroy_arena(arena);
            }
        }
    }
    return true;
}
