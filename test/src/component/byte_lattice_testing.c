/** @file byte_lattice_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief ByteArray subtype laws without mutable value facts.
 */
#ifdef NDEBUG
#    undef NDEBUG
#endif
#include "analysis/lattice.h"
#include "lib/memory/allocate.h"
#include "lib/memory/arena.h"
#include "lib/strings/string_ext.h"

#include <assert.h>

int main(void) {
    arena_t *arena = create_arena(256);
    const lattice_element_t *bytes = make_byte_array_element();
    const lattice_element_t *values[] = {make_bottom_element(),
                                         bytes,
                                         make_user_defined_object_element(),
                                         make_not_null_element(),
                                         make_top_element(),
                                         make_null_element(),
                                         make_integer_element(),
                                         make_numeric_element(),
                                         make_real_element(),
                                         make_string_element(),
                                         make_boolean_element(),
                                         make_array_element(),
                                         make_function_element()};
    assert(is_user_defined_object_lattice_element(bytes));
    assert(lattice_may_dispatch_operator(bytes));
    assert(lattice_truth(bytes) == ABSTRACT_EITHER);
    string_value_t text = lattice_to_string(bytes);
    assert(!STRCMP(text.data, U"ByteArray"));
    FREE_STRING(text);
    assert(lattice_join(arena, bytes, make_user_defined_object_element())->type
           == LATTICE_USER_DEFINED_OBJECT);
    assert(lattice_meet(arena, bytes, make_user_defined_object_element())->type
           == LATTICE_BYTE_ARRAY);
    assert(lattice_meet(arena, bytes, make_array_element())->type == LATTICE_BOTTOM);
    const size_t n = sizeof(values) / sizeof(*values);
    for (size_t i = 0; i < n; ++i) {
        const lattice_element_t *a = values[i];
        assert(lattice_join(arena, a, a)->type == a->type);
        assert(lattice_meet(arena, a, a)->type == a->type);
        for (size_t j = 0; j < n; ++j) {
            const lattice_element_t *b = values[j];
            assert(lattice_join(arena, a, b)->type == lattice_join(arena, b, a)->type);
            assert(lattice_meet(arena, a, b)->type == lattice_meet(arena, b, a)->type);
            assert(lattice_join(arena, a, lattice_meet(arena, a, b))->type == a->type);
            assert(lattice_meet(arena, a, lattice_join(arena, a, b))->type == a->type);
            for (size_t k = 0; k < n; ++k) {
                const lattice_element_t *c = values[k];
                assert(lattice_join(arena, lattice_join(arena, a, b), c)->type
                       == lattice_join(arena, a, lattice_join(arena, b, c))->type);
                assert(lattice_meet(arena, lattice_meet(arena, a, b), c)->type
                       == lattice_meet(arena, a, lattice_meet(arena, b, c))->type);
            }
        }
    }
    const lattice_element_t *typed = make_typed_array_element(arena, LATTICE_BYTE_ARRAY);
    text = lattice_to_string(typed);
    assert(!STRCMP(text.data, U"array<ByteArray>"));
    FREE_STRING(text);
    destroy_arena(arena);
    return 0;
}
