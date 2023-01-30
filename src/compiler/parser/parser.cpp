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

    const char * parser_data::copy_file_name(const char *name) {
        auto pair = file_names_map.find(name);
        if (pair == file_names_map.end()) {
            size_t len = std::strlen(name);
            char *copy = new char[len + 1];
            std::memcpy(copy, name, (len + 1) * sizeof(char));
            file_names_list->push_back(copy);
            file_names_map[name] = copy;
            return copy;
        }
        else {
            return pair->second;
        }
    };

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
     * @brief Tries to parse the list of tokens as a list of statements
     * @param data Data needed for parsing
     * @param iter Iterator by token
     * @param block The object in which to put statements
     */
    void parse_statement_block(parser_data *data, token_iterator *iter, statement_block *block);

    /**
     * @brief Tries to parse the list of tokens as a variable(s) declaration
     * @param data Data needed for parsing
     * @param iter Iterator by token
     * @param keyword Token containing "var" keyword
     * @return A statement
     */
    declare_variable * parse_variable_declaration(parser_data *data, token_iterator *iter,
        token *keyword);

    /**
     * @brief Tries to parse the list of tokens, started from the dollar sign, 
     *   as a variable declaration
     * @param data Data needed for parsing
     * @param iter Iterator by token
     * @param keyword Token containing dollar sign
     * @return A statement
     */
    declare_variable * parse_variable_dollar_declaration(parser_data *data, token_iterator *iter,
            token *dollar);

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

    program * parse_program(gc_data *gc, token_iterator *iter) {
        program *prog = new program();
        parser_data data;
        data.gc = gc;
        data.objects = prog->get_object_set();
        data.file_names_list = prog->get_file_names_list();
        try {
            parse_statement_block(&data, iter, prog);
        }
        catch (compiler_exception ex) {
            prog->release();
            throw;
        }
        return prog;
    }

    void parse_statement_block(parser_data *data, token_iterator *iter, statement_block *block) {
        while (iter->valid()) {
            statement *stmt = parse_statement(data, iter);
            block->add_statement(stmt);
            stmt->release();            
        }
    }

    statement * parse_statement(parser_data *data, token_iterator *iter) {
        if (!iter->valid()) {
            return nullptr;
        }
        token *tok = iter->get();
        switch(tok->type) {
            case token_type::identifier: {
                expression *expr = parse_expression(data, iter);
                statement *result = new statement_expression(
                    data->copy_file_name(tok->file_name), tok->line, expr
                );
                expr->release();
                if (iter->valid()) {
                    tok = iter->get();
                    if (tok->type == token_type::semicolon) {
                        iter->next();
                    }
                }
                return result;
            }
            case token_type::keyword_var:
                iter->next();
                return parse_variable_declaration(data, iter, tok);
            case token_type::dollar_sign:
                iter->next();
                return parse_variable_dollar_declaration(data, iter, tok);
        }
        throw compiler_exception(new compiler_exception_data(
            tok, get_messages()->msg_unable_to_parse_token_sequence())
        );
    }

    declare_variable * parse_variable_declaration(parser_data *data, token_iterator *iter,
            token *keyword) {
        assert(keyword->type == token_type::keyword_var);
        declare_variable *result = new declare_variable(
            data->copy_file_name(keyword->file_name),
            keyword->line
        );
        token *separator = keyword;
        while(true) {
            dynamic_string *name = nullptr;
            if (iter->valid()) {
                token *tok_name = iter->get();
                if (tok_name->type == token_type::identifier) {
                    std::wstring str_name(tok_name->code, tok_name->length);
                    name = new dynamic_string(data->gc, str_name);
                }
            }
            if (!name) {
                result->release();
                throw compiler_exception(new compiler_exception_data(
                    separator, get_messages()->msg_variable_name_is_expected())
                );
            }
            token *tok = iter->next();
            if (!iter->valid()) {
                result->add_variable(name, nullptr);
                name->release();
                return result;
            }
            if (tok->type == token_type::semicolon) {
                iter->next();
                result->add_variable(name, nullptr);
                name->release();
                return result;
            }
            if (tok->type == token_type::comma) {
                separator = tok;
                iter->next();
                result->add_variable(name, nullptr);
                name->release();
            }
            else if (tok->type == token_type::assignment) {
                iter->next();
                try {
                    expression *init_value = parse_expression(data, iter);
                    result->add_variable(name, init_value);
                    name->release();
                    init_value->release();
                }
                catch (compiler_exception ex) {
                    name->release();
                    result->release();
                    throw;
                }
                if (!iter->valid()) {
                    return result;
                }
                tok = iter->get();
                if (tok->type == token_type::semicolon) {
                    iter->next();
                    return result;
                }
                if (tok->type == token_type::comma) {
                    separator = tok;
                    iter->next();
                }
            }
        }
    }

    declare_variable * parse_variable_dollar_declaration(parser_data *data, token_iterator *iter,
            token *dollar) {
        assert(dollar->type == token_type::dollar_sign);
        declare_variable *result = new declare_variable(
            data->copy_file_name(dollar->file_name),
            dollar->line
        );
        dynamic_string *name = nullptr;
        if (iter->valid()) {
            token *tok_name = iter->get();
            if (tok_name->type == token_type::identifier) {
                std::wstring str_name(tok_name->code, tok_name->length);
                name = new dynamic_string(data->gc, str_name);
            }
        }
        if (!name) {
            result->release();
            throw compiler_exception(new compiler_exception_data(
                dollar, get_messages()->msg_variable_name_is_expected())
            );
        }
        token *tok = iter->next();
        if (!iter->valid()) {
            result->add_variable(name, nullptr);
            name->release();
            return result;
        }
        if (tok->type == token_type::semicolon) {
            iter->next();
            result->add_variable(name, nullptr);
            name->release();
            return result;
        }
        if (tok->type == token_type::assignment) {
            iter->next();
            try {
                expression *init_value = parse_expression(data, iter);
                result->add_variable(name, init_value);
                name->release();
                init_value->release();
            }
            catch (compiler_exception ex) {
                name->release();
                result->release();
                throw;
            }
            if (!iter->valid()) {
                return result;
            }
            tok = iter->get();
            if (tok->type == token_type::semicolon) {
                iter->next();
                return result;
            }
        }
        result->release();
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
            if (tok->type == token_type::operato) {
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
            switch(second->type) {
                case token_type::comma:
                case token_type::semicolon:
                case token_type::operato:
                    end_of_sequence = true;
                    break;
                default:
                    iter->next();
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
            expression *result = new expression_variable(obj);
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

        position pos = first->merge_position(second);
        throw compiler_exception(new compiler_exception_data(
            &pos, get_messages()->msg_unable_to_parse_token_sequence())
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
