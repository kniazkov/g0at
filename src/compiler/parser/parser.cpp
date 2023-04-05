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
#include "model/data_description.h"
#include "model/expressions.h"
#include "model/statements.h"
#include "model/strings.h"
#include "model/built_in_functions.h"

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
     * @brief Descriptor of a binary operation
     */
    struct binary_operation_descriptor {
        /**
         * @brief Symbols denoting this operation
         */
        const wchar_t *symbols;

        /**
         * @brief Functor that creates a binary operation from two operands
         */
        binary_operation * (*creator)(expression *left, expression *right);
    };

    /**
     * @brief Descriptor of a assignment operation
     */
    struct assignment_descriptor {
        /**
         * @brief Symbols denoting this operation
         */
        const wchar_t *symbols;

        /**
         * @brief Functor that creates a binary operation from two operands
         */
        assignment * (*creator)(assignable_expression *left, expression *right);
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
    variable_declaration * parse_variable_declaration(parser_data *data, token_iterator *iter,
        token *keyword);

    /**
     * @brief Tries to parse the list of tokens, started from the dollar sign, 
     *   as a variable declaration
     * @param data Data needed for parsing
     * @param iter Iterator by token
     * @param keyword Token containing dollar sign
     * @return A statement
     */
    variable_declaration * parse_variable_dollar_declaration(parser_data *data,
        token_iterator *iter, token *dollar);

    /**
     * @brief Tries to parse the list of tokens as a data declaration, i.e. such sequence:
     *   <code>name:type</code>
     * @param data Data needed for parsing
     * @param iter Iterator by token
     * @param name Pointer to variable name
     * @param proto_list Pointer to a list containing prototype names
     * @return Parsing result, <code>true</code> if token sequence contains data declaration
     */
    bool parse_data_declaration(parser_data *data, token_iterator *iter, dynamic_string **name,
        std::vector<std::wstring> *proto_list);

    /**
     * @brief Tries to parse the list of tokens as a variable(s) declaration
     * @param data Data needed for parsing
     * @param iter Iterator by token
     * @param first_token First token, containing "var" keyword or dollar sign
     * @param multiple Multiple declarations are allowed
     * @return A statement
     */
    variable_declaration * parse_variable_declaration(parser_data *data, token_iterator *iter,
            token *first_token, bool multiple);

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
     * @param first First token (identifier)
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
            const binary_operation_descriptor *descr);

    /**
     * @brief Parses a chain of operators and expressions, 
     *   and when specified operators are found, combines two expressions and one operator
     *   into an assignment operation
     * @param chain Chain of operators and expressions
     */
    void parse_assignments(std::list<token_chain_item> *chain);

    /**
     * @brief Tries to parse the list of tokens as a function declaration
     * @param data Data needed for parsing
     * @param iter Iterator by token
     * @param first First token (keyword or dollar sign)
     * @return Expression
     */
    expression * parse_function_declaration(parser_data *data, token_iterator *iter, token *first);

    /**
     * @brief Tries to parse the list of tokens as a property acess
     * @param left_part Left part of the property access (i.e. expression before the dot)
     * @param data Data needed for parsing
     * @param iter Iterator by token
     * @param first First token (dot)
     * @return Expression
     */
    property_access *parse_property_access(expression *left_part, parser_data *data,
        token_iterator *iter, token *first);

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

    variable_declaration * parse_variable_declaration(parser_data *data, token_iterator *iter,
            token *keyword) {
        assert(keyword->type == token_type::keyword_var);\
        return parse_variable_declaration(data, iter, keyword, true);
   }

    variable_declaration * parse_variable_dollar_declaration(parser_data *data,
        token_iterator *iter, token *dollar) {
        assert(dollar->type == token_type::dollar_sign);
        return parse_variable_declaration(data, iter, dollar, false);
    }

    bool parse_data_declaration(parser_data *data, token_iterator *iter, dynamic_string **name,
            std::vector<std::wstring> *proto_list) {
        if (!iter->valid()) {
            return false;
        }
        token *tok_name = iter->get();
        if (tok_name->type == token_type::identifier) {
            std::wstring str_name(tok_name->code, tok_name->length);
            *name = new dynamic_string(data->gc, str_name);
        }
        else {
            return false;
        }
        token *tok = iter->next();
        if (tok && tok->type == token_type::colon) {
            token *next = iter->next();
            if (!next || next->type != token_type::identifier) {
                throw compiler_exception(new compiler_exception_data(
                    tok, get_messages()->msg_expected_type_name())
                );
            }
            iter->next();
            std::wstring proto(next->code, next->length);
            proto_list->push_back(proto);
        }
        return true;
    }

    variable_declaration * parse_variable_declaration(parser_data *data, token_iterator *iter, 
            token *first_token, bool multiple) {
        variable_declaration *result = new variable_declaration(
            data->copy_file_name(first_token->file_name),
            first_token->line
        );
        token *separator = first_token;
        while(true) {
            dynamic_string *name = nullptr;
            std::vector<std::wstring> proto_list;
            bool has_name = parse_data_declaration(data, iter, &name, &proto_list);
            if (!has_name) {
                result->release();
                throw compiler_exception(new compiler_exception_data(
                    separator, get_messages()->msg_variable_name_is_expected())
                );
            }
            token *tok = iter->get();
            if (!iter->valid()) {
                data_descriptor *descriptor = new data_descriptor(true, name, nullptr, proto_list);
                name->release();
                result->add_variable(descriptor);
                descriptor->release();
                return result;
            }
            if (tok->type == token_type::semicolon) {
                iter->next();
                data_descriptor *descriptor = new data_descriptor(true, name, nullptr, proto_list);
                name->release();
                result->add_variable(descriptor);
                descriptor->release();
                return result;
            }
            if (tok->type == token_type::comma) {
                if (multiple) {
                    separator = tok;
                    iter->next();
                    data_descriptor *descriptor = new data_descriptor(true, name, nullptr,
                        proto_list);
                    name->release();
                    result->add_variable(descriptor);
                    descriptor->release();
                }
                else {
                    name->release();
                    result->release();
                    throw compiler_exception(new compiler_exception_data(
                        tok, get_messages()->msg_multiple_declarations_are_not_allowed())
                    );
                }
            }
            else if (tok->type == token_type::operato && tok->length == 1 && tok->code[0] == '=') {
                iter->next();
                try {
                    expression *init_value = parse_expression(data, iter);
                    data_descriptor *descriptor = new data_descriptor(true, name, init_value,
                        proto_list);
                    name->release();
                    init_value->release();
                    result->add_variable(descriptor);
                    descriptor->release();
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
                    if (multiple) {
                        separator = tok;
                        iter->next();
                    }
                    else {
                        result->release();
                        throw compiler_exception(new compiler_exception_data(
                            tok, get_messages()->msg_multiple_declarations_are_not_allowed())
                        );
                    }
                }
            } // declaration with initial value
        } // while(true)
    }

    /**
     * @brief Descriptors for operators *, /, %
     */
    const binary_operation_descriptor mul_div_mod[] = {
        { L"*", multiplication::creator }
    };

    /**
     * @brief Descriptors for operators +, -
     */
    const binary_operation_descriptor plus_minus[] = {
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

        do {
            if (chain.size() == 1) break;
            parse_binary_operators(&chain, 1, mul_div_mod);
            if (chain.size() == 1) break;
            parse_binary_operators(&chain, 2, plus_minus);
            if (chain.size() == 1) break;
            parse_assignments(&chain);
        } while(false);

        assert(chain.size() == 1 && chain.begin()->type == token_chain_item::is_expression);
        return chain.begin()->ptr.expr;
    }

    expression * parse_expression_without_operators(parser_data *data, token_iterator *iter) {
        assert(iter->valid());
        token *first = iter->get();
        iter->next();
        if (first->type == token_type::identifier) {
            return parse_expression_begins_with_identifier(data, iter, first);
        }
        if (first->type == token_type::string) {
            token_string *str = (token_string*)first;
            dynamic_string *obj = new dynamic_string(data->gc, str->data);
            data->objects->insert(obj);
            expression *result = new object_as_expression(obj);
            obj->release();
            return result;
        }
        if (first->type == token_type::integer) {
            token_number *num = (token_number*)first;
            return new constant_integer_number(num->data.int_value);
        }
        if (first->type == token_type::keyword_function) {
            return parse_function_declaration(data, iter, first);
        }
        if (first->type == token_type::keyword_system) {
            //if (iter->valid()) {
            //}
            return new system_object();
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

        if (second->type == token_type::dot) {
            /*
                This is a property access:
                    obj.x
            */
            std::wstring var_name(first->code, first->length);
            dynamic_string *obj = new dynamic_string(data->gc, var_name);
            data->objects->insert(obj);
            expression *left_part = new expression_variable(obj);
            obj->release();
            try {
                property_access *result = parse_property_access(left_part, data, iter, second);
                left_part->release();
                return result;
            }
            catch(compiler_exception ex) {
                left_part->release();
                throw;
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
            const binary_operation_descriptor *descr) {
        auto iter = chain->begin(),
            end = chain->end();
        iter++;
        while(iter != end) {
            bool found = false;
            auto left =  std::prev(iter),
                right =  std::next(iter);
            if (iter->type == token_chain_item::is_token && right != end &&
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
                    auto next =  std::next(right);
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

    /**
     * @brief Descriptors for assignment operators
     */
    const assignment_descriptor assignments[] = {
        { L"=", simple_assignment::creator }
    };

    void parse_assignments(std::list<token_chain_item> *chain) {
        chain->reverse();
        auto iter = chain->begin(),
            end = chain->end();
        iter++;
        while(iter != end) {
            bool found = false;
            auto left =  std::prev(iter),
                right =  std::next(iter);
            if (iter->type == token_chain_item::is_token && right != end &&
                    left->type == token_chain_item::is_expression &&
                    right->type == token_chain_item::is_expression) {
                int index = 0;
                for (; index < sizeof(assignments) / sizeof(assignment_descriptor); index++) {
                    if (0 == std::memcmp(assignments[index].symbols, iter->ptr.tok->code,
                                iter->ptr.tok->length * sizeof(wchar_t))) {
                        found = true;
                        break;
                    }
                }
                if (found) {
                    token_chain_item item;
                    auto next =  std::next(right);
                    item.type = token_chain_item::is_expression;
                    assignable_expression *right_expr = right->ptr.expr->to_assignable_expression();
                    item.ptr.expr = assignments[index].creator(right_expr, left->ptr.expr);
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
        chain->reverse();
    }

    expression * parse_function_declaration(parser_data *data, token_iterator *iter,
            token *first) {
        do {
            if (!iter->valid()) {
                break;
            }
            
            token *second = iter->get();
            if (second->type == token_type::comma || second->type == token_type::semicolon) {
                break;
            }
            token_brackets_pair *args_token = nullptr;
            token_brackets_pair *body_token = nullptr;
            if (second->type == token_type::brackets_pair) {
                token_brackets_pair *pair = (token_brackets_pair*)second;
                if (pair->opening_bracket == '(') {
                    args_token = pair;

                } else if (pair->opening_bracket == '{') {
                    body_token = pair;
                }
            } else {
                position pos = first->merge_position(second);
                throw compiler_exception(new compiler_exception_data(
                    &pos, get_messages()->msg_unable_to_parse_token_sequence())
                );
            }

            token *third = iter->next();
            if (third != nullptr &&
                    third->type != token_type::comma && third->type != token_type::semicolon) {
                if (third->type == token_type::brackets_pair) {
                    token_brackets_pair *pair = (token_brackets_pair*)third;
                    if (pair->opening_bracket == '(') {
                        if (args_token != nullptr) {
                            throw compiler_exception(new compiler_exception_data(
                                pair, get_messages()->msg_function_arguments_already_defined())
                            );
                        }
                        else if (body_token != nullptr) {
                            throw compiler_exception(new compiler_exception_data(
                                pair, get_messages()->msg_function_body_must_be_after_arguments())
                            );
                        }
                        args_token = pair;
                    } else if (pair->opening_bracket == '{') {
                        if (body_token != nullptr) {
                            throw compiler_exception(new compiler_exception_data(
                                pair, get_messages()->msg_function_body_already_defined())
                            );
                        }
                        body_token = pair;
                    }
                    iter->next();
                } else {
                    position pos = second->merge_position(third);
                    throw compiler_exception(new compiler_exception_data(
                        &pos, get_messages()->msg_unable_to_parse_token_sequence())
                    );
                }
            }

            if (body_token != nullptr) {
                token_iterator_over_vector body_iterator(body_token->tokens);
                statement_block *body = new statement_block();
                parse_statement_block(data, &body_iterator, body);
                function_declaration *result = new function_declaration({}, body);
                body->release();
                return result;
            }

        } while(false);
        return new object_as_expression(get_function_that_does_nothing_instance());
    }

    property_access *parse_property_access(expression *left_part, parser_data *data,
            token_iterator *iter, token *first) {
        base_string *name = nullptr;
        if (iter->valid()) {
            token *tok = iter->get();
            iter->next();
            if (tok->type == token_type::identifier) {
                std::wstring str(tok->code, tok->length);
                name = new dynamic_string(data->gc, str);
                data->objects->insert(name);
            }
        }
        if (name == nullptr) {
            throw compiler_exception(new compiler_exception_data(
                first, get_messages()->msg_expected_property_name())
            );
        }
        return new property_access(left_part, name);
    }
}
