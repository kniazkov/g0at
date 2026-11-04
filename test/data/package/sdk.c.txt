#include "goat/native_strings.h"
#include <assert.h>
int main(void) {
    goat_native_string_view_v2_t text = {U"ababa", 5}, part = {U"aba", 3};
    assert(goat_native_string_index_of(text, part) == 0);
    assert(goat_native_string_last_index_of(text, part) == 2);
    return 0;
}
