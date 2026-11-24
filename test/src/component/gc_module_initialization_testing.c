/** @file gc_module_initialization_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Collection inside nested initializers and competing real VM gate waits.
 */
#ifdef NDEBUG
#    undef NDEBUG
#endif
#include "common/module_compilation.h"
#include "lib/memory/allocate.h"
#include "model/array.h"
#include "model/builtin_function.h"
#include "model/module_files.h"
#include "model/string_builder.h"
#include "vm/gc_protocol.h"
#include "vm/gc_stats.h"
#include "vm/vm.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    const gchar_t *key;
    const char *handler;
    const char *expression;
    size_t effects;
} operator_case_t;

static const operator_case_t operator_cases[] = {
    {"add",
     U"+",
     "func(x){calls+=1;if(this.marker[0]!=21){throw [0];}return x[0];}",
     "return lhs()+rhs();",
     2},
    {"unary",
     U"-",
     "func(){calls+=1;if(this.marker[0]!=21){throw [0];}return 42;}",
     "return -lhs();",
     1},
    {"not",
     U"!",
     "func(){calls+=1;if(this.marker[0]!=21){throw [0];}return true;}",
     "if(!lhs()){return 42;}return 0;",
     1},
    {"and",
     U"&&",
     "func(x){calls+=1;if(this.marker[0]!=21){throw [0];}return x[0]==42;}",
     "if(lhs()&&rhs()){return 42;}return 0;",
     2},
    {"or",
     U"||",
     "func(x){calls+=1;if(this.marker[0]!=21){throw [0];}return x[0]==42;}",
     "if(lhs()||rhs()){return 42;}return 0;",
     2},
    {"less",
     U"<",
     "func(x){calls+=1;if(this.marker[0]!=21){throw [0];}return x[0]==42;}",
     "if(lhs()<rhs()){return 42;}return 0;",
     2},
    {"opposite",
     U"!=",
     "func(x){calls+=1;if(this.marker[0]!=21){throw [0];}return x[0]!=42;}",
     "if(lhs()==rhs()){return 42;}return 0;",
     2},
    {"index",
     U"[]",
     "func(x){calls+=1;if(this.marker[0]!=21){throw [0];}return x[0];}",
     "return lhs()[rhs()];",
     2},
    {"setter",
     U"[]=",
     "func(i,v){calls+=1;if(this.marker[0]!=21){throw [0];}if(i[0]!=42||v[0]!=42){throw [0];}}",
     "lhs()[rhs()]=rhs();return 42;",
     3},
    {"fallback", NULL, "func(){}", "return lhs()[\"result\"];", 1},
    {"fallback-write", NULL, "func(){}", "lhs()[\"result\"]=rhs()[0];return 42;", 2}};
static const operator_case_t *operator_case;

typedef struct {
    const char *name;
    const char *expression;
    const char *expected;
    const char *removed;
    bool builder;
} serialization_case_t;

static const serialization_case_t serialization_cases[] = {
    {"object-string-add",
     "str(\"prefix:\")+awaitStart()",
     "\"prefix:\"+str(awaitStart())",
     "\"prefix:\"+str({external items=[];})",
     false},
    {"object-char-add",
     "'x'+awaitStart()",
     "\"x\"+str(awaitStart())",
     "\"x\"+str({external items=[];})",
     false},
    {"object-append",
     "checkedBuilder(makeBuilder().append(awaitStart()))",
     "\"changed:\"+str(awaitStart())",
     "\"changed:\"+str({external items=[];})",
     true},

    {"array", "awaitStart().toString()", "str(awaitStart())", "\"[ ]\"", false},
    {"detached", "detached(awaitStart())", "str(awaitStart())", "\"[ ]\"", false},
    {"separator",
     "awaitStart().toString(str(\"::\"))",
     "awaitStart().toString(\"::\")",
     "\"\"",
     false},
    {"char-separator", "awaitStart().toString(':')", "awaitStart().toString(':')", "\"\"", false},
    {"string-add",
     "str(\"prefix:\")+awaitStart()",
     "\"prefix:\"+str(awaitStart())",
     "\"prefix:[ ]\"",
     false},
    {"char-add", "'x'+awaitStart()", "\"x\"+str(awaitStart())", "\"x[ ]\"", false},
    {"append",
     "checkedBuilder(makeBuilder().append(awaitStart()))",
     "\"changed:\"+str(awaitStart())",
     "\"changed:[ ]\"",
     true},
    {"builder-add",
     "checkedBuilder(makeBuilder()+awaitStart())",
     "\"changed:\"+str(awaitStart())",
     "\"changed:[ ]\"",
     true},
    {"shift",
     "checkedBuilder(makeBuilder()<<awaitStart())",
     "\"changed:\"+str(awaitStart())",
     "\"changed:[ ]\"",
     true},
    {"compound",
     "(func(){var b=makeBuilder();b+=awaitStart();return checkedBuilder(b);})()",
     "\"changed:\"+str(awaitStart())",
     "\"changed:[ ]\"",
     true},
    {"self",
     "(func(){var b=makeBuilder();b.append(awaitStart());b.append(b);return checkedBuilder(b);})()",
     "(\"changed:\"+str(awaitStart()))+(\"changed:\"+str(awaitStart()))",
     "\"changed:[ ]changed:[ ]\"",
     true}};
