/** @file test_try_catch.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Direct AST tests before try/catch parser integration.
 */
#include <stdio.h>
#include "test_macro.h"
#include "graph/statement.h"
#include "graph/expression.h"
#include "graph/declarations.h"
#include "graph/visualization.h"
#include "graph/scope.h"
#include "analysis/abstract_state.h"
#include "lib/linked_list.h"
#include "lib/allocate.h"
#include "codegen/source_builder.h"
#include "codegen/linker.h"
#include "model/object.h"
#include "model/thread.h"
#include "model/context.h"
#include "vm/vm.h"

static node_t *block(arena_t *arena, node_t *child) {
    list_t *list = create_linked_list(arena);
    if (child) append_item_to_linked_list(list, (value_t){.ptr = child});
    node_t *node = create_statement_list_node(arena);
    fill_statement_list_node(node, list);
    return node;
}

bool test_try_catch_structure(void) {
    arena_t *arena = create_arena(8);
    statement_t *body = create_statement_expression_node(arena, (expression_t*)create_integer_node(arena, 1));
    node_t *handler = block(arena, NULL);
    wchar_t name[] = L"error_extra";
    node_t *node = create_try_catch_node(arena, body, (string_view_t){name,5}, handler);
    name[0] = L'X';
    ASSERT(is_statement(node->vtbl->type));
    ASSERT(!is_expression(node->vtbl->type));
    ASSERT(get_node_child_count(node) == 2);
    ASSERT(get_node_child(node,0) == &body->base && get_node_child(node,1) == handler);
    ASSERT(!get_node_child(node,2) && !get_node_child(node,SIZE_MAX));
    ASSERT(!wcscmp(get_node_child_tag(node,0),L"try"));
    ASSERT(!wcscmp(get_node_child_tag(node,1),L"catch"));
    ASSERT(!get_node_child_tag(node,2));
    node_display_value_t display = get_node_data(node);
    ASSERT(!wcscmp(display.text.data,L"error"));
    ASSERT(!display.text.should_free);
    string_value_t source = generate_goat_code_from_node(node);
    ASSERT(!wcscmp(source.data,L"try 1; catch (error) { }"));
    FREE_STRING(source);
    source_builder_t *builder = create_source_builder();
    generate_indented_goat_code_from_node(node,builder,0);
    source = build_source(builder);
    ASSERT(wcsstr(source.data,L"try\n") && wcsstr(source.data,L"catch (error) { }"));
    FREE_STRING(source);
    destroy_source_builder(builder);
    node->id=1; body->base.id=2; get_node_child(&body->base,0)->id=3; handler->id=4;
    scope_t *scope = create_scope(arena, NULL);
    node->scope = body->base.scope = get_node_child(&body->base,0)->scope = handler->scope = scope;
    source = generate_graph_dot(node);
    ASSERT(wcsstr(source.data,L"try-catch") && wcsstr(source.data,L"error"));
    FREE_STRING(source);
    ASSERT(!can_generate_c_code_from_node(node));
    ASSERT(!generate_c_code_from_node(node).data);
    statement_t *empty_body = create_statement_expression_node(arena,(expression_t*)block(arena,NULL));
    node_t *empty = create_try_catch_node(arena,empty_body,(string_view_t){L"e",1},block(arena,NULL));
    node_t *nested = create_try_catch_node(arena,(statement_t*)empty,(string_view_t){L"outer",5},block(arena,NULL));
    source=generate_goat_code_from_node(nested);
    ASSERT(!wcscmp(source.data,L"try try { } catch (e) { } catch (outer) { }"));
    FREE_STRING(source);
    builder=create_source_builder();
    generate_indented_goat_code_from_node(empty,builder,0);
    source=build_source(builder);
    ASSERT(!wcscmp(source.data,L"try { }\ncatch (e) { }\n"));
    FREE_STRING(source); destroy_source_builder(builder);
    node_t *container=block(arena,&body->base);
    ASSERT(container->vtbl->insert_child_before(container,node,&body->base));
    ASSERT(get_node_child(container,0)==node);
    destroy_arena(arena);
    return true;
}

/** @brief Emits a throwing test statement without adding a production throw AST node. */
static instr_index_t emit_throw(node_t *node, code_builder_t *code, data_builder_t *data) {
    instr_index_t first=add_instruction(code,(instruction_t){.opcode=ILOAD32,.arg1=5000});
    add_instruction(code,(instruction_t){.opcode=THROW});
    return first;
}

