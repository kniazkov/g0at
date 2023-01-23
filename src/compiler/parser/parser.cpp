/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <cassert>
#include <list>
#include <variant>
#include "parser.h"
#include "compiler/scanner/tokens.h"
#include "compiler/common/exceptions.h"
#include "resources/messages.h"
#include "model/expressions.h"
#include "model/statements.h"
#include "model/strings.h"

namespace goat {

    /**
     * @brief Tries to parse the list of tokens as an expression
     * @param data Data needed for parsing
     * @param iter Iterator by token
     * @return An expression
     */
    expression * parse_expression(parser_data *data, token_iterator *iter);

    /**
     * @brief Parses the list of tokens as an expression, but does not process the operators
     * @param data Data needed for parsing
     * @param iter Iterator by token
     * @return An expression
     */
    expression * parse_expression_without_operators(parser_data *data, token_iterator *iter);

    /**
     * @brief Parses an expression that begins with an identifier
     * @param data Data needed for parsing
     * @param iter Iterator by token
     * @param ident First token (identifier)
     * @return An expression
     */
    expression * parse_expression_begins_with_identifier(parser_data *data, token_iterator *iter,
        token *first);

    /**
     * @brief Parses the list of tokens as a comma-separated list of expressions
     * @param data Data needed for parsing
     * @param iter Iterator by token
     * @param list List of expressions to be filled
     */
    void parse_function_call_arguments(parser_data *data, token_iterator *iter,
        std::vector<expression*> *list);

    statement * parse_statement(parser_data *data, token_iterator *iter) {
        if (!iter->valid()) {
            return nullptr;
        }
        token *tok = iter->get();
        switch(tok->type) {
            case token_type::identifier: {
                expression *expr = parse_expression(data, iter);
                if (iter->valid()) {
                    tok = iter->get();
                    if (tok->type == token_type::comma || tok->type == token_type::semicolon) {
                        iter->next();
                    }
                }
                statement *result = new statement_expression(expr);
                expr->release();
                return result;
            }
        }
        throw compiler_exception(tok, get_messages()->msg_unable_to_parse_token_sequence());
    }

    expression * parse_expression(parser_data *data, token_iterator *iter) {
        return parse_expression_without_operators(data, iter);
    }

    expression * parse_expression_without_operators(parser_data *data, token_iterator *iter) {
        assert(iter->valid());
        token *first = iter->get();
        if (first->type == token_type::identifier) {
            iter->next();
            return parse_expression_begins_with_identifier(data, iter, first);
        }
        if (first->type == token_type::string) {
            iter->next();
            token_string *str = (token_string*)first;
            dynamic_string *obj = new dynamic_string(data->gc, str->data);
            data->objects->insert(obj);
            expression *result = new expression_object(obj);
            obj->release();
            return result;
        }
        if (first->type == token_type::integer) {
            iter->next();
            token_number *num = (token_number*)first;
            return new constant_integer_number(num->data.int_value);
        }
        throw compiler_exception(first, get_messages()->msg_unable_to_parse_token_sequence());
    }

    expression * parse_expression_begins_with_identifier(
            parser_data *data, token_iterator *iter, token *first) {
        assert(first->type == token_type::identifier);
        
        bool end_of_sequence = false;
        token *second = nullptr;
        if (iter->valid()) {
            second = iter->get();
            if (second->type == token_type::semicolon) {
                end_of_sequence = true;
            }
        } else {
            end_of_sequence = true;
        }

        if (end_of_sequence) {
            /*
                This is reading a variable:
                    x;
                    x
             */
            std::wstring var_name(first->code, first->length);
            dynamic_string *obj = new dynamic_string(data->gc, var_name);
            data->objects->insert(obj);
            expression *result = new read_variable(obj);
            obj->release();
            return result;
        }

        if (second->type == token_type::brackets_pair) {
            token_brackets_pair *pair = (token_brackets_pair*)second;
            if(pair->opening_bracket == '(') {
                /*
                    This is a function call:
                        func(...) 
                */
                token_iterator_over_vector iter2(pair->tokens);
                std::vector<expression*> args;
                parse_function_call_arguments(data, &iter2, &args);
                std::wstring func_name(first->code, first->length);
                dynamic_string *obj = new dynamic_string(data->gc, func_name);
                data->objects->insert(obj);
                expression *result = new function_call(obj, args);
                obj->release();
                for (expression *expr : args) {
                    expr->release();
                }
                return result;
            }
        }

        throw compiler_exception(first, get_messages()->msg_unable_to_parse_token_sequence());
    }

    void parse_function_call_arguments(parser_data *data, token_iterator *iter,
            std::vector<expression*> *list) {
        while(iter->valid()) {
            expression *expr = parse_expression(data, iter);
            list->push_back(expr);
        }
    }
}
