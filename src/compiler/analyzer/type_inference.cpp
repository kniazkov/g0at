/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "model/code.h"
#include "model/statements.h"

namespace goat {

    /**
     * @brief Element visitor which detects types of variables based on their usage
     */
    class type_inference : public element_visitor {

        void visit(data_descriptor *descriptor) override {
            expression *init_value = descriptor->get_init_value();
            if (init_value) {
                descriptor->type = init_value->get_data_type();
            } 
        }
    };

    /**
     * @brief Detects types of variables based on their usage
     * @param prog The program
     */
    void perform_type_inference(program *prog) {
        type_inference visitor;
        prog->traverse_syntax_tree(&visitor);
    }
}
