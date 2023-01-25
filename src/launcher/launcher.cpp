/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <cassert>
#include <cstring>
#include <vector>
#include <iostream>
#include <string>
#include <fstream>
#include <streambuf>
#include "lib/utf8_encoder.h"
#include "compiler/scanner/scanner.h"
#include "compiler/scanner/brackets_processor.h"
#include "compiler/parser/parser.h"
#include "compiler/common/exceptions.h"
#include "model/built_in_functions.h"
#include "model/scope.h"
#include "model/statements.h"
#include "model/exceptions.h"

namespace goat {

    /**
     * @brief Parsed command line arguments 
     */
    struct command_line_interface {
        /**
         * @brief The name of the file containing the source code
         */
        const char *source_file_name;

        /**
         * @brief Arguments passed to the Goat program
         */
        std::vector<const char*> program_arguments;

        /**
         * @brief Show version of the Goat interpreter
         */
        bool show_version;
    };

    /**
     * @brief Parses command line arguments
     * @param argc Number of command line arguments
     * @param argv Array containing command line arguments
     * @param cli Parsed arguments
     * @return Parsing result (<code>true</code> if successful)
     */
    bool parse_command_line_arguments(int argc, char** argv, command_line_interface *cli) {
        cli->source_file_name = nullptr;
        cli->show_version = false;

        int index = 1;
        while(index < argc) {
            char *arg = argv[index];
            if (cli->source_file_name == nullptr) {
                size_t len = std::strlen(arg);
                if (len == 2 && arg[0] == '-') {
                    char ch = arg[1];
                    switch(ch) {
                        case 'v':
                            cli->show_version = true;
                            break;
                    }
                }
                else if (len > 2 && arg[0]== '-' && arg[1] == '-') {
                    arg += 2;
                    if (0 == strcmp(arg, "version")) {
                        cli->show_version = true;
                    }
                }
                else {
                    cli->source_file_name = arg;
                }
            }
            else {
                cli->program_arguments.push_back(arg);
            }
            index++;
        }
        return true;
    }

    /**
     * @brief Debug printer that prints to the buffer
     */
    struct console : public function_print::printer {
        void print(std::wstring str) override {
            std::cout << encode_utf8(str);
        }
    };

    int run(int argc, char** argv) {
        command_line_interface cli;
        if (false == parse_command_line_arguments(argc, argv, &cli)) {
            return -1;
        }
        if (cli.show_version) {
            std::cout << "The Goat programming language interpreter, v. 0.0.1" << std::endl;
            return 0;
        }
        if (cli.source_file_name != nullptr) {
            std::ifstream stream(cli.source_file_name);
            std::string raw;
            stream.seekg(0, std::ios::end);   
            raw.reserve(stream.tellg());
            stream.seekg(0, std::ios::beg);
            raw.assign((std::istreambuf_iterator<char>(stream)),
                        std::istreambuf_iterator<char>());
            std::wstring code = decode_utf8(raw);
            std::vector<token*> all_tokens,
                root_token_list;
            gc_data gc;
            try {
                scanner scan(&all_tokens, cli.source_file_name, code);
                process_brackets(&scan, &all_tokens, &root_token_list);
                
                std::unordered_set<object*> objects;
                parser_data pdata;
                console con;
                pdata.gc = &gc;
                pdata.objects = &objects;
                token_iterator_over_vector iter(root_token_list);
                statement *stmt = parse_statement(&pdata, &iter);
                for (token *tok : all_tokens) {
                    delete tok;
                }
                all_tokens.clear();
                scope *main = create_main_scope(&gc, &con);
                stmt->exec(main);
                stmt->release();
                main->release();
                gc.sweep();
            }
            catch (compiler_exception exc) {
                for (token *tok : all_tokens) {
                    delete tok;
                }
                std::cerr << exc.what() << std::endl;
            }
            catch (runtime_exception exr) {
                std::cerr << exr.what() << std::endl;
            }
            assert(gc.get_count() == 0);
            assert(get_number_of_elements() == 0);
        }

        return 0;
    }
}
