/** @file native_continuation_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Native PC driver boundaries, arbitrary objects and non-replaying VM transitions.
 */
#ifdef NDEBUG
#    undef NDEBUG
#endif
#include "codegen/linker.h"
#include "lib/memory/allocate.h"
#include "lib/strings/string_ext.h"
#include "model/array.h"
#include "model/builtin_function.h"
#include "model/context.h"
#include "model/native_library.h"
#include "model/process.h"
#include "model/thread.h"
#include "unit/analysis_test_support.h"
#include "vm/gc.h"
#include "vm/gc_protocol.h"
#include "vm/gc_stats.h"
#include "vm/vm.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t entries, steps, effects;
static process_t *gc_process;
static bool runtime_gc;
static bool require_runtime_step_gc;
static size_t runtime_step_collections;
static bool force_init_gc;
static bool verify_boundary_counts;
static bool force_step_gc;
static bool step_swept;
static size_t step_collections, complete_collections, exception_collections;
static size_t failed_collections, driver_failures;
static bool require_failure_payload;
static size_t failure_payload_checks;
static size_t between_init_collections, inside_init_collections, pending_init_collections;
static bool require_pending_init_gc;
static bool fail_after_effect, invalid_pc, escaping_exception, language_failure;
static bool host_cancellation, backend_failure, constructor_aborted;

/** @brief The failure fixture's argument is owned only by the active call frame. */
static size_t check_failure_payload(void) {
    if (!require_failure_payload)
        return 0;
    static object_static_string_t payload_key, tag_key;
    size_t found = 0;
    for (context_t *ctx = gc_process->main_thread->context; ctx != get_root_context();
         ctx = ctx->previous) {
        object_t *payload =
            get_object_property(ctx->data, get_static_string_object(&payload_key, U"payload"));
        if (!payload)
            continue;
        object_t *tag = get_object_property(payload, get_static_string_object(&tag_key, U"tag"));
        assert(tag && get_object_integer_value(tag).has_value);
        assert(get_object_integer_value(tag).value == 7654321);
        ++found;
    }
    return found;
}

/** @brief Probe only after step returns, before the host tears down failed frames. */
static void collect_after_step(uint32_t status) {
    if (!force_step_gc)
        return;
    /* Exclusive single-thread fixture: the driver owns only scalar PC/status.
       VM operands and results must already be published, even after frame teardown. */
    assert(status == GOAT_NATIVE_CONTINUE || status == GOAT_NATIVE_COMPLETE
           || status == GOAT_NATIVE_EXCEPTION || status == GOAT_NATIVE_FAILED);
    assert(gc_process->active_threads == 1);
    assert(os_mutex_depth(gc_process->execution_mutex) > 0);
    size_t payloads = check_failure_payload();
    if (require_failure_payload)
        assert(payloads > 0);
    uint64_t before = gc_process->gc_statistics->collections;
    collect_garbage(gc_process);
    assert(check_failure_payload() == payloads);
    failure_payload_checks += payloads;
    assert(gc_process->gc_statistics->collections == before + 1);
    step_swept |= gc_process->gc_statistics->last.before.objects
                  > gc_process->gc_statistics->last.after.objects;
    ++step_collections;
    complete_collections += status == GOAT_NATIVE_COMPLETE;
    exception_collections += status == GOAT_NATIVE_EXCEPTION;
    failed_collections += status == GOAT_NATIVE_FAILED;
}

static uint32_t GOAT_NATIVE_CALL invoke(const goat_native_continuation_context_t *context,
                                        uint32_t pc) {
    ++entries;
    if (invalid_pc) {
        uint32_t next;
        ++steps;
        uint32_t status = context->step(context->state, UINT32_MAX, &next);
        collect_after_step(status);
        return status;
    }
    for (;;) {
        uint32_t next;
        ++steps;
        uint64_t before = runtime_gc ? gc_process->gc_statistics->explicit_collections : 0;
        uint32_t status = context->step(context->state, pc, &next);
        if (runtime_gc)
            runtime_step_collections += gc_process->gc_statistics->explicit_collections - before;
        collect_after_step(status);
        if (force_init_gc && status == GOAT_NATIVE_CONTINUE) {
            context_t *owner = gc_process->main_thread->context;
            /* The completed VM method has returned to its constructor owner.
               Collect before the next INHERIT step can call the pending function. */
            if (owner->init_snapshot && owner->init_index > 0) {
                ++between_init_collections;
                if (owner->init_index < get_init_snapshot_functions(owner->init_snapshot).size)
                    ++pending_init_collections;
                assert(os_mutex_depth(gc_process->execution_mutex) > 0);
                collect_garbage(gc_process);
            }
        }
        if (status != GOAT_NATIVE_CONTINUE)
            return status;
        if (fail_after_effect && effects) {
            ++driver_failures; /* This failure is produced by the adapter, not by step. */
            return GOAT_NATIVE_FAILED;
        }
        pc = next;
    }
}

