/** @file test_compound.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Assignment grammar and regenerated-source parsing.
 */
#include "test_compound.h"

#include "analysis_test_support.h"
#include "lib/allocate.h"
#include "test_macro.h"

#include <stdio.h>

bool test_compound_parser(void) {
    const wchar_t *valid[] = {L"a+=1",
                              L"a-=1",
                              L"a*=1",
                              L"a/=1",
                              L"a%=1",
                              L"a**=2",
                              L"a&=1",
                              L"a|=1",
                              L"a^=1",
                              L"a<<=1",
                              L"a>>=1",
                              L"a+=b*=2",
                              L"a=b+=c",
                              L"f().x+=g()",
                              L"f()[g(),h()]+=r()",
                              L"a[]+=1",
                              L"var a=1; var b=(a+=2);",
                              L"a+=b+2*3",
                              L"a += -1"};
    const wchar_t *invalid[] = {L"+=1",
                                L"a+=",
                                L"1+=2",
                                L"f()+=2",
                                L"(a+b)+=2",
                                L"a<<=",
                                L"a>>=",
                                L"a + = 1",
                                L"a<< =2",
                                L"a&&=1",
                                L"a||=1",
                                L"a**="};
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
                fprintf(stderr, "compound parser case %zu/%zu\n", pass, i);
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
