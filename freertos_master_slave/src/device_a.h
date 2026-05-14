/* SPDX-License-Identifier: MIT */
/**
 * @file device_a.h
 * @brief Device A (master) public API.
 *
 * device_a_process_event() is a pure function with no FreeRTOS dependency
 * and is safe to call from unit tests compiled with BUILD_TESTS.
 * device_a_task() is the FreeRTOS task wrapper and is excluded in test builds.
 */

#ifndef DEVICE_A_H
#define DEVICE_A_H

#include <stdint.h>
#include "ipc.h"  /* device_b_state_t */

/* ── State type ──────────────────────────────────────────────────────────── */

typedef enum
{
    DEVICE_A_IDLE       = 0,  /**< Waiting for Device B to become ACTIVE.          */
    DEVICE_A_PROCESSING = 1,  /**< Device B is ACTIVE; nominal work in progress.   */
    DEVICE_A_ERROR      = 2,  /**< Persistent FAULT detected; reset issued.         */
} device_a_state_t;

/* ── Pure state-machine functions (no FreeRTOS dependency) ──────────────── */

/**
 * @brief Return a human-readable name for a Device A state.
 * @param state  The state to name.
 * @return Pointer to a static string literal.
 */
const char *device_a_state_name(device_a_state_t state);

/**
 * @brief Compute the next Device A state.
 *
 * Pure function — no FreeRTOS calls, no side effects.  The caller is
 * responsible for tracking fault_count and executing any transition actions
 * (logging, issuing reset commands).
 *
 * @param current     Current Device A state.
 * @param b_state     Most-recently-observed Device B state.
 * @param fault_count Consecutive cycles Device B has been observed in FAULT.
 * @return Next Device A state.
 */
device_a_state_t device_a_process_event(device_a_state_t current,
                                         device_b_state_t b_state,
                                         uint32_t         fault_count);

/* ── FreeRTOS task (excluded from unit-test builds) ─────────────────────── */

#ifndef BUILD_TESTS
/**
 * @brief Device A FreeRTOS task entry point.
 *
 * Polls g_state_queue, maintains fault_count, calls device_a_process_event(),
 * issues reset commands via g_reset_queue, and logs all transitions via
 * logger_log().  Deletes itself when g_sim_running becomes pdFALSE.
 */
void device_a_task(void *params);
#endif

#endif /* DEVICE_A_H */
