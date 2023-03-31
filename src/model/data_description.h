/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include "code.h"
#include "scope.h"
#include "expressions.h"
#include "compiler/analyzer/data_type.h"

namespace goat {

    /**
     * @brief Data descriptor, that is, the descriptor of a variable,
     *  constant or argument of a function
     */
    class data_descriptor : public element {
    public:
        /**
         * @brief Constructor
         * @param modifiable Flag indicating whether or not this data is modifiable
         * @param name Name of the variable, constant, argument
         * @param proto_name Prototype name
         * @param init_value Initial value
         */
        data_descriptor(bool modifiable, base_string *name, base_string *proto_name,
            expression *init_value);

        /**
         * @brief Destructor
         */
        ~data_descriptor();

        /**
         * @brief Returns data name (identifier) as a string
         * @return Data name (identifier)
         */
        std::wstring get_name_as_string() {
            return name->to_string(nullptr);
        }

        /**
         * @brief Returns initial value (if exists)
         */
        expression * get_init_value() {
            return init_value;
        }

        /**
         * @brief Initializes data (that is, creates variables and constants
         *  and assigns values to them) in the scope
         * @param scope The scope
         */
        void initialize_data_in_the_scope(scope *scope);

        void traverse_syntax_tree(element_visitor *visitor) override;
        const char * get_class_name() const override;
        std::vector<child_descriptor> get_children() const override;
        std::vector<element_data_descriptor> get_data() const override;

        /**
         * @brief A Goat data type to which data can be cast
         */
        const data_type *type;

    private:
        /**
         * @brief Flag indicating whether or not this data is modifiable
         */
        bool modifiable;

        /**
         * @brief Identifier of the data, that is, the name of the variable, constant, argument...
         */
        base_string *name;

        /**
         * @brief Prototype name
         */
        base_string *proto_name;

        /**
         * @brief Initial value
         */
        expression *init_value;
    };
}
