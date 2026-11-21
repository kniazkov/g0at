/** @file gc_protocol_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Collection sessions, nested exclusions, missing participants and lifecycle.
 */
#ifdef NDEBUG
#    undef NDEBUG
#endif
#include "lib/memory/allocate.h"
#include "model/array.h"
#include "model/context.h"
#include "vm/gc_protocol.h"

#include <assert.h>

typedef struct {
    process_t *process;
    thread_t *worker;
    gc_session_t *session;
    unsigned phase, acknowledged;
} concurrent_case_t;

/* The collector owns the same mutex while observing each phase. Acknowledgements
   prevent the worker from advancing early; no sleeps or scheduler timing assumptions. */
static void barrier(concurrent_case_t *c, unsigned phase) {
    c->phase = phase;
    assert(broadcast_os_condition(c->process->threads_changed) == OS_THREAD_OK);
    while (c->acknowledged < phase)
        assert(wait_os_condition(c->process->threads_changed, c->process->execution_mutex)
               == OS_THREAD_OK);
}

static void *protocol_worker(void *argument) {
    concurrent_case_t *c = argument;
    process_t *p = c->process;
    process_execution_enter(p);
    process_execution_enter(p);
    gc_participant_t token = {0};
    assert(gc_participant_enter(c->session, c->worker, &token));
    assert(gc_participant_block(&token) && gc_participant_block(&token));
    object_t *leaf = create_integer_object(p, 7654321);
    object_t *value = create_array_object(p, (object_array_t){&leaf, 1});
    DECREF(leaf);
    barrier(c, 1);
    assert(os_mutex_depth(p->execution_mutex) == 2 && token.blocked == 2);
    assert(gc_participant_unblock(&token));
    assert(!gc_participant_publish(&token));
    barrier(c, 2);
    assert(token.blocked == 1);
    gc_root_handle_t root = register_gc_root(p, value);
    assert(root.serial && gc_participant_unblock(&token));
    assert(gc_participant_publish(&token));
    barrier(c, 3);
    /* Publication is withdrawn before even removing the owning root. */
    assert(gc_participant_withdraw(&token));
    assert(unregister_gc_root(p, root));
    barrier(c, 4);
    root = register_gc_root(p, value);
    assert(root.serial && gc_participant_publish(&token));
    barrier(c, 5);
    assert(gc_participant_withdraw(&token));
    object_array_t elements = get_array_elements(value);
    assert(elements.size == 1 && get_object_integer_value(elements.items[0]).value == 7654321);
    assert(unregister_gc_root(p, root));
    DECREF(value);
    destroy_thread(c->worker);
    c->worker = NULL;
    assert(!token.session);
    barrier(c, 6);
    process_execution_leave(p);
    process_execution_leave(p);
    return NULL;
}

static void concurrent_protocol(void) {
    size_t before = get_allocated_memory_size();
    process_t *p = create_process();
    process_execution_enter(p);
    gc_session_t session = {0};
    gc_participant_t main = {0};
    assert(gc_session_begin(&session, p, true));
    assert(gc_participant_enter(&session, p->main_thread, &main));
    concurrent_case_t c = {.process = p,
                           .worker = create_thread(p, get_root_context()),
                           .session = &session};
    assert(gc_participant_publish(&main));
    assert(gc_session_request(&session));
    /* A model record exists before its OS thread starts: missing registration refuses. */
    assert(gc_session_poll(&session) == GC_DEFERRED && session.requested);
    os_thread_t *worker = NULL;
    assert(create_os_thread(&worker, protocol_worker, &c, 0) == OS_THREAD_OK);
    for (unsigned phase = 1; phase <= 6; ++phase) {
        while (c.phase < phase)
            assert(wait_os_condition(p->threads_changed, p->execution_mutex) == OS_THREAD_OK);
        bool collect = phase == 3 || phase == 5 || phase == 6;
        assert(gc_session_poll(&session) == (collect ? GC_COLLECTED : GC_DEFERRED));
        assert(session.requested == !collect);
        if (collect) {
            assert(gc_session_poll(&session) == GC_IDLE);
            if (phase != 6)
                assert(gc_session_request(&session));
        }
        c.acknowledged = phase;
        assert(broadcast_os_condition(p->threads_changed) == OS_THREAD_OK);
    }
    assert(session.requests == 3 && session.collections == 3 && session.refusals == 4);
    /* Join outside the lock; the worker still has its final barrier to leave. */
    size_t depth = process_execution_suspend(p);
    assert(join_os_thread(worker, NULL) == OS_THREAD_OK);
    assert(destroy_os_thread(worker) == OS_THREAD_OK);
    process_execution_resume(p, depth);
    assert(gc_participant_leave(&main) && gc_session_end(&session));
    process_execution_leave(p);
    destroy_process(p);
    assert(get_allocated_memory_size() == before);
}