bool test_try_catch_bytecode(void) {
    for (int throws=0; throws<4; throws++) {
        arena_t *arena=create_arena(8);
        statement_t *body=create_statement_expression_node(arena,(expression_t*)create_integer_node(arena,1));
        node_vtbl_t throwing=*body->base.vtbl;
        throwing.generate_bytecode=emit_throw;
        if (throws) body->base.vtbl=&throwing;
        /* Return in catch exposes the bound value through an actual function call. */
        node_t *ret=create_return_node(arena,create_variable_node(arena,(string_view_t){L"error",5}));
        node_t *node=create_try_catch_node(arena,body,(string_view_t){L"error",5},block(arena,ret));
        if (throws == 2) {
            node = create_try_catch_node(arena, body, (string_view_t){L"error",5}, block(arena,NULL));
        } else if (throws == 3) {
            statement_t *again = create_statement_expression_node(arena,(expression_t*)create_integer_node(arena,1));
            again->base.vtbl = &throwing;
            node_t *inner = create_try_catch_node(arena, body, (string_view_t){L"inner",5},
                block(arena, &again->base));
            node = create_try_catch_node(arena, (statement_t*)inner,
                (string_view_t){L"error",5}, block(arena,ret));
        }
        code_builder_t *code=create_code_builder();
        data_builder_t *data=create_data_builder();
        add_instruction(code,(instruction_t){.opcode=ARG,.arg1=4});
        add_instruction(code,(instruction_t){.opcode=FUNC});
        add_instruction(code,(instruction_t){.opcode=CALL});
        add_instruction(code,(instruction_t){.opcode=END});
        instr_index_t start=generate_bytecode_from_node(node,code,data);
        ASSERT(start==4 && get_instruction(code,start)->opcode==TRY);
        instr_index_t target=get_instruction(code,start)->arg1;
        ASSERT(get_instruction(code,target)->opcode==ENTER);
        ASSERT(get_instruction(code,target+1)->opcode==VAR);
        add_instruction(code,(instruction_t){.opcode=ILOAD32,.arg1=7});
        add_instruction(code,(instruction_t){.opcode=RET});
        bytecode_t *bytecode=link_code_and_data(code,data);
        process_t *proc=create_process();
        context_t *initial=proc->main_thread->context;
        object_t *key = create_string_object(proc,(string_value_t){L"error",5,false});
        ASSERT(create_object_property(initial->data,key,get_static_integer_object(99),false)==MSTAT_OK);
        DECREF(key);
        ASSERT(run(proc,bytecode)==0);
        ASSERT(get_object_integer_value(get_object_property(initial->data,key)).value==99);
        ASSERT(proc->main_thread->context==initial);
        ASSERT(proc->main_thread->data_stack->size==1);
        ASSERT(get_object_integer_value(peek_object_from_stack(proc->main_thread->data_stack,0)).value==((throws==1 || throws==3)?5000:7));
        destroy_process(proc);
        free_bytecode(bytecode);
        destroy_code_builder(code); destroy_data_builder(data); destroy_arena(arena);
    }
    return true;
}

bool test_try_catch_analysis_placeholder(void) {
    arena_t *arena=create_arena(8);
    variable_declaration_pair_t decl=create_synthetic_variable_declaration_node(arena,(string_view_t){L"x",1});
    abstract_state_t *state=create_abstract_state(arena);
    set_in_abstract_state(state,decl.declarator,make_integer_constant_element(arena,1));
    node_t *node=create_try_catch_node(arena,(statement_t*)create_return_node(arena,NULL),
        (string_view_t){L"e",1},block(arena,NULL));
    ASSERT(execute_node(node,state,arena)==state);
    ASSERT(state->control_flow==FLOW_NORMAL);
    ASSERT(get_from_abstract_state(state,decl.declarator)->type==LATTICE_TOP);
    state->control_flow=FLOW_RETURN;
    set_in_abstract_state(state,decl.declarator,make_integer_constant_element(arena,2));
    execute_node(node,state,arena);
    ASSERT(state->control_flow==FLOW_RETURN);
    ASSERT(get_from_abstract_state(state,decl.declarator)->type==LATTICE_INTEGER_CONSTANT);
    destroy_abstract_state(state); destroy_arena(arena);
    return true;
}
