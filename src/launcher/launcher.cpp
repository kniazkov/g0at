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
#include "lib/utf8_encoder.h"
#include "lib/io.h"
#include "resources/messages.h"
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

        /**
         * @brief Language in which messages are printed (e.g. for compilation errors)
         */
        const char *language;

        /**
         * @brief Constructor
         */
        command_line_interface() {
            source_file_name = nullptr;
            show_version = false;
            language = nullptr;
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
            try {
                scanner scan(&all_tokens, cli.source_file_name, code);
                process_brackets(&scan, &all_tokens, &root_token_list);
                console con;
                token_iterator_over_vector iter(root_token_list);
                program *prog = parse_program(&gc, &iter);
                for (token *tok : all_tokens) {
                    delete tok;
                }
                all_tokens.clear();
                scope *main = create_main_scope(&gc, &con);
                prog->exec(main);
                prog->release();
                main->release();
                gc.sweep();
            }
            catch (compiler_exception exc) {
                for (token *tok : all_tokens) {
                    delete tok;
                }
                std::cerr << exc.get_report() << std::endl;
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
