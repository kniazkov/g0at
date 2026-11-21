/** @file gc_protocol.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Deferred collection without waiting or inferring publication from lock ownership.
 */
#include "gc_protocol.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool locked(const gc_session_t *s) {
    return s && s->process && s->process->gc_session == s
           && os_mutex_depth(s->process->execution_mutex) > 0;
}

static void count(size_t *value) {
    if (*value != SIZE_MAX)
        ++*value;
}

bool gc_session_begin(gc_session_t *s, process_t *p, bool exclusive) {
    if (!s || !p || !exclusive || s->process || p->gc_session || p->execution || p->destroying
        || p->gc_collecting || !os_mutex_depth(p->execution_mutex))
        return false;
    *s = (gc_session_t){.process = p};
    p->gc_session = s;
    return true;
}

bool gc_participant_enter(gc_session_t *s, thread_t *t, gc_participant_t *token) {
    if (!locked(s) || !t || t->process != s->process || !token || token->session)
        return false;
    for (gc_participant_t *p = s->participants; p; p = p->next)
        if (p->thread == t)
            return false;
    *token = (gc_participant_t){.session = s, .thread = t, .next = s->participants};
    s->participants = token;
    return true;
}

static bool member(gc_participant_t *t) {
    if (!t || !locked(t->session))
        return false;
    for (gc_participant_t *p = t->session->participants; p; p = p->next)
        if (p == t)
            return true;
    return false;
}

bool gc_participant_publish(gc_participant_t *t) {
    if (!member(t) || t->blocked || t->published)
        return false;
    t->published = true;
    return true;
}

bool gc_participant_withdraw(gc_participant_t *t) {
    if (!member(t) || !t->published)
        return false;
    t->published = false;
    return true;
}

bool gc_participant_block(gc_participant_t *t) {
    if (!member(t) || t->published || t->blocked == SIZE_MAX)
        return false;
    ++t->blocked;
    return true;
}

bool gc_participant_unblock(gc_participant_t *t) {
    if (!member(t) || !t->blocked)
        return false;
    --t->blocked;
    return true;
}

bool gc_participant_leave(gc_participant_t *t) {
    if (!member(t) || t->blocked)
        return false;
    gc_participant_t **link = &t->session->participants;
    while (*link != t)
        link = &(*link)->next;
    *link = t->next;
    memset(t, 0, sizeof(*t));
    return true;
}

bool gc_session_request(gc_session_t *s) {
    if (!locked(s))
        return false;
    count(&s->requests);
    s->requested = true;
    return true;
}

gc_poll_result_t gc_session_poll(gc_session_t *s) {
    if (!locked(s))
        return GC_DEFERRED;
    if (!s->requested)
        return GC_IDLE;
    process_t *p = s->process;
    if (p->gc_collecting || p->draining_releases || p->destroying || p->exit_requested
        || p->native_failed || (p->initializing_file && !s->initializers)
        || p->initialization_count != s->initializers
        || p->initialization_waiters != s->initialization_waiters || p->finished_workers)
        goto refused;
    /* Startup records must publish. A completed program worker is terminal only after
       schedule_and_reap has joined its OS thread and cleared the worker handle.
       Its completion value/exception remain ordinary model roots. */
    thread_t *thread = p->main_thread;
    if (!thread)
        goto refused;
    do {
        gc_participant_t *t = s->participants;
        while (t && t->thread != thread)
            t = t->next;
        bool terminal = thread != p->main_thread && thread->program_worker && thread->finished
                        && !thread->worker;
        if (!terminal && (!t || !t->published || t->blocked))
            goto refused;
        thread = thread->next;
    } while (thread != p->main_thread);
    for (gc_participant_t *t = s->participants; t; t = t->next)
        if (!t->published || t->blocked)
            goto refused;
    s->requested = false;
    collect_garbage(p);
    count(&s->collections);
    return GC_COLLECTED;
refused:
    count(&s->refusals);
    return GC_DEFERRED;
}

bool gc_session_end(gc_session_t *s) {
    if (!locked(s) || s->participants || s->process->execution || s->initializers
        || s->initialization_waiters)
        return false;
    s->process->gc_session = NULL;
    s->process = NULL;
    s->requested = false;
    return true;
}

void gc_session_retire_thread(thread_t *thread) {
    gc_session_t *s = thread->process->gc_session;
    if (!s)
        return;
    if (!locked(s))
        abort();
    for (gc_participant_t *t = s->participants; t; t = t->next) {
        if (t->thread == thread) {
            if (!gc_participant_leave(t))
                abort();
            return;
        }
    }
}

bool configure_gc_boundaries(process_t *p, bool enabled, bool exclusive) {
    if (!p || !os_mutex_depth(p->execution_mutex) || p->execution || p->gc_session || p->destroying
        || (enabled && !exclusive))
        return false;
    p->gc_force_boundaries = enabled;
    return true;
}

gc_participant_t *gc_call_wait_begin(thread_t *thread) {
    gc_participant_t *token = thread->gc_call_participant;
    if (!token || !member(token) || token->thread != thread || token->blocked != 1
        || token->published || thread->initialization_depth != thread->gc_initialization_depth
        || os_mutex_depth(thread->process->execution_mutex) != 1 + thread->gc_call_extra_lock_depth)
        return NULL;
    if (!gc_participant_unblock(token) || !gc_participant_publish(token))
        abort();
    return token;
}

void gc_call_wait_end(gc_participant_t *token) {
    if (token && (!gc_participant_withdraw(token) || !gc_participant_block(token)))
        abort();
}

gc_participant_t *gc_call_wait_pause(thread_t *thread) {
    gc_participant_t *token = thread->gc_call_participant;
    if (!token || !member(token) || token->thread != thread || !token->published)
        return NULL;
    gc_call_wait_end(token);
    return token;
}

void gc_call_wait_resume(gc_participant_t *token) {
    if (token
        && (!member(token) || token->blocked != 1 || !gc_participant_unblock(token)
            || !gc_participant_publish(token)))
        abort();
}