static const serialization_case_t *serialization_case;
static bool serialization_object;
static object_t *observed_builder; // borrowed; the language call owns the receiver
static size_t builder_calls;

static bool concurrent, recursive, failed, entry;
static bool inherited, shadow, local_seen, bound, lexical, bound_heap;
static bool origin, origin_method, origin_operator, origin_callable, origin_startup, origin_builtin;
static bool observation, observation_remove;
static size_t observation_arguments;
static object_t *observed_graph; // borrowed; only the builtin argument keeps it alive
static size_t builtin_origin_calls;
static module_files_t *test_files;
static object_t *descendant;
static size_t observations;
static uint64_t initial_collections;
static bool copying, copying_flat, copying_remove;
static size_t copying_calls;
static object_t *copy_source;

static operation_result_t await_gate(object_t **args, uint16_t count, thread_t *thread) {
    (void)args;
    assert(!count && thread->initialization_depth);
    process_t *p = thread->process;
    if (concurrent) {
        while (!p->initialization_waiters)
            wait_os_condition(p->threads_changed, p->execution_mutex);
        gc_session_t *s = p->gc_session;
        assert(s->initialization_waiters == 1 && s->initializers == 1);
        gc_participant_t *owner = s->participants;
        while (owner && owner->thread != thread)
            owner = owner->next;
        assert(owner && owner->blocked == 1 && !owner->published);
        size_t published = 0;
        for (gc_participant_t *t = s->participants; t; t = t->next)
            published += t->published;
        /* One waiter on the initialization gate; main may still be reaching join. */
        assert(published >= 1);
        assert(!inherited || local_seen);
        if (origin_method || origin_operator || origin_callable) {
            /* Selection is already complete. Remove the receiver's last edge
             * to the function while the entry check is asleep. */
            static object_static_string_t selected_key;
            object_t *key = get_static_string_object(&selected_key,
                                                     origin_operator   ? U"+"
                                                     : origin_callable ? U"()"
                                                                       : U"method");
            assert(set_field_cell_value(descendant,
                                        get_own_field_cell(descendant, key),
                                        get_null_object())
                   == MSTAT_OK);
        }
        if (copying) {
            assert(copying_calls == 1);
            static object_static_string_t key;
            object_t *name = get_static_string_object(&key, U"changed");
            object_t *value = create_integer_object(p, 42);
            assert(create_object_property(copy_source, name, value, false) == MSTAT_OK);
            get_own_field_cell(copy_source, name)->modifier = ACCESS_EXTERNAL;
            DECREF(value);
            if (copying_remove)
                set_inheritance_prototype_count(copy_source, 0);
        }
        if (observation)
            assert(observation_arguments == 2); // unused format argument never waits
        if (serialization_case && serialization_case->builder) {
            assert(builder_calls == 1);
            string_builder_t *buffer = get_string_builder_storage(observed_builder);
            for (size_t i = 0; i < 512; ++i)
                append_char(buffer, U'x');
            buffer->length = 0;
            buffer->data[0] = 0;
            append_string(buffer, U"changed:");
            field_cell_t *length = get_own_field_cell(observed_builder, get_string_length());
            object_t *value = create_integer_object(p, (int64_t)buffer->length);
            DECREF(length->object);
            length->object = value;
        }
        if (observation_remove) {
            operation_result_t cleared = clear_array_elements(observed_graph);
            assert(!cleared.is_exception);
            DECREF(cleared.value);
        }
        if (shadow) {
            /* An earlier slot appears while the lookup is asleep. Resumption
             * must restart before inspecting the now failed inherited scope. */
            static object_static_string_t key;
            object_t *value = create_integer_object(p, 42);
            assert(create_object_property(descendant,
                                          get_static_string_object(&key, U"result"),
                                          value,
                                          false)
                   == MSTAT_OK);
            get_own_field_cell(descendant, get_static_string_object(&key, U"result"))->modifier =
                ACCESS_EXTERNAL;
            DECREF(value);
        }
    }
    initial_collections = p->gc_statistics->explicit_collections;
    return operation_success(get_null_object());
}

