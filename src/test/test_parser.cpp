/*
    Copyright 2023 Ivan Kniazkov

    Use of this source code is governed by an MIT-style license
    that can be found in the LICENSE.txt file or at https://opensource.org/licenses/MIT.
*/

#include <string>
#include <vector>
#include "unit_testing.h"
#include "dbg_printer.h"
#include "compiler/common/exceptions.h"
#include "compiler/scanner/scanner.h"
#include "compiler/scanner/brackets_processor.h"
#include "compiler/parser/parser.h"
#include "model/statements.h"
#include "model/exceptions.h"

namespace goat {

    /**
     * @brief Tests the parser on a short piece of source code (one statement)
     * @param code Source code
     * @param expected_result Result expected to be printed
     */
    static bool test_parsing_statement(std::wstring code, std::wstring expected_result) {
        std::vector<token*> all_tokens,
            root_token_list;
        gc_data gc;
        bool was_compiler_exception = false;
        bool was_runtime_exception = false;
        try {
            scanner scan(&all_tokens, nullptr, code);
            process_brackets(&scan, &all_tokens, &root_token_list);
            std::unordered_set<object*> objects;
            parser_data pdata;
            pdata.gc = &gc;
            pdata.objects = &objects;
            token_iterator_over_vector iter(root_token_list);
            statement *stmt = parse_statement(&pdata, &iter);
            dbg_printer printer;
            scope *main = create_main_scope(&gc, &printer);
            stmt->exec(main);
            stmt->release();
            main->release();
            assert_equals(std::wstring, expected_result, printer.stream.str());
        }
        catch (compiler_exception exc) {
            was_compiler_exception = true;
        }
        catch (runtime_exception exr) {
            was_runtime_exception = true;
        }
        assert_equals(bool, false, was_compiler_exception);
        assert_equals(bool, false, was_runtime_exception);
        for (token *tok : all_tokens) {
            delete tok;
        }
        assert_equals(unsigned int, 0, gc.get_count());        
        return true;
    }

    bool test_parsing_hello_world() {
        return test_parsing_statement(L"print(\"it works.\");", L"it works.");
    }

    bool test_parsing_integer() {
        return test_parsing_statement(L"print(1024);", L"1024");
    }

    bool test_parsing_operator_plus() {
        return test_parsing_statement(L"print(2 + 3);", L"5");
    }
}
