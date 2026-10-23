/** @file process_channel.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Private duplex transport for independently supervised services.
 */
#pragma once
#include "child_process.h"

typedef enum { CHANNEL_OK, CHANNEL_TIMEOUT, CHANNEL_CLOSED, CHANNEL_ERROR } channel_status_t;

/* Zero is invalid. The second endpoint is explicitly inherited by start_child_process. */
bool create_process_channel(uintptr_t pair[2]);
channel_status_t wait_process_channel(uintptr_t channel, child_deadline_t deadline);
channel_status_t
read_process_channel(uintptr_t channel, void *data, size_t size, child_deadline_t deadline);
channel_status_t
write_process_channel(uintptr_t channel, const void *data, size_t size, child_deadline_t deadline);
void close_process_channel(uintptr_t channel);
void process_pause(unsigned milliseconds);
/* 1 = original owner ended, 0 = alive, -1 = invalid/unusable watch. */
int child_parent_state(uintptr_t watch);
