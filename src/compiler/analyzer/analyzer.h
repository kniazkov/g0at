/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

namespace goat {

    class program;

    /**
     * @brief Performs program analysis, i.e. fills the syntax tree with additional information,
     *   replaces / deletes nodes of the syntax tree
     * @param prog The program
     */
    void perform_a_program_analysis(program *prog);
}
