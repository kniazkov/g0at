/** @file test_conditional.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Conditional grammar regressions.
 */
#include "test_conditional.h"

#include "analysis_test_support.h"
#include "lib/files/file_system.h"
#include "lib/memory/allocate.h"
#include "test_macro.h"

#include <stdio.h>

bool test_conditional_parser(void) {
    const gchar_t *valid[] = {U"a?b:c",
                              U"a?b?c:d:e",
                              U"a?b:c?d:e",
                              U"a ? b=c : d=e",
                              U"x=a?b:c",
                              U"x+=a?b:c",
                              U"a?b+=c:d*=e",
                              U"a || b ? c : d && e",
                              U"a?b:-c",
                              U"f(a?b:c,d)",
                              U"[a?b:c,d]",
                              U"obj[a?b:c]=d",
                              U"(a?b:c).x",
                              U"(a?b:c)()",
                              U"(a?b:c)?d:e",
                              U"switch(x){case a?b:c: println(x);}",
                              U"for(;;) switch(x){case a?b:c: break;}",
                              U"a ? b -> {} : c -> {}",
                              U"(a?b:c) -> {}"};
    const gchar_t *invalid[] = {U"?a:b",
                                U"a?",
                                U"a?:b",
                                U"a?b",
                                U"a?b:",
                                U"a:b",
                                U"a?b:c:d",
                                U"a?b,c:d",
                                U"a?b:var c=1",
                                U"a??b:c",
                                U"a?b?c:d",
                                U"a?b:c?d",
                                U"(a?b:c)=d",
                                U"a ? return b : c"};
    for (size_t pass = 0; pass < 2; pass++) {
        const gchar_t **texts = pass ? invalid : valid;
        size_t count = pass ? sizeof(invalid) / sizeof(*invalid) : sizeof(valid) / sizeof(*valid);
        for (size_t i = 0; i < count; i++) {
            arena_t *arena = create_arena(8);
            parser_memory_t memory = {arena, arena, arena, arena};
            node_t *root =
                parse_analysis_test_program(&memory,
                                            (string_value_t){texts[i], STRLEN(texts[i]), false});
            if (pass ? root != NULL : root == NULL)
                FPRINTF(stderr, "conditional parser case %zu/%zu\n", pass, i);
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
