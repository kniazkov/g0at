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
#include <sstream>
#include "lib/utf8_encoder.h"
#include "lib/io.h"
#include "resources/messages.h"
#include "compiler/scanner/scanner.h"
#include "compiler/scanner/brackets_processor.h"
#include "compiler/parser/parser.h"
#include "compiler/common/exceptions.h"
#include "compiler/analyzer/analyzer.h"
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

        /**
         * @brief Language in which messages are printed (e.g. for compilation errors)
         */
        const char *language;

        /**
         * @brief Save the abstract syntax tree in DOT format
         */
        bool dump_ast;

        /**
         * @brief Constructor
         */
        command_line_interface() {
            source_file_name = nullptr;
            show_version = false;
            language = nullptr;
            dump_ast = false;
        }
    };

    /**
     * @brief Parses command line arguments
     * @param argc Number of command line arguments
     * @param argv Array containing command line arguments
     * @param cli Parsed arguments
     * @return Parsing result (<code>true</code> if successful)
     */
    bool parse_command_line_arguments(int argc, char** argv, command_line_interface *cli) {
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
                    if (0 == std::strcmp(arg, "version")) {
                        cli->show_version = true;
                    }
                    else if (0 == std::strncmp(arg, "lang=", 5)) {
                        cli->language = arg + 5;
                    }
                    else if (0 == std::strcmp(arg, "dump-ast")) {
                        cli->dump_ast = true;
                    }
                }
                else {
                    convert_path_delimiters_to_unix(arg);
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
        if (cli.language != nullptr) {
            set_message_language(cli.language);
        }
        if (cli.show_version) {
            std::cout << encode_utf8(get_messages()->msg_interpreter_description())<< std::endl;
            return 0;
        }
        if (cli.source_file_name != nullptr) {
            bool loaded;
            std::string content = load_file_to_string(cli.source_file_name, &loaded);
            if (!loaded) {
                std::cout << encode_utf8(get_messages()->msg_file_not_found()) 
                    << ": '" << cli.source_file_name << '\'' << std::endl;
                return -1;
            }
            std::wstring code = decode_utf8(content);
            std::vector<token*> all_tokens,
                root_token_list;
            gc_data gc;
            program *prog = nullptr;
            try {
                scanner scan(&all_tokens, cli.source_file_name, code);
                process_brackets(&scan, &all_tokens, &root_token_list);
                token_iterator_over_vector iter(root_token_list);
                prog = parse_program(&gc, &iter);
            }
            catch (compiler_exception exc) {
                std::cerr << exc.get_report();
            }
            for (token *tok : all_tokens) {
                delete tok;
            }
            all_tokens.clear();
            if (prog) {
                perform_a_program_analysis(prog);
                if (cli.dump_ast) {
                    std::stringstream name;
                    name << cli.source_file_name << ".graph";
                    write_string_to_file(name.str().c_str(), prog->generate_graph_description());                   
                }
                console con;
                scope *main = create_main_scope(&gc, &con);
                try {
                    prog->exec(main);
                }
                catch (runtime_exception exr) {
                    std::cerr << encode_utf8(get_messages()->msg_unhandled_exception()) << ": "
                        << exr.get_report();
                }
                prog->release();
                main->release();
            }
            gc.sweep();
            assert(gc.get_count() == 0);
            assert(get_number_of_elements() == 0);
        }

        return 0;
    }
}
