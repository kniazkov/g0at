/** @file test_exception_parser.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Exception syntax, regeneration and lexical binding.
 */
#include "analysis/analysis.h"
#include "analysis_test_support.h"
#include "cli/options.h"
#include "graph/declarations.h"
#include "graph/statement.h"
#include "graph/variable.h"
#include "lib/allocate.h"
#include "test_macro.h"

#include <stdio.h>

bool test_exception_parser(void) {
    const wchar_t *valid[] = {L"try { } catch (e) { }",
                              L"try throw 1; catch (e) { throw e; }",
                              L"try try throw null catch (e) { throw e; } catch (outer) { }",
                              L"try if (true) { throw 1; } else { } catch (e) { }",
                              L"if (false) try throw 1 catch (e) { } else { }",
                              L"try return; catch (e) { return e; }"};
    const wchar_t *invalid[] = {L"try",
                                L"try {}",
                                L"catch (e) {}",
                                L"try {} catch {}",
                                L"try {} catch () {}",
                                L"try {} catch (a,b) {}",
                                L"try {} catch (1) {}",
                                L"try {} catch (e+1) {}",
                                L"try {} catch ((e)) {}",
                                L"try {} catch (null) {}",
                                L"try {} catch (e) print(e);",
                                L"try {} catch (e) {} catch (f) {}",
                                L"throw;",
                                L"try throw catch (e) {}",
                                L"try ; catch (e) {}"};
    for (size_t i = 0; i < sizeof(valid) / sizeof(*valid); i++) {
        arena_t *arena = create_arena(8);
        parser_memory_t memory = {arena, arena, arena, arena};
        node_t *root =
            parse_analysis_test_program(&memory,
                                        (string_value_t){valid[i], wcslen(valid[i]), false});
        ASSERT(root);
        string_value_t source = generate_goat_code_from_node(root);
        ASSERT(parse_analysis_test_program(&memory, source));
        FREE_STRING(source);
        destroy_arena(arena);
    }
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
        arena_t *arena = create_arena(8);
        parser_memory_t memory = {arena, arena, arena, arena};
        ASSERT(
            !parse_analysis_test_program(&memory,
                                         (string_value_t){invalid[i], wcslen(invalid[i]), false}));
        destroy_arena(arena);
    }
    return true;
}

/** @brief Checks that handler references resolve to its declaration, including in closures. */
static bool bound_to(node_t *node, declarator_t *decl, size_t *count) {
    if (node->vtbl->type == NODE_VARIABLE && !wcscmp(((variable_t *)node)->name.data, L"e")) {
        if (((variable_t *)node)->declarator != decl)
            return false;
        (*count)++;
    }
    for (size_t i = 0; i < get_node_child_count(node); i++)
        if (!bound_to(get_node_child(node, i), decl, count))
            return false;
    return true;
}

bool test_catch_binding(void) {
    arena_t *arena = create_arena(8);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *root =
        parse_analysis_test_program(&memory,
                                    STATIC_STRING(L"var e = 1; try throw 2 catch (e) { print(e); "
                                                  L"var f = func { return e; }; } print(e);"));
    ASSERT(root);
    options_t *options = create_options();
    options->optimization_level = OPTIMIZATION_NONE;
    options->enable_warnings = true;
    ASSERT(!analyze(root, &memory, options, NULL));
    node_t *stmt = get_node_child(root, 1);
    ASSERT(stmt->vtbl->type == NODE_TRY_CATCH);
    declarator_t *decl = get_catch_declarator(stmt);
    ASSERT(decl->base.scope == get_node_child(stmt, 1)->scope);
    ASSERT(decl->base.scope != root->scope);
    size_t count = 0;
    ASSERT(bound_to(get_node_child(stmt, 1), decl, &count) && count == 2);
    declarator_t *outer = (declarator_t *)get_node_child(get_node_child(root, 0), 0);
    count = 0;
    ASSERT(bound_to(get_node_child(root, 2), outer, &count) && count == 1);
    destroy_options(options);
    destroy_arena(arena);
    return true;
}