static operation_result_t await_start(object_t **args, uint16_t count, thread_t *thread) {
    if (serialization_case && count == 1) {
        assert(!thread->gc_call_participant);
        observed_builder = args[0];
        assert(get_string_builder_storage(observed_builder));
        ++builder_calls;
        INCREF(observed_builder);
        return operation_success(observed_builder);
    }
    assert(!count);
    process_t *p = thread->process;
    while (!p->initialization_count && !(observation && observations)) {
        size_t depth = process_execution_suspend(p);
        process_execution_resume(p, depth);
    }
    if (copying) {
        ++copying_calls;
        object_t *scope = module_file_scope(module_files_get(test_files, 1));
        if (copying_flat) {
            copy_source = create_user_defined_object(p, (object_array_t){&scope, 1});
            static object_static_string_t flat_key;
            assert(create_object_property(copy_source,
                                          get_static_string_object(&flat_key, U"flat"),
                                          get_function_flat(),
                                          true)
                   == MSTAT_OK);
            get_own_field_cell(copy_source, get_static_string_object(&flat_key, U"flat"))
                ->modifier = ACCESS_EXTERNAL;
            object_t *cycle = create_array_object(p, (object_array_t){0});
            operation_result_t inserted = insert_array_elements(cycle, 0, &cycle, 1);
            assert(!inserted.is_exception);
            DECREF(inserted.value);
            static object_static_string_t marker;
            assert(create_object_property(copy_source,
                                          get_static_string_object(&marker, U"marker"),
                                          cycle,
                                          true)
                   == MSTAT_OK);
            get_own_field_cell(copy_source, get_static_string_object(&marker, U"marker"))
                ->modifier = ACCESS_EXTERNAL;
            DECREF(cycle);
        } else {
            copy_source = scope;
            INCREF(scope);
        }
        return operation_success(copy_source);
    }
    if (observation) {
        ++observation_arguments;
        object_t *cycle = create_array_object(p, (object_array_t){0});
        operation_result_t inserted = insert_array_elements(cycle, 0, &cycle, 1);
        assert(!inserted.is_exception);
        DECREF(inserted.value);
        object_t *items[] = {module_file_scope(module_files_get(test_files, 1)), cycle};
        observed_graph = create_array_object(p, (object_array_t){items, 2});
        DECREF(cycle);
        if (serialization_object) {
            object_t *root = get_root_object();
            object_t *wrapper = create_user_defined_object(p, (object_array_t){&root, 1});
            static object_static_string_t key;
            object_t *name = get_static_string_object(&key, U"items");
            assert(create_object_property(wrapper, name, observed_graph, true) == MSTAT_OK);
            get_own_field_cell(wrapper, name)->modifier = ACCESS_EXTERNAL;
            DECREF(observed_graph);
            return operation_success(wrapper);
        }
        return operation_success(observed_graph);
    }
    if (origin && !origin_builtin) {
        static object_static_string_t work_key, method_key, marker_key;
        object_t *scope = module_file_scope(module_files_get(test_files, 1));
        object_t *key = get_static_string_object(&work_key, U"work");
        field_cell_t *cell = get_own_field_cell(scope, key);
        while (!is_function_object(cell->object)) {
            size_t depth = process_execution_suspend(p);
            process_execution_resume(p, depth);
            cell = get_own_field_cell(scope, key);
        }
        object_t *function = cell->object;
        INCREF(function);
        assert(set_field_cell_value(scope, cell, get_null_object()) == MSTAT_OK);
        if (!origin_method && !origin_operator && !origin_callable)
            return operation_success(function);
        object_t *root = get_root_object();
        object_t *object = create_user_defined_object(p, (object_array_t){&root, 1});
        object_t *name = get_static_string_object(&method_key,
                                                  origin_operator   ? U"+"
                                                  : origin_callable ? U"()"
                                                                    : U"method");
        assert(create_object_property(object, name, function, false) == MSTAT_OK);
        descendant = object; // borrowed fixture pointer, deliberately not a GC root
        get_own_field_cell(object, name)->modifier = ACCESS_EXTERNAL;
        DECREF(function);
        object_t *number = create_integer_object(p, 21);
        object_t *marker = create_array_object(p, (object_array_t){&number, 1});
        DECREF(number);
        name = get_static_string_object(&marker_key, U"marker");
        assert(create_object_property(object, name, marker, true) == MSTAT_OK);
        get_own_field_cell(object, name)->modifier = ACCESS_EXTERNAL;
        DECREF(marker);
        return operation_success(object);
    }
    if (operator_case && operator_case->key) {
        static object_static_string_t handler_key, operator_key;
        object_t *handler = get_object_property(module_file_scope(module_files_main(test_files)),
                                                get_static_string_object(&handler_key, U"handler"));
        object_t *scope = module_file_scope(module_files_get(test_files, 1));
        object_t *key = get_static_string_object(&operator_key, operator_case->key);
        assert(create_object_property(scope, key, handler, true) == MSTAT_OK);
        get_own_field_cell(scope, key)->modifier = ACCESS_EXTERNAL;
    }
    if (bound) {
        /* Build an explicit capture/lexical fixture for a real BLOAD/BSTORE.
         * This bypasses function-entry waits so the tested boundary is the
         * declaration lookup itself, not an earlier callable-origin check. */
        static object_static_string_t key;
        object_t *name = get_static_string_object(&key, U"target");
        field_cell_t *original =
            get_own_field_cell(module_file_scope(module_files_main(test_files)), name);
        object_t *storage = module_file_scope(module_files_get(test_files, 1));
        object_t *value = create_integer_object(p, 42);
        assert(create_object_property(storage, name, value, false) == MSTAT_OK);
        DECREF(value);
        field_cell_t *cell = get_own_field_cell(storage, name);
        cell->modifier = ACCESS_EXTERNAL;
        cell->binding_unit = retain_module_identity(original->binding_unit);
        cell->declaration_id = original->declaration_id;
        assert(cell->binding_unit && cell->declaration_id);
        function_access_t *access = &thread->context->access;
        for (size_t i = 0; i < access->captures->size; ++i) {
            field_capture_t *capture = access->captures->data[i];
            DECREF(capture->storage);
            FREE(capture);
        }
        access->captures->size = 0;
        if (lexical) {
            for (size_t i = 0; i < access->lexical_scopes->size; ++i) {
                object_t *scope = access->lexical_scopes->data[i];
                DECREF(scope);
            }
            access->lexical_scopes->size = 0;
            assert(add_lexical_scope(access, storage));
        } else
            assert(add_function_field_capture(access, storage, cell));
        if (bound_heap) {
            /* CHECKBOUND has already run against the old capture. Install the
             * waiting scope while evaluating RHS so BSTORE itself must wait. */
            object_t *number = create_integer_object(p, 42);
            object_t *array = create_array_object(p, (object_array_t){&number, 1});
            DECREF(number);
            return operation_success(array);
        }
    }
    return operation_success(get_null_object());
}

