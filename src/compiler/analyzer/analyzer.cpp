/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "analyzer.h"

namespace goat {

    void bind_variables_to_their_declaration_locations(program *prog);
    void perform_type_inference(program *prog);

    void perform_a_program_analysis(program *prog) {
        bind_variables_to_their_declaration_locations(prog);
        perform_type_inference(prog);
    }
}
