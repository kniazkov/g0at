/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

namespace goat {

    /**
     * @brief Runs the Goat interpreter
     * @param argc Number of command line arguments
     * @param argv Array containing command line arguments
     * @return Error code
     */
    int run(int argc, char** argv);
}