static operation_result_t
origin_echo(object_t *receiver, object_t **args, uint16_t count, thread_t *thread) {
    assert(receiver == module_file_scope(module_files_get(test_files, 1)));
    assert(count == 2 && !thread->gc_call_participant);
    /* Admission ended before this arbitrary builtin callback. */
    assert(gc_session_request(thread->process->gc_session));
    assert(gc_session_poll(thread->process->gc_session) == GC_DEFERRED);
    object_t *first = get_array_elements(args[0]).items[0];
    object_t *extra = get_array_elements(args[1]).items[0];
    assert(get_object_integer_value(first).value == 42);
    assert(get_object_integer_value(extra).value == 99);
    ++builtin_origin_calls;
    INCREF(first);
    return operation_success(first);
}

static operation_result_t make_receiver(object_t **args, uint16_t count, thread_t *thread) {
    (void)args;
    assert(!count);
    object_t *prototype = module_file_scope(module_files_get(test_files, 1));
    object_t *object = create_user_defined_object(thread->process, (object_array_t){&prototype, 1});
    object_t *number = create_integer_object(thread->process, 21);
    object_t *marker = create_array_object(thread->process, (object_array_t){&number, 1});
    DECREF(number);
    static object_static_string_t key;
    object_t *name = get_static_string_object(&key, U"marker");
    assert(create_object_property(object, name, marker, true) == MSTAT_OK);
    get_own_field_cell(object, name)->modifier = ACCESS_EXTERNAL;
    DECREF(marker);
    return operation_success(object);
}

static operation_result_t seen_local(object_t **args, uint16_t count, thread_t *thread) {
    (void)args;
    assert(!count && thread->process->initialization_count);
    local_seen = true;
    return operation_success(get_null_object());
}

static operation_result_t probe(object_t **args, uint16_t count, thread_t *thread) {
    (void)args;
    assert(!count && thread->initialization_depth == 2);
    process_t *p = thread->process;
    assert(p->initialization_count == 2);
    gc_session_t *session = p->gc_session;
    assert(session->initializers == (recursive ? 0 : 2));
    assert(thread->gc_initialization_depth == (recursive ? 0 : 2));
    if (recursive)
        assert(p->gc_statistics->explicit_collections == 0);
    else {
        assert(p->gc_statistics->explicit_collections > initial_collections + 10);
        assert(p->gc_statistics->last.after.objects < 150);
    }
    /* This callback is unreviewed: even a fully counted initializer cannot
       publish arbitrary C locals merely because it owns the model lock. */
    assert(!thread->gc_call_participant);
    assert(gc_session_request(p->gc_session));
    assert(gc_session_poll(p->gc_session) == GC_DEFERRED);
    ++observations;
    return operation_success(get_null_object());
}

