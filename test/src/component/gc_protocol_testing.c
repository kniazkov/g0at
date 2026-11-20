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

int main(void) {
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