static operation_result_t mark(object_t **arguments, uint16_t count, thread_t *thread) {
    (void)arguments;
    assert(!count);
    ++effects;
    if (force_init_gc) {
        for (context_t *ctx = thread->context; ctx != get_root_context(); ctx = ctx->previous)
            if (ctx->init_snapshot) {
                ++inside_init_collections;
                break;
            }
    }
    assert(os_mutex_depth(thread->process->execution_mutex) > 0);
    collect_garbage(thread->process);
    if (host_cancellation) {
        thread->process->exit_requested = true;
        thread->process->exit_code = 7;
        thread->process->native_failed = backend_failure;
    }
    return operation_success(get_null_object());
}

static void setup(process_t *process) {
    static builtin_function_object_t storage;
    static const builtin_function_t descriptor = {.name = U"mark", .execute = mark};
    object_t *name = create_string_object(process, STATIC_STRING(U"mark"));
    assert(create_object_property(process->main_thread->context->data,
                                  name,
                                  get_builtin_function_object(&storage, &descriptor),
                                  true)
           == MSTAT_OK);
    DECREF(name);
}

static void deferred(node_t *node, code_builder_t *code, data_builder_t *data) {
    if (node->vtbl->type == NODE_FUNCTION_OBJECT)
        generate_deferred_bytecode_from_node(node, code, data);
    for (size_t i = 0; i < get_node_child_count(node); ++i)
        deferred(get_node_child(node, i), code, data);
}

