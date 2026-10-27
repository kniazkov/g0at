/** @file test_access_analysis.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Definite denial is independent of optimization; uncertain identities stay checked.
 */
#include "test_access_analysis.h"

#include "analysis/access.h"
#include "analysis/analysis.h"
#include "analysis_test_support.h"
#include "cli/options.h"
#include "graph/declarations.h"
#include "graph/variable.h"
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test_macro.h"

#include <stdio.h>

static size_t proofs(const node_t *node, access_proof_t proof) {
    size_t result = node->access_proof == proof;
    for (size_t i = 0; i < get_node_child_count(node); i++)
        result += proofs(get_node_child(node, i), proof);
    return result;
}

/** @brief Locate a proof site without relying on child layout. */
static node_t *find_type(node_t *node, node_type_t type) {
    if (node->vtbl->type == type)
        return node;
    for (size_t i = 0; i < get_node_child_count(node); i++) {
        node_t *found = find_type(get_node_child(node, i), type);
        if (found)
            return found;
    }
    return NULL;
}

/** @brief Separate trees with identical diagnostic filenames do not establish provenance. */
static bool cross_source_proofs(void) {
    const gchar_t *texts[] = {U"const value=1; value;",
                              U"public value=1; value;",
                              U"external value=1; value;",
                              U"const object={const value=1;}; object.value;",
                              U"const object={public value=1;}; object.value;",
                              U"const object={external value=1;}; object.value;"};
    for (size_t i = 0; i < sizeof(texts) / sizeof(*texts); i++) {
        arena_t *arena = create_arena(8);
        parser_memory_t memory = {arena, arena, arena, arena};
        string_value_t source = {texts[i], STRLEN(texts[i]), false};
        node_t *local = parse_analysis_test_program(&memory, source);
        node_t *foreign = parse_analysis_test_program(&memory, source);
        ASSERT(local && foreign);
        options_t *options = create_options();
        options->optimization_level = OPTIMIZATION_NONE;
        ASSERT(get_most_severe_compilation_error(analyze(local, &memory, options, NULL)) < ERROR);
        ASSERT(get_most_severe_compilation_error(analyze(foreign, &memory, options, NULL)) < ERROR);
        variable_t *reference = (variable_t *)find_type(local, NODE_VARIABLE);
        variable_t *foreign_reference = (variable_t *)find_type(foreign, NODE_VARIABLE);
        node_t *site = i < 3 ? (node_t *)reference : find_type(local, NODE_MEMBER_ACCESS);
        ASSERT(reference && foreign_reference && site);
        ASSERT(site->access_proof == ACCESS_PROOF_ALLOWED);
        ASSERT(requires_access_safe_optimization(local) == (i % 3 != 0));
        reference->declarator = foreign_reference->declarator;
        ASSERT(requires_access_safe_optimization(local));
        compilation_error_t *errors = NULL;
        analyze_access(local, arena, &errors);
        ASSERT(!errors);
        ASSERT(site->access_proof == (i % 3 == 2 ? ACCESS_PROOF_ALLOWED : ACCESS_PROOF_UNKNOWN));
        destroy_options(options);
        destroy_arena(arena);
    }
    return true;
}

bool test_access_analysis(void) {
    ASSERT(cross_source_proofs());
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
                    FPRINTF(stderr, "access syntax case %zu/%zu\n", group, i);
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
