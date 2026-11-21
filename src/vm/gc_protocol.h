/** @file gc_protocol.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Lock-serialized, fail-closed collection sessions for reviewed runtime bridges.
 */
#pragma once
#include "gc.h"
#include "model/thread.h"

typedef struct gc_participant_t gc_participant_t;

typedef struct {
    process_t *process;
    gc_participant_t *participants;
    bool requested;
    size_t requests, refusals, collections;
} gc_session_t;

struct gc_participant_t {
    gc_session_t *session;
    thread_t *thread;
    gc_participant_t *next;
    size_t blocked;
    bool published;
};

typedef enum { GC_IDLE, GC_DEFERRED, GC_COLLECTED } gc_poll_result_t;

/** All calls require the process model lock. Tokens are zero-initialized, noncopyable.
 * Exclusive host ownership is a promise; this API cannot detect unregistered external clients.
 * Publication promises all managed C references have traced owners and remain stable until
 * withdrawal. Suspending the model lock alone never publishes a participant.
 */
bool gc_session_begin(gc_session_t *session, process_t *process, bool exclusive_host);
bool gc_participant_enter(gc_session_t *session, thread_t *thread, gc_participant_t *token);
bool gc_participant_publish(gc_participant_t *token);
bool gc_participant_withdraw(gc_participant_t *token);
bool gc_participant_block(gc_participant_t *token);
bool gc_participant_unblock(gc_participant_t *token);
bool gc_participant_leave(gc_participant_t *token);
bool gc_session_request(gc_session_t *session);
gc_poll_result_t gc_session_poll(gc_session_t *session);
bool gc_session_end(gc_session_t *session);

/** Internal lifecycle hook: retire a thread token before freeing the model record. */
void gc_session_retire_thread(thread_t *thread);

/** Configure forced runtime collection while idle under the model lock. Enabling
 * requires exclusive host ownership for each run; disabled by default. */
bool configure_gc_boundaries(process_t *process, bool enabled, bool exclusive_host);
