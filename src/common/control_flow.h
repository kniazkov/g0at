/**
 * @file control_flow.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Shared control-flow mode definitions.
 *
 * This file defines control-flow modes used by both the abstract interpreter
 * and the runtime execution machinery. The same flow state can describe whether
 * statement execution proceeds normally or has been interrupted by a non-local
 * control transfer such as `return`.
 */

#pragma once

/**
 * @enum control_flow_t
 * @brief Control-flow mode shared by interpretation and runtime logic.
 *
 * Describes whether execution should continue normally or whether a statement has requested
 * control transfer out of the current function body.
 */
typedef enum {
    /**
     * @brief Normal execution flow.
     *
     * Execution continues with the next statement or instruction.
     */
    FLOW_NORMAL,

    /**
     * @brief Return flow.
     *
     * A return statement has been executed and execution should unwind the
     * current function body without executing following statements in it.
     */
    FLOW_RETURN
} control_flow_t;