int main(int argc, char **argv) {
    assert(argc == 2);
    for (size_t i = 0; i < sizeof(operator_cases) / sizeof(*operator_cases); ++i) {
        char name[80];
        snprintf(name,
                 sizeof(name),
                 "operator-%s-%s",
                 operator_cases[i].name,
                 strstr(argv[1], "failure") ? "failure" : "success");
        if (!strcmp(name, argv[1]))
            operator_case = operator_cases + i;
    }
    for (size_t i = 0; i < sizeof(serialization_cases) / sizeof(*serialization_cases); ++i) {
        char name[100];
        snprintf(name,
                 sizeof(name),
                 "observation-serialize-%s-%s-%s",
                 serialization_cases[i].name,
                 strstr(argv[1], "remove") ? "remove" : "stable",
                 strstr(argv[1], "failure") ? "failure" : "success");
        if (!strcmp(argv[1], name))
            serialization_case = serialization_cases + i;
    }
    serialization_object = serialization_case && strstr(argv[1], "object-") != NULL;
    copying = strstr(argv[1], "copying-") != NULL;
    copying_flat = copying && strstr(argv[1], "flat");
    copying_remove = copying_flat && strstr(argv[1], "remove");
    origin = strstr(argv[1], "origin") != NULL;
    origin_method = origin && strstr(argv[1], "method") != NULL;
    origin_operator = origin && strstr(argv[1], "operator") != NULL;
    origin_callable = origin && strstr(argv[1], "callable") != NULL;
    origin_startup = origin && strstr(argv[1], "startup") != NULL;
    origin_builtin = origin && strstr(argv[1], "builtin") != NULL;
    bound = strstr(argv[1], "bound") != NULL;
    lexical = strstr(argv[1], "lexical") != NULL;
    bound_heap = bound && strstr(argv[1], "heap") != NULL;
    observation = strstr(argv[1], "observation") != NULL;
    observation_remove = observation && strstr(argv[1], "remove") != NULL;
    bool property = copying || observation || origin || operator_case || bound
                    || strstr(argv[1], "property") != NULL;
    inherited = strstr(argv[1], "inherited") != NULL;
    shadow = strstr(argv[1], "shadow") != NULL;
    concurrent = property || strstr(argv[1], "workers") != NULL;
    recursive = strstr(argv[1], "recursive") != NULL;
    failed = strstr(argv[1], "failure") != NULL;
    entry = strstr(argv[1], "entry") != NULL;
    assert(!concurrent || !recursive);
    size_t baseline = get_allocated_memory_size();
    char *main_source =
        concurrent
            ? (failed ? "var f=func(){return include \"init.goat\";};var a=spawn(f);var b=spawn(f);"
                        "var sum=0;try{join(a);}catch(e){sum+=e[0];}"
                        "try{join(b);}catch(e){sum+=e[0];}external result=sum;"
                      : "var f=func(){return include \"init.goat\";};var a=spawn(f);var b=spawn(f);"
                        "external result=join(a).result+join(b).result;")
            : (failed ? "var sum=0;try{include \"init.goat\";}catch(e){sum=e[0];}"
                        "try{include \"init.goat\";}catch(e){sum+=e[0];}external result=sum;"
                      : "external result=(include \"init.goat\").result*2;");
    char *property_source = NULL;
    if (property) {
        const char *operation = strstr(argv[1], "write")   ? "shared.result=42;"
                                : strstr(argv[1], "check") ? "shared.result+=0;"
                                                           : "";
        const char *tail =
            (shadow || observation_remove || copying_remove) && failed
                ? "var sum=0;try{join(a);}catch(e){sum=e[0];}external result=sum+join(b);"
            : failed ? "var sum=0;try{join(a);}catch(e){sum+=e[0];}"
                       "try{join(b);}catch(e){sum+=e[0];}external result=sum;"
                     : "external result=join(a).result+join(b);";
        property_source = ALLOC(2048);
        snprintf(property_source,
                 1024,
                 "var a=spawn(func(){return include \"init.goat\";});"
                 "var b=spawn(func(){awaitStart();%s%sreturn shared.result;});%s",
                 inherited ? "if(shared.local!=17){throw [0];}seenLocal();" : "",
                 operation,
                 tail);
        if (bound) {
            bool heap = bound_heap;
            const char *bound_operation = heap                       ? "target=awaitStart();"
                                          : strstr(argv[1], "write") ? "target=42;"
                                          : strstr(argv[1], "check") ? "target+=0;"
                                                                     : "";
            snprintf(property_source,
                     1024,
                     "external var target=0;var a=spawn(func(){return include \"init.goat\";});"
                     "var b=spawn(func(){%s%sreturn target%s;});%s",
                     heap ? "" : "awaitStart();",
                     bound_operation,
                     heap ? "[0]" : "",
                     tail);
        }
        if (operator_case) {
            snprintf(property_source,
                     2048,
                     "external var calls=0;external var effects=0;external handler=%s;"
                     "var lhs=func(){effects+=1;return makeReceiver();};"
                     "var rhs=func(){effects+=1;return [42];};"
                     "var a=spawn(func(){return include \"init.goat\";});"
                     "var b=spawn(func(){awaitStart();%s});%s",
                     operator_case->handler,
                     operator_case->expression,
                     tail);
        }
        if (origin) {
            const char *worker =
                origin_startup    ? "var b=spawn(awaitStart(),[42],[99]);"
                : origin_method   ? "var b=spawn(func(){return awaitStart().method([42],[99]);});"
                : origin_operator ? "var b=spawn(func(){return awaitStart()+[42];});"
                                  : "var b=spawn(func(){return awaitStart()([42],[99]);});";
            snprintf(property_source,
                     2048,
                     "var a=spawn(func(){return include \"init.goat\";});%s%s",
                     worker,
                     tail);
        }
        if (observation) {
            const char *expression =
                strstr(argv[1], "method")
                    ? "str(\"prefix:%0:%1:%0:%2\").format([21],awaitStart(),[99])"
                : strstr(argv[1], "format")
                    ? "format(\"prefix:%0:%1:%0:%2\",[21],awaitStart(),[99])"
                : strstr(argv[1], "println") ? "println(awaitStart())"
                : strstr(argv[1], "print")   ? "print(awaitStart())"
                                             : "str(awaitStart())";
            bool output = strstr(argv[1], "print") != NULL;
            bool formatting = strstr(argv[1], "format") || strstr(argv[1], "method");
            const char *expected =
                output               ? "null"
                : observation_remove ? (formatting ? "\"prefix:[21]:[ ]:[21]:[99]\"" : "\"[ ]\"")
                : formatting         ? "format(\"prefix:%0:%1:%0:%2\",[21],awaitStart(),[99])"
                                     : "str(awaitStart())";
            if (serialization_case) {
                expression = serialization_case->expression;
                expected =
                    observation_remove ? serialization_case->removed : serialization_case->expected;
            }
            snprintf(
                property_source,
                2048,
                "var checkedBuilder=func(b){var text=str(b);if(b.length!=text.length){throw [0];}"
                "return text;};"
                "var detached=[].toString;var makeBuilder=func(){var b=StringBuilder -> {};"
                "b.append(\"prefix:\");return awaitStart(b);};"
                "var a=spawn(func(){return include \"init.goat\";});"
                "var b=spawn(func(){if(format(\"%%1\",awaitStart(),[21])!=\"[21]\"){throw "
                "[0];}var text=%s;if(text!=%s){throw [0];}return 42;});%s",
                expression,
                expected,
                tail);
        }
        if (copying) {
            const char *method = copying_flat ? "flat" : "clone";
            char expression[128];
            snprintf(expression,
                     sizeof(expression),
                     strstr(argv[1], "detached") ? "detached(awaitStart())" : "awaitStart().%s()",
                     method);
            snprintf(property_source,
                     2048,
                     "var detached={}.%s;var a=spawn(func(){return include \"init.goat\";});"
                     "var b=spawn(func(){var copied=%s;%sreturn copied.changed;});%s",
                     method,
                     expression,
                     copying_flat ? "if(copied.marker[0]!=copied.marker){throw [0];}" : "",
                     tail);
        }
        if (origin_builtin)
            snprintf(property_source,
                     2048,
                     "var a=null;var begin=func(){a=spawn(func(){return include \"init.goat\";});"
                     "awaitStart();return [42];};"
                     "var b=spawn(func(){return shared.echo(begin(),[99]);});"
                     "var sum=0;try{sum=join(b);}catch(e){sum=e[0];}"
                     "try{sum+=(join(a)).result;}catch(e){sum+=e[0];}external result=sum;");
        main_source = property_source;
    }
    char *init_source = "awaitGate();var held=[21];var leaf=include \"leaf.goat\";"
                        "external var result=held[0]+leaf.result;";
    char origin_source[1024];
    if (origin && !origin_builtin) {
        snprintf(origin_source,
                 sizeof(origin_source),
                 "external var calls=0;external var work=func(x,extra){calls+=1;%s"
                 "if(extra!=null&&extra[0]!=99){throw [0];}return x[0];};"
                 "awaitGate();var held=[21];var leaf=include \"leaf.goat\";"
                 "external var result=held[0]+leaf.result;",
                 origin_method || origin_operator || origin_callable
                     ? "if(this.marker[0]!=21){throw [0];}"
                     : "");
        init_source = origin_source;
    }
    char *leaf_source =
        failed ? "var i=0;while(i<100){var a=[];a.push(a);i+=1;}probe();throw [42];"
               : "var i=0;while(i<100){var a=[];a.push(a);i+=1;}probe();external result=21;";
    module_source_t input[] = {{.path = "main.goat", .name = "main.goat", .source = main_source},
                               {.path = "init.goat", .name = "init.goat", .source = init_source},
                               {.path = "leaf.goat", .name = "leaf.goat", .source = leaf_source}};
    size_t targets[] = {1, 1, 2};
    for (size_t i = 0; i < 3; ++i) {
        input[i].length = strlen(input[i].source);
        source_include_error_t error = {0};
        assert(parse_source_includes(input[i].path,
                                     input[i].source,
                                     input[i].length,
                                     &input[i].includes,
                                     &error));
        input[i].targets = i == 0 ? targets : targets + 2;
    }
    module_sources_t sources = {.files = input, .count = 3};
    module_compilation_t *compiled = NULL;
    module_compilation_error_t error = {0};
    if (!compile_module_sources(&sources, &compiled, &error)) {
        fprintf(stderr, "%s\n", error.message);
        abort();
    }
    void *image = malloc(compiled->code->buffer_size);
    assert(image);
    memcpy(image, compiled->code->buffer, compiled->code->buffer_size);
    process_t *p = create_process();
    module_files_t *files = NULL;
    assert(create_module_files_from_snapshot(p, compiled->metadata, compiled->code, &files));
    test_files = files;
    assert(prepare_process_launch(p, module_files_identity(files), 0, "/gc/main.goat"));
    static const builtin_function_t descriptors[] = {{.name = U"awaitGate", .execute = await_gate},
                                                     {.name = U"probe", .execute = probe}};
    builtin_function_object_t storage[2] = {0};
    object_static_string_t keys[2] = {0};
    for (size_t i = 0; i < 2; ++i)
        assert(create_object_property(module_file_scope(module_files_get(files, i + 1)),
                                      get_static_string_object(&keys[i], descriptors[i].name),
                                      get_builtin_function_object(&storage[i], &descriptors[i]),
                                      true)
               == MSTAT_OK);
    static const builtin_function_t ready = {.name = U"awaitStart", .execute = await_start};
    builtin_function_object_t ready_storage = {0};
    object_static_string_t ready_key = {0}, shared_key = {0}, local_key = {0}, seen_key = {0};
    static const builtin_function_t seen = {.name = U"seenLocal", .execute = seen_local};
    builtin_function_object_t seen_storage = {0};
    static const builtin_function_t maker = {.name = U"makeReceiver", .execute = make_receiver};
    builtin_function_object_t maker_storage = {0};
    object_static_string_t maker_key = {0};
    if (property) {
        object_t *scope = module_file_scope(module_files_main(files));
        assert(create_object_property(scope,
                                      get_static_string_object(&ready_key, U"awaitStart"),
                                      get_builtin_function_object(&ready_storage, &ready),
                                      true)
               == MSTAT_OK);
        if (operator_case)
            assert(create_object_property(scope,
                                          get_static_string_object(&maker_key, U"makeReceiver"),
                                          get_builtin_function_object(&maker_storage, &maker),
                                          true)
                   == MSTAT_OK);
        descendant = module_file_scope(module_files_get(files, 1));
        if (inherited) {
            for (size_t i = 0; i < 3; ++i) {
                object_t *next = create_user_defined_object(p, (object_array_t){&descendant, 1});
                if (i)
                    DECREF(descendant);
                descendant = next;
            }
        }
        if (inherited) {
            object_t *local = create_integer_object(p, 17);
            object_t *key = get_static_string_object(&local_key, U"local");
            assert(create_object_property(descendant, key, local, true) == MSTAT_OK);
            get_own_field_cell(descendant, key)->modifier = ACCESS_EXTERNAL;
            DECREF(local);
            assert(create_object_property(scope,
                                          get_static_string_object(&seen_key, U"seenLocal"),
                                          get_builtin_function_object(&seen_storage, &seen),
                                          true)
                   == MSTAT_OK);
        }
        assert(create_object_property(scope,
                                      get_static_string_object(&shared_key, U"shared"),
                                      descendant,
                                      true)
               == MSTAT_OK);
    }
    if (inherited)
        DECREF(descendant); // main scope owns the complete prototype chain
    static const builtin_function_t echo = {.name = U"echo", .execute_method = origin_echo};
    builtin_function_object_t echo_storage = {0};
    object_static_string_t echo_key = {0};
    if (origin_builtin) {
        object_t *scope = module_file_scope(module_files_get(files, 1));
        object_t *key = get_static_string_object(&echo_key, U"echo");
        assert(create_object_property(scope,
                                      key,
                                      get_builtin_function_object(&echo_storage, &echo),
                                      true)
               == MSTAT_OK);
        get_own_field_cell(scope, key)->modifier = ACCESS_EXTERNAL;
    }
    gc_stats_t stats;
    assert(configure_gc_statistics(p, &stats));
    process_execution_enter(p);
    assert(configure_gc_boundaries(p, true, true));
    if (!recursive)
        process_execution_leave(p);
    int status = run_module_file(p, module_files_get(files, entry ? 1 : 0));
    if (status && !(entry && failed) && p->main_thread->exception.value) {
        string_value_t message = convert_object_to_string(p->main_thread->exception.value);
        char *text = encode_utf8(message.data);
        fprintf(stderr, "failure: %s\n", text);
        FREE(text);
        FREE_STRING(message);
    }
    assert((status != 0) == (entry && failed));
    if (recursive)
        process_execution_leave(p);
    assert(observations == 1);
    assert(copying_calls == (copying ? 1 : 0));
    if (serialization_case) {
        size_t expected_reads = 2;
        if (!failed && !observation_remove)
            expected_reads += !strcmp(serialization_case->name, "self") ? 2 : 1;
        assert(observation_arguments == expected_reads);
        assert(builder_calls == (serialization_case->builder ? 1 : 0));
    }
    assert(builtin_origin_calls == (origin_builtin && !failed ? 1 : 0));
    object_static_string_t result_key = {0};
    object_t *result =
        get_object_property(module_file_scope(module_files_get(files, entry ? 1 : 0)),
                            get_static_string_object(&result_key, U"result"));
    if (!(entry && failed))
        assert(result && get_object_integer_value(result).value == (entry ? 42 : 84));
    if (operator_case) {
        static object_static_string_t calls_key, effects_key;
        object_t *scope = module_file_scope(module_files_main(files));
        object_t *calls =
            get_object_property(scope, get_static_string_object(&calls_key, U"calls"));
        object_t *effects =
            get_object_property(scope, get_static_string_object(&effects_key, U"effects"));
        assert(get_object_integer_value(calls).value == (!failed && operator_case->key ? 1 : 0));
        /* Logical dispatch waits before RHS; all other operands run first. */
        size_t expected =
            failed && (!strcmp(operator_case->name, "and") || !strcmp(operator_case->name, "or"))
                ? 1
                : operator_case->effects;
        assert(get_object_integer_value(effects).value == (int64_t)expected);
    }
    if (origin && !origin_builtin) {
        static object_static_string_t calls_key;
        object_t *scope = module_file_scope(module_files_get(files, 1));
        object_t *calls =
            get_object_property(scope, get_static_string_object(&calls_key, U"calls"));
        assert(get_object_integer_value(calls).value == (failed ? 0 : 1));
    }
    if (bound) {
        static object_static_string_t target_key;
        object_t *original = get_object_property(module_file_scope(module_files_main(files)),
                                                 get_static_string_object(&target_key, U"target"));
        assert(get_object_integer_value(original).value == 0);
    }
    if (failed) {
        exception_t first = module_file_exception(module_files_get(files, 1));
        exception_t leaf = module_file_exception(module_files_get(files, 2));
        assert(first.value && first.value == leaf.value && first.trace == leaf.trace);
    }
    assert(module_file_state(module_files_get(files, 1))
           == (failed ? MODULE_FILE_FAILED : MODULE_FILE_INITIALIZED));
    assert(!p->initializing_file && !p->initialization_count && !p->initialization_waiters);
    assert(!p->gc_session && !p->gc_roots_size);
    thread_t *t = p->main_thread;
    do {
        assert(!t->initialization_depth && !t->gc_initialization_depth);
        assert(!t->gc_call_participant && !t->gc_join_ready);
        t = t->next;
    } while (t != p->main_thread);
    assert(!memcmp(image, compiled->code->buffer, compiled->code->buffer_size));
    free(image);
    destroy_process(p);
    free_module_compilation(compiled);
    clear_module_compilation_error(&error);
    for (size_t i = 0; i < 3; ++i)
        clear_source_includes(&input[i].includes);
    FREE(property_source);
    assert(get_allocated_memory_size() == baseline);
    return 0;
}
