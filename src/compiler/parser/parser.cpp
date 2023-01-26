/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <cassert>
#include <cstring>
#include <list>
#include "parser.h"
#include "compiler/scanner/tokens.h"
#include "compiler/common/exceptions.h"
#include "resources/messages.h"
#include "model/expressions.h"
#include "model/statements.h"
#include "model/strings.h"

namespace goat {

    /**
     * @brief Element of the chain, which contains both tokens and expressions
     *   that have already been parsed
     */
    struct token_chain_item {
        /**
         * Type of element 
         */
        enum {
            is_token,
            is_expression
        } type;

        /**
         * @brief Pointer to the corresponding entity
         */
        union {
            token *tok;
            expression *expr;
        } ptr;
    };

    /**
     * @brief A functor that creates a binary operation from two operands
     */
    typedef binary_operation * (*binary_operation_creator)(expression *left, expression *right);
    
    /**
     * @brief Descriptor of a binary operation
     */
    struct binary_operation_description {
        /**
         * @brief Symbols denoting this operation
         */
        const wchar_t *symbols;

        /**
         * @brief Functor that creates a binary operation from two operands
         */
        binary_operation_creator creator;
    };

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

    /**
     * @brief Parses a chain of operators and expressions, 
     *   and when specified operators are found, combines two expressions and one operator
     *   into a binary operation
     * @param chain Chain of operators and expressions
     * @param count Number of operator descriptors
     * @param descr Array of operator descriptors
     */
    void parse_binary_operators(std::list<token_chain_item> *chain, int count,
        const binary_operation_description *descr);

    /* ----------------------------------------------------------------------------------------- */

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
                    if (tok->type == token_type::semicolon) {
                        iter->next();
                    }
                }
                statement *result = new statement_expression(expr);
                expr->release();
                return result;
            }
        }
        throw compiler_exception(new compiler_exception_data(
            tok, get_messages()->msg_unable_to_parse_token_sequence())
        );
    }

    /**
     * @brief Descriptors for operators *, /, %
     */
    const binary_operation_description mul_div_mod[] = {
        { L"*", multiplication::creator }
    };

    /**
     * @brief Descriptors for operators +, -
     */
    const binary_operation_description plus_minus[] = {
        { L"+", addition::creator },
        { L"-", subtraction::creator },
    };

    expression * parse_expression(parser_data *data, token_iterator *iter) {
        std::list<token_chain_item> chain;

        while(iter->valid()) {
            token *tok = iter->get();
            if (tok->type == token_type::comma || tok->type == token_type::semicolon) {
                break;
            }
            token_chain_item item;
            if (tok->type == token_type::oper) {
                item.type = token_chain_item::is_token;
                item.ptr.tok = tok;
                iter->next();
            } else {
                item.type = token_chain_item::is_expression;
                item.ptr.expr = parse_expression_without_operators(data, iter);
            }
            chain.push_back(item);
        }

        if (chain.size() > 1) {
            parse_binary_operators(&chain, 1, mul_div_mod);            
            parse_binary_operators(&chain, 2, plus_minus);            
        }

        assert(chain.size() == 1 && chain.begin()->type == token_chain_item::is_expression);
        return chain.begin()->ptr.expr;
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
        throw compiler_exception(new compiler_exception_data(
            first, get_messages()->msg_unable_to_parse_token_sequence())
        );
    }

    expression * parse_expression_begins_with_identifier(
            parser_data *data, token_iterator *iter, token *first) {
        assert(first->type == token_type::identifier);
        
        bool end_of_sequence = false;
        token *second = nullptr;
        if (iter->valid()) {
            second = iter->get();
            iter->next();
            if (second->type == token_type::comma || second->type == token_type::semicolon) {
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

        throw compiler_exception(new compiler_exception_data(
            first, get_messages()->msg_unable_to_parse_token_sequence())
        );
    }

    void parse_function_call_arguments(parser_data *data, token_iterator *iter,
            std::vector<expression*> *list) {
        while(iter->valid()) {
            expression *expr = parse_expression(data, iter);
            list->push_back(expr);
        }
    }

    void parse_binary_operators(std::list<token_chain_item> *chain, int count,
            const binary_operation_description *descr) {
        std::list<token_chain_item>::iterator iter = chain->begin();
        iter++;
        while(iter != chain->end()) {
            bool found = false;
            std::list<token_chain_item>::iterator left =  std::prev(iter);
            std::list<token_chain_item>::iterator right =  std::next(iter);
            if (iter->type == token_chain_item::is_token && right != chain->end() &&
                    left->type == token_chain_item::is_expression &&
                    right->type == token_chain_item::is_expression) {
                int index = 0;
                for (; index < count; index++) {
                    if (0 == std::memcmp(descr[index].symbols, iter->ptr.tok->code,
                                iter->ptr.tok->length * sizeof(wchar_t))) {
                        found = true;
                        break;
                    }
                }
                if (found) {
                    token_chain_item item;
                    std::list<token_chain_item>::iterator next =  std::next(right);
                    item.type = token_chain_item::is_expression;
                    item.ptr.expr = descr[index].creator(left->ptr.expr, right->ptr.expr);
                    chain->insert(left, item);
                    left->ptr.expr->release();
                    right->ptr.expr->release();
                    chain->erase(left);
                    chain->erase(right);
                    chain->erase(iter);
                    iter = next;
                }
            }
            if (!found) {
                iter++;
            }
        }
    }
}