static void program(const gchar_t *source,
                    unsigned native_functions,
                    size_t expected_entries,
                    size_t expected_effects,
                    bool failure) {
    size_t before = get_allocated_memory_size();
    arena_t *arena = create_arena(8);
    parser_memory_t memory = {arena, arena, arena, arena};
    node_t *node =
        parse_analysis_test_program(&memory, (string_value_t){source, STRLEN(source), false});
    assert(node);
    code_builder_t *builder = create_code_builder();
    data_builder_t *data = create_data_builder();
    generate_bytecode_from_node(node, builder, data);
    deferred(node, builder, data);
    bytecode_t *code = link_code_and_data(builder, data);
    assert(code && validate_bytecode_image(code));
    /* Exercise the same checked decoder used for persisted instruction images,
       before binding process-local continuation descriptors. */
    bytecode_t *decoded = decode_bytecode_image(code->buffer, code->buffer_size);
    assert(decoded && validate_bytecode_image(decoded));
    assert(decoded->buffer_size == code->buffer_size);
    assert(!memcmp(decoded->buffer, code->buffer, code->buffer_size));
    free_bytecode(code);
    code = decoded;
    assert(code->function_count >= native_functions);
    void *image = malloc(code->buffer_size);
    assert(image);
    memcpy(image, code->buffer, code->buffer_size);
    for (unsigned i = 0; i < native_functions; ++i) {
        const bytecode_function_t *function = &code->functions[i];
        uint32_t end = (uint32_t)code->instructions_count;
        for (size_t j = 0; j < code->function_count; ++j)
            if (code->functions[j].body > function->body && code->functions[j].body < end)
                end = code->functions[j].body;
        goat_native_continuation_entry_t entry = {.function_id = function->id,
                                                  .function_instruction = function->instruction,
                                                  .first_instruction = function->body,
                                                  .instruction_count = end - function->body,
                                                  .invoke = invoke};
        /* A syntactically valid descriptor may not shorten or extend the actual body. */
        goat_native_continuation_entry_t invalid = entry;
        ++invalid.instruction_count;
        native_function_descriptor_t *descriptor =
            create_native_continuation_descriptor(&invalid, NULL, NULL);
        assert(descriptor);
        assert(!bind_bytecode_native_function(code, function->instruction, descriptor));
        release_native_function_descriptor(descriptor);
        invalid = entry;
        assert(invalid.instruction_count > 1);
        --invalid.instruction_count;
        descriptor = create_native_continuation_descriptor(&invalid, NULL, NULL);
        assert(descriptor);
        assert(!bind_bytecode_native_function(code, function->instruction, descriptor));
        release_native_function_descriptor(descriptor);
        descriptor = create_native_continuation_descriptor(&entry, NULL, NULL);
        assert(descriptor);
        assert(bind_bytecode_native_function(code, function->instruction, descriptor));
        release_native_function_descriptor(descriptor);
    }
    process_t *process = create_process();
    setup(process);
    process_execution_enter(process);
    assert(configure_gc_boundaries(process, runtime_gc, true));
    process_execution_leave(process);
    gc_stats_t stats;
    if (verify_boundary_counts || runtime_gc)
        assert(configure_gc_statistics(process, &stats));
    gc_process = process;
    thread_t *thread = process->main_thread;
    context_t *caller = thread->context;
    entries = steps = effects = 0;
    step_collections = complete_collections = exception_collections = 0;
    failed_collections = driver_failures = 0;
    failure_payload_checks = 0;
    step_swept = false;
    runtime_step_collections = 0;
    if (force_step_gc) {
        object_t *dead = create_array_object(process, (object_array_t){0});
        operation_result_t inserted = insert_array_elements(dead, 0, &dead, 1);
        assert(!inserted.is_exception);
        DECREF(inserted.value);
        DECREF(dead);
    }
    size_t roots = process->gc_roots_size;
    int status = run(process, code);
    if (status && !failure)
        fprintf(stderr,
                "Unexpected continuation failure, native status %u\n",
                thread->native_status);
    assert((status != 0) == failure);
    if (host_cancellation)
        assert(status == (backend_failure ? 1 : 7));
    assert(entries == expected_entries && effects == expected_effects && steps >= entries);
    if (verify_boundary_counts && !runtime_gc) {
        assert(stats.explicit_collections == expected_effects + step_collections);
        assert(stats.run_end_collections == 1
               && stats.collections == expected_effects + step_collections + 1);
    }
    if (force_step_gc) {
        assert(step_swept);
        assert(step_collections == steps);
        assert(invalid_pc ? step_collections == entries : step_collections > entries);
        assert(complete_collections + exception_collections + failed_collections + driver_failures
               == entries);
        assert(exception_collections == (language_failure || escaping_exception ? entries : 0));
        assert(failed_collections + driver_failures
               == (failure && !language_failure ? entries : 0));
        assert(driver_failures == (fail_after_effect ? entries : 0));
        if (require_failure_payload)
            assert(failure_payload_checks > 0);
    }
    assert(thread->native_attempts == entries && thread->native_retries == 0);
    assert(thread->native_successes == (failure || escaping_exception ? 0 : entries));
    assert(thread->data_stack->size == 0);
    if (runtime_gc)
        assert(!process->gc_session && stats.explicit_collections > 0);
    assert(!thread->gc_call_participant && !thread->gc_join_ready);
    if (require_runtime_step_gc)
        assert(runtime_step_collections > 0);
    assert(process->gc_roots_size == roots);
    if (force_init_gc) {
        assert(between_init_collections > 0);
        assert(inside_init_collections > 0);
        if (require_pending_init_gc)
            assert(pending_init_collections > 0);
        collect_garbage(process);
        assert(process->gc_roots_size == roots);
    }
    if (language_failure) {
        assert(thread->context == get_root_context());
        assert(thread->exception.value == get_static_integer_object(73));
        assert(thread->native_status == GOAT_NATIVE_OK);
        assert(exception_trace_size(thread->exception.trace) >= 2);
        const exception_trace_frame_t *origin = exception_trace_frame(thread->exception.trace, 0);
        assert(origin->instruction < code->instructions_count);
        assert(code->instructions[origin->instruction].opcode == THROW);
    } else {
        assert(!thread->exception.value);
        if (constructor_aborted) {
            assert(thread->context != caller);
            for (context_t *ctx = thread->context; ctx != get_root_context(); ctx = ctx->previous)
                assert(!ctx->init_snapshot);
        } else
            assert(thread->context == caller);
        if (host_cancellation && !backend_failure) {
            assert(!process->native_failed && thread->native_status == GOAT_NATIVE_OK);
            assert(!thread->exception.trace);
        } else if (failure)
            assert(thread->native_status == GOAT_NATIVE_BAD_REQUEST);
    }
    if (fail_after_effect)
        assert(steps == 2);
    assert(!memcmp(image, code->buffer, code->buffer_size));
    free(image);
    destroy_process(process);
    free_bytecode(code);
    destroy_code_builder(builder);
    destroy_data_builder(data);
    destroy_arena(arena);
    assert(get_allocated_memory_size() == before);
}

