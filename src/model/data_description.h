/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#pragma once

#include <vector>
#include "code.h"
#include "scope.h"
#include "expressions.h"
#include "compiler/analyzer/data_type.h"

namespace goat {

    /**
     * @brief The descriptor of the prototype, that is, the part after the variable/constant name
     */
    class prototype_descriptor {
    public:
        /**
         * @brief Virtual destructor.
         */
        virtual ~prototype_descriptor() {
        }

        /**
         * @brief Represents the descriptor as a Goat object
         * @param gc Data required for the garbage collector
         * @return Descriptor as a Goat object
         */
        virtual object * to_object(gc_data* const gc) = 0;
    };

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
         * @param parent Prototype descriptor
         * @param init_value Initial value
         */
        data_descriptor(bool modifiable, base_string *name, expression *init_value);

        /**
         * @brief Constructor
         * @param modifiable Flag indicating whether or not this data is modifiable
         * @param name Name of the variable, constant, argument
         * @param parent Prototype descriptor
         * @param init_value Initial value
         * @param proto_list List of prototype names
         */
        data_descriptor(bool modifiable, base_string *name, expression *init_value,
            std::vector<std::wstring> proto_list);

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
         * @brief Prototype descriptor
         */
        prototype_descriptor *extends_from;

        /**
         * @brief Initial value
         */
        expression *init_value;
    };
}
