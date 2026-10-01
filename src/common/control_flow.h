/**
 * @file control_flow.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Shared control-flow mode definitions.
 */

#pragma once

/**
 * @brief Control-flow mode shared by interpretation and runtime logic.
 *
 * Describes whether execution should continue normally or whether a statement has requested control
 * transfer out of the current function body.
 */
typedef enum {
    /** @brief Normal execution flow. */
    FLOW_NORMAL,

    /** @brief Return flow. */
    FLOW_RETURN,

    /** @brief Abstract path has no normal execution (not used by the VM). */
    FLOW_UNREACHABLE,

    /** @brief Exception flow handled by a runtime TRY context. */
    FLOW_THROW
} control_flow_t;
