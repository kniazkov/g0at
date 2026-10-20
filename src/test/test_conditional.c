/** @file test_conditional.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Conditional grammar regressions.
 */
#include "test_conditional.h"

#include "analysis_test_support.h"
#include "lib/allocate.h"
#include "test_macro.h"

#include <stdio.h>

bool test_conditional_parser(void) {
    const wchar_t *valid[] = {L"a?b:c",
                              L"a?b?c:d:e",
                              L"a?b:c?d:e",
                              L"a ? b=c : d=e",
                              L"x=a?b:c",
                              L"x+=a?b:c",
                              L"a?b+=c:d*=e",
                              L"a || b ? c : d && e",
                              L"a?b:-c",
                              L"f(a?b:c,d)",
                              L"[a?b:c,d]",
                              L"obj[a?b:c]=d",
                              L"(a?b:c).x",
                              L"(a?b:c)()",
                              L"(a?b:c)?d:e",
                              L"switch(x){case a?b:c: println(x);}",
                              L"for(;;) switch(x){case a?b:c: break;}",
                              L"a ? b -> {} : c -> {}",
                              L"(a?b:c) -> {}"};
    const wchar_t *invalid[] = {L"?a:b",
                                L"a?",
                                L"a?:b",
                                L"a?b",
                                L"a?b:",
                                L"a:b",
                                L"a?b:c:d",
                                L"a?b,c:d",
                                L"a?b:var c=1",
                                L"a??b:c",
                                L"a?b?c:d",
                                L"a?b:c?d",
                                L"(a?b:c)=d",
                                L"a ? return b : c"};
    for (size_t pass = 0; pass < 2; pass++) {
        const wchar_t **texts = pass ? invalid : valid;
        size_t count = pass ? sizeof(invalid) / sizeof(*invalid) : sizeof(valid) / sizeof(*valid);
        for (size_t i = 0; i < count; i++) {
            arena_t *arena = create_arena(8);
            parser_memory_t memory = {arena, arena, arena, arena};
            node_t *root =
                parse_analysis_test_program(&memory,
                                            (string_value_t){texts[i], wcslen(texts[i]), false});
            if (pass ? root != NULL : root == NULL)
                fprintf(stderr, "conditional parser case %zu/%zu\n", pass, i);
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