/** A released model lock is not enough: only a reviewed call can publish its wait. */
static void call_wait_protocol(void) {
    size_t before = get_allocated_memory_size();
    process_t *p = create_process();
    thread_t *thread = p->main_thread;
    gc_session_t session = {0};
    gc_participant_t token = {0};
    process_execution_enter(p);
    assert(gc_session_begin(&session, p, true));
    assert(gc_participant_enter(&session, thread, &token));
    assert(gc_participant_block(&token));
    assert(!gc_call_wait_begin(thread));
    thread->gc_call_participant = &token;
    assert(gc_participant_block(&token));
    assert(!gc_call_wait_begin(thread));
    assert(gc_participant_unblock(&token));
    thread->initialization_depth = 1;
    assert(!gc_call_wait_begin(thread));
    thread->initialization_depth = 0;
    process_execution_enter(p);
    assert(!gc_call_wait_begin(thread));
    /* Direct module entry has one reviewed wrapper, not arbitrary host recursion. */
    thread->gc_call_extra_lock_depth = 1;
    assert(gc_call_wait_begin(thread) == &token);
    assert(token.published && !token.blocked);
    assert(!gc_call_wait_begin(thread));
    size_t depth = process_execution_suspend(p);
    assert(depth == 2);
    process_execution_enter(p);
    /* Services reacquire only one lock level, regardless of the original entry. */
    assert(gc_call_wait_pause(thread) == &token);
    assert(!token.published && token.blocked == 1);
    assert(gc_session_request(&session));
    assert(gc_session_poll(&session) == GC_DEFERRED);
    assert(!gc_call_wait_pause(thread));
    gc_call_wait_resume(&token);
    assert(token.published && !token.blocked);
    assert(gc_session_poll(&session) == GC_COLLECTED);
    process_execution_leave(p);
    process_execution_resume(p, depth);
    gc_call_wait_end(&token);
    assert(!token.published && token.blocked == 1);
    thread->gc_call_extra_lock_depth = 0;
    thread->gc_call_participant = NULL;
    gc_call_wait_end(NULL);
    gc_call_wait_resume(NULL);
    assert(gc_participant_unblock(&token));
    assert(gc_participant_leave(&token) && gc_session_end(&session));
    process_execution_leave(p);
    process_execution_leave(p);
    destroy_process(p);
    assert(get_allocated_memory_size() == before);
}

int main(void) {
    call_wait_protocol();
    concurrent_protocol();
    size_t before = get_allocated_memory_size();
    process_t *p = create_process();
    gc_session_t s = {0}, duplicate = {0};
    gc_participant_t main = {0}, other = {0};
    assert(!gc_session_begin(&s, p, true));
    process_execution_enter(p);
    assert(!gc_session_begin(&s, p, false));
    assert(gc_session_begin(&s, p, true));
    assert(!gc_session_begin(&duplicate, p, true));
    assert(gc_session_poll(&s) == GC_IDLE);
    assert(gc_session_request(&s) && gc_session_request(&s));
    assert(gc_session_poll(&s) == GC_DEFERRED && s.requested);
    assert(gc_participant_enter(&s, p->main_thread, &main));
    assert(!gc_participant_enter(&s, p->main_thread, &other));
    assert(gc_participant_block(&main) && gc_participant_block(&main));
    size_t depth = process_execution_suspend(p);
    assert(!gc_participant_unblock(&main));
    process_execution_resume(p, depth);
    assert(!gc_participant_publish(&main));
    assert(gc_participant_unblock(&main));
    assert(!gc_participant_publish(&main));
    assert(!gc_participant_leave(&main));
    assert(gc_session_poll(&s) == GC_DEFERRED);
    assert(gc_participant_unblock(&main));
    assert(!gc_participant_unblock(&main));
    assert(gc_participant_publish(&main));
    assert(!gc_participant_block(&main));
    thread_t *worker = create_thread(p, get_root_context());
    assert(worker && gc_session_poll(&s) == GC_DEFERRED);
    assert(gc_participant_enter(&s, worker, &other));
    assert(gc_session_poll(&s) == GC_DEFERRED);
    assert(gc_participant_publish(&other));
    object_t *dead = create_array_object(p, (object_array_t){0});
    operation_result_t result = insert_array_elements(dead, 0, &dead, 1);
    assert(!result.is_exception);
    DECREF(result.value);
    DECREF(dead);
    assert(gc_session_poll(&s) == GC_COLLECTED);
    assert(!s.requested && s.collections == 1 && s.requests == 2 && s.refusals == 4);
    assert(gc_session_poll(&s) == GC_IDLE);
    assert(gc_participant_withdraw(&other));
    assert(gc_session_request(&s));
    assert(gc_session_poll(&s) == GC_DEFERRED);
    destroy_thread(worker);
    assert(!other.session);
    assert(gc_session_poll(&s) == GC_COLLECTED);
    assert(!gc_session_end(&s));
    assert(gc_participant_leave(&main));
    assert(gc_session_request(&s));
    assert(gc_session_end(&s) && !s.requested);
    assert(gc_session_begin(&s, p, true));
    assert(s.requests == 0 && s.collections == 0 && s.refusals == 0);
    assert(gc_session_poll(&s) == GC_IDLE);
    assert(gc_session_end(&s));
    process_execution_leave(p);
    destroy_process(p);
    assert(get_allocated_memory_size() == before);
    return 0;
}
