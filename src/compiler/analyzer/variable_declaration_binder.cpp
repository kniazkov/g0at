/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include "model/code.h"
#include "model/statements.h"

namespace goat {

    /**
     * @brief Element visitor that binds variables to their declaration locations
     */
    class variable_declaration_binder : public element_visitor {
        void visit(expression_variable *expr) override {
            std::wstring name = expr->get_variable_name();
            auto pair = descriptors.find(name);
            if (pair != descriptors.end()) {
                expr->set_descriptor(pair->second);
            }
        }

        void visit(data_descriptor *descriptor) override {
            descriptors[descriptor->get_name_as_string()] = descriptor;
        }

    private:
        /**
         * @brief The relationship between statements in which variables are declared
         *   and variable names
         */
        std::map<std::wstring, data_descriptor*> descriptors;
    };

    /**
     * @brief Binds variables to their declaration locations
     * @param prog The program
     */
    void bind_variables_to_their_declaration_locations(program *prog) {
        variable_declaration_binder visitor;
        prog->traverse_syntax_tree(&visitor);
    }
}
