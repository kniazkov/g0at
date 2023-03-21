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

        void visit(variable_declaration *stmt) override {
            for (std::wstring name : stmt->get_list_of_variable_names()) {
                auto descr = stmt->get_descriptor_by_name(name);
                if (descr->init_value) {
                    descr->type = descr->init_value->get_data_type();
                }
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