int main(int argc, char **argv) {
    assert(argc == 2 || argc == 3);
    runtime_gc = argc == 3;
    const char *scenario = argv[1];
    if (!strncmp(scenario, "step-gc-", 8)) {
        scenario += 8;
        force_step_gc = verify_boundary_counts = true;
    }
    require_runtime_step_gc = runtime_gc
                              && (!strcmp(scenario, "temporaries") || !strcmp(scenario, "recursion")
                                  || !strcmp(scenario, "method-recursion"));
    if (!strcmp(scenario, "temporaries")) {
        /* Both elements and the terminal array result have no source-level owner yet. */
        program(U"var caller=func(){return [{var tag=73;},{var tag=91;}];};"
                U"var a=caller();if(a.length!=2 || a[0].tag!=73 || a[1].tag!=91)throw 1;",
                1,
                1,
                0,
                false);
    } else if (!strcmp(scenario, "mixed")) {
        /* Only caller and helper get drivers: the intervening operator handler is VM-only. */
        program(
            U"var caller=func(a,b){if(arguments.length!=3 || arguments[2]!=b)throw 3;return a+b;};"
            U"var helper=func(x){mark();return x;};"
            U"var a={};var value={};a[\"+\"]=func(b){"
            U"if(this!=a || arguments.length!=1 || arguments[0]!=b)throw 1;"
            U"return helper(b);};if(caller(a,value,value)!=value)throw 2;",
            2,
            2,
            1,
            false);
    } else if (!strcmp(scenario, "unwind")) {
        program(U"var caller=func(a){try{return a+1;}catch(e){"
                U"if(e!=73)throw 1;return saved;}finally{mark();}};"
                U"var a={};var saved={};a[\"+\"]=func(x){throw 73;};"
                U"if(caller(a)!=saved)throw 2;",
                1,
                1,
                1,
                false);
    } else if (!strcmp(scenario, "escape")) {
        escaping_exception = true;
        /* Escaping throw must return through C before the outer VM catch executes. */
        program(U"var caller=func(a){try{return a+1;}finally{mark();}};"
                U"var a={};a[\"+\"]=func(x){throw 73;};"
                U"var caught=false;try{caller(a);}catch(e){caught=e==73;}"
                U"if(!caught)throw 2;",
                1,
                1,
                1,
                false);
    } else if (!strcmp(scenario, "trace")) {
        language_failure = true;
        program(U"var caller=func(a){try{return a+1;}finally{mark();}};"
                U"var a={};a[\"+\"]=func(x){throw 73;};caller(a);",
                1,
                1,
                1,
                true);
    } else if (!strcmp(scenario, "strict-bool")) {
        escaping_exception = true;
        program(U"var caller=func(a){return a<1;};"
                U"var a={};a[\"<\"]=func(x){mark();return {};};"
                U"var caught=false;try{caller(a);}catch(e){caught=true;}"
                U"if(!caught)throw 2;",
                1,
                1,
                1,
                false);
    } else if (!strcmp(scenario, "recursion")) {
        program(U"var caller=func(n){mark();if(n==0)return saved;return caller(n-1);};"
                U"var saved={};if(caller(8)!=saved)throw 2;",
                1,
                9,
                9,
                false);
    } else if (!strcmp(scenario, "published-finally")) {
        verify_boundary_counts = true;
        /* Each payload is produced inside the call, not kept by an outside variable.
           The existing controlled probe consults it only while the finally is executing. */
        program(U"var caller=func(){try{return {var tag=73;};}finally{mark();}};"
                U"var result=caller();if(result.tag!=73)throw 1;",
                1,
                1,
                1,
                false);
        escaping_exception = true;
        program(U"var caller=func(){try{throw {var tag=91;};}finally{mark();}};"
                U"var caught=false;try{caller();}catch(e){caught=e.tag==91;}"
                U"if(!caught)throw 2;",
                1,
                1,
                1,
                false);
    } else if (!strcmp(scenario, "method-gc")) {
        /* The factory result has no caller variable: the method frame must root it. */
        program(U"var method=func(){mark();if(this.tag!=73)throw 1;return this;};"
                U"var factory=func(){return {var tag=73;var m=method;};};"
                U"var value=factory().m();if(value.m()!=value)throw 2;"
                U"if(value[\"m\"]()!=value)throw 3;",
                1,
                3,
                3,
                false);
    } else if (!strcmp(scenario, "method-recursion")) {
        /* One function serves repeated receivers; nested returns must restore this. */
        program(U"var method=func(n,expected){if(this!=expected)throw 1;mark();"
                U"if(n!=0 && this.m(n-1,expected)!=this)throw 2;"
                U"if(this!=expected)throw 3;return this;};"
                U"var base={var m=method;};var a=base->{};var b=base->{};"
                U"if(a.m(4,a)!=a || b[\"m\"](3,b)!=b)throw 4;",
                1,
                9,
                9,
                false);
    } else if (!strcmp(scenario, "method-unwind")) {
        /* A VM-only method throws into a native method; neither frame may leak this. */
        program(U"var method=func(expected){var receiver=this;try{receiver.fail(expected);throw 1;}"
                U"catch(e){if(e!=73)throw 2;}"
                U"finally{mark();}if(this!=expected)throw 3;return this;};"
                U"var bomb=func(expected){if(this!=expected)throw 4;mark();throw 73;};"
                U"var base={var m=method;var fail=bomb;};var a=base->{};var b=base->{};"
                U"if(a.m(a)!=a || b.m(b)!=b)throw 5;",
                1,
                2,
                4,
                false);
    } else if (!strcmp(scenario, "init-vm") || !strcmp(scenario, "init-method")) {
        unsigned native = !strcmp(scenario, "init-method");
        program(U"var initialize=func(a){if(a!=null || arguments.length!=0)throw 1;"
                U"mark();this.x=7;return 999;};"
                U"var base={var init=initialize;};"
                U"var obj=base->{if(this.x!=7)throw 2;mark();};"
                U"if(obj.x!=7)throw 3;",
                native,
                native,
                2,
                false);
    } else if (!strcmp(scenario, "init-caller")) {
        program(U"var factory=func(base){return base->{mark();};};"
                U"var inner={const init=func(){mark();this.x=4;};};"
                U"var base={const init=func(){mark();var self=this;"
                U"var nested=inner->{};if(this!=self || nested.x!=4)throw 1;this.x=7;};};"
                U"if(factory(base).x!=7 || factory(base).x!=7)throw 2;",
                1,
                2,
                6,
                false);
    } else if (!strcmp(scenario, "init-static")) {
        program(U"var factory=func(base){return base->{this.x=7;};};"
                U"var base={var init=mark;};if(factory(base).x!=7)throw 1;",
                1,
                1,
                1,
                false);
    } else if (!strcmp(scenario, "init-validation")) {
        program(U"var factory=func(a,b){try{var v=[a,b]->{throw 2;};}"
                U"catch(e){if(e!=\"INVALID_OPERATION\")throw 3;return 7;}return 99;};"
                U"var a={const init=42;};var b={var init=mark;};"
                U"if(factory(a,b)!=7)throw 1;",
                1,
                1,
                0,
                false);
    } else if (!strcmp(scenario, "init-return")) {
        program(U"var factory=func(base){try{var obj=base->{return this.x+1;};"
                U"return 99;}finally{mark();}};"
                U"var base={const init=func(){this.x=6;return 100;};};"
                U"if(factory(base)!=7)throw 1;",
                1,
                1,
                1,
                false);
    } else if (!strcmp(scenario, "init-unwind")) {
        escaping_exception = true;
        program(U"var factory=func(base){return base->{throw 2;};};"
                U"var state={var leaked=null;};"
                U"var base={const init=func(){state.leaked=this;this.x=7;mark();throw 73;};};"
                U"var caught=false;try{factory(base);}catch(e){caught=e==73;}"
                U"if(!caught || state.leaked.x!=7)throw 1;",
                1,
                1,
                1,
                false);
    } else if (!strcmp(scenario, "init-trace")) {
        language_failure = true;
        program(U"var factory=func(base){return base->{throw 2;};};"
                U"var base={const init=func(){try{mark();throw 73;}finally{mark();}};};"
                U"factory(base);",
                1,
                1,
                2,
                true);
    } else if (!strcmp(scenario, "init-native-trace")) {
        language_failure = true;
        program(U"var initialize=func(){mark();throw 73;};"
                U"var base={var init=initialize;};var obj=base->{throw 2;};",
                1,
                1,
                1,
                true);
    } else if (!strcmp(scenario, "init-cancel-direct")
               || !strcmp(scenario, "init-backend-direct")) {
        host_cancellation = constructor_aborted = true;
        backend_failure = !strcmp(scenario, "init-backend-direct");
        program(U"var initialize=func(){mark();};var base={var init=initialize;};"
                U"var obj=base->{throw 2;};",
                1,
                1,
                1,
                true);
    } else if (!strcmp(scenario, "init-cancel")) {
        host_cancellation = true;
        program(U"var factory=func(base){return base->{};};"
                U"var base={const init=func(){mark();};};factory(base);",
                1,
                1,
                1,
                true);
    } else if (!strcmp(scenario, "init-snapshot")) {
        program(U"var original=func(){mark();this.x=this.x+10;};"
                U"var a={var init=original;};"
                U"var b={const init=func(){a.init=func(){this.x=100;};"
                U"original=null;this.x=1;mark();};};"
                U"var value=[a,b]->{};if(value.x!=11)throw 1;",
                1,
                1,
                2,
                false);
    } else if (!strcmp(scenario, "init-gc-closure")) {
        force_init_gc = require_pending_init_gc = true;
        program(U"var factory=func(a,b){return [a,b]->{};};"
                U"var make=func(value){return func(){mark();this.x=this.x+value;};};"
                U"var a={var init=make(40);};"
                U"var b={const init=func(){a.init=null;this.x=2;mark();};};"
                U"var obj=factory(a,b);if(obj.x!=42)throw 1;mark();",
                1,
                1,
                3,
                false);
    } else if (!strcmp(scenario, "init-gc-exception")) {
        force_init_gc = require_pending_init_gc = escaping_exception = true;
        program(U"var factory=func(a,b,last){return [last,a,b]->{throw 2;};};"
                U"var state={var leaked=null;};"
                U"var make=func(value){return func(){mark();this.x=this.x+value;throw 73;};};"
                U"var a={var init=make(40);};"
                U"var b={const init=func(){a.init=null;state.leaked=this;this.x=2;mark();};};"
                U"var last={const init=func(){mark();throw 3;};};"
                U"var caught=false;try{factory(a,b,last);}catch(e){caught=e==73;}"
                U"mark();if(!caught || state.leaked.x!=42)throw 1;"
                U"state.leaked.x=43;if(state.leaked.x!=43)throw 4;",
                1,
                1,
                3,
                false);
    } else if (!strcmp(scenario, "init-gc-reentrant")) {
        force_init_gc = true;
        program(U"var factory=func(base){return base->{};};"
                U"var state={var depth=0;var inner=null;};"
                U"var base={const init=func(){this.depth=state.depth;mark();"
                U"if(state.depth==0){state.depth=1;state.inner=factory(base);state.depth=0;}"
                U"mark();};};var outer=factory(base);"
                U"if(outer.depth!=0 || state.inner.depth!=1 || outer==state.inner)throw 1;",
                1,
                2,
                4,
                false);
    } else if (!strcmp(scenario, "no-replay")) {
        fail_after_effect = true;
        require_failure_payload = true;
        program(U"var caller=func(payload){mark();return 1;};caller({var tag=7654321;});",
                1,
                1,
                1,
                true);
    } else if (!strcmp(scenario, "host-cancel") || !strcmp(scenario, "cancel-backend")) {
        host_cancellation = true;
        backend_failure = !strcmp(scenario, "cancel-backend");
        require_failure_payload = true;
        program(U"var caller=func(payload){mark();return 1;};caller({var tag=7654321;});",
                1,
                1,
                1,
                true);
        assert(steps == 2);
    } else if (!strcmp(scenario, "range")) {
        invalid_pc = true;
        require_failure_payload = true;
        program(U"var caller=func(payload){mark();return 1;};caller({var tag=7654321;});",
                1,
                1,
                0,
                true);
        assert(steps == 1);
    } else {
        assert(!"unknown case");
    }
    return 0;
}
