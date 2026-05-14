/* SPDX-License-Identifier: MIT */
/**
 * @file device_a.c
 * @brief Device A (master) state-machine and FreeRTOS task implementation.
 *
 * The file is split into two sections:
 *   1. Pure state-machine functions — always compiled, no FreeRTOS dependency.
 *   2. FreeRTOS task — compiled only when BUILD_TESTS is not defined.
 */

#include "device_a.h"

#ifndef BUILD_TESTS
#  include "FreeRTOS.h"
#  include "task.h"
#  include "logger.h"

extern volatile BaseType_t g_sim_running;
#endif /* BUILD_TESTS */

/* ── Pure state-machine functions ────────────────────────────────────────── */

const char *device_a_state_name(device_a_state_t state)
{
    switch (state)
    {
        case DEVICE_A_IDLE:       return "IDLE";
        case DEVICE_A_PROCESSING: return "PROCESSING";
        case DEVICE_A_ERROR:      return "ERROR";
        default:                  return "UNKNOWN";
    }
}

device_a_state_t device_a_process_event(device_a_state_t current,
                                         device_b_state_t b_state,
                                         uint32_t         fault_count)
{
    switch (current)
    {
        case DEVICE_A_IDLE:
            if (fault_count >= FAULT_THRESHOLD)  { return DEVICE_A_ERROR; }
            if (b_state == DEVICE_B_ACTIVE)      { return DEVICE_A_PROCESSING; }
            return DEVICE_A_IDLE;

        case DEVICE_A_PROCESSING:
            if (fault_count >= FAULT_THRESHOLD)  { return DEVICE_A_ERROR; }
            if (b_state == DEVICE_B_SLEEP)       { return DEVICE_A_IDLE; }
            return DEVICE_A_PROCESSING;

        case DEVICE_A_ERROR:
            if (b_state == DEVICE_B_SLEEP)       { return DEVICE_A_IDLE; }
            return DEVICE_A_ERROR;

        default:
            return current;
    }
}

/* ── FreeRTOS task ───────────────────────────────────────────────────────── */

#ifndef BUILD_TESTS

static void issue_reset(void)
{
    reset_cmd_t cmd = RESET_CMD_REBOOT;

    if (xQueueSend(g_reset_queue, &cmd, 0) == pdTRUE)
    {
        logger_log(LOG_SRC_DEVICE_A, "reset command issued");
    }
    else
    {
        logger_log(LOG_SRC_DEVICE_A, "reset_queue full — will retry next cycle");
    }
}

void device_a_task(void *params)
{
    device_a_state_t a_state          = DEVICE_A_IDLE;
    device_b_state_t b_state          = DEVICE_B_SLEEP;
    device_a_state_t new_state;
    uint32_t         fault_count      = 0U;
    uint32_t         error_poll_count = 0U;

    (void)params;

    logger_log(LOG_SRC_DEVICE_A, "task started  state=IDLE");

    for (;;)
    {
        /*
         * Block up to DEVICE_A_POLL_MS waiting for a new B state.
         * On timeout b_state retains its last value, so the fault counter
         * keeps incrementing if B remains in FAULT while quiescent.
         */
        xQueueReceive(g_state_queue, &b_state, pdMS_TO_TICKS(DEVICE_A_POLL_MS));

        /* Update the persistent fault counter. */
        if (b_state == DEVICE_B_FAULT)
        {
            fault_count++;
            logger_log(LOG_SRC_DEVICE_A, "fault_count=%u", (unsigned)fault_count);
        }
        else
        {
            fault_count = 0U;
        }

        new_state = device_a_process_event(a_state, b_state, fault_count);

        /* Execute actions on state transition. */
        if (new_state != a_state)
        {
            if (new_state == DEVICE_A_ERROR)
            {
                issue_reset();
                fault_count      = 0U;
                error_poll_count = 0U;
            }

            if (a_state == DEVICE_A_ERROR)
            {
                /* Recovery: B returned to SLEEP. */
                fault_count      = 0U;
                error_poll_count = 0U;
                logger_log(LOG_SRC_DEVICE_A, "ERROR -> IDLE  (recovery complete)");
            }
            else
            {
                logger_log(LOG_SRC_DEVICE_A, "%s -> %s",
                           device_a_state_name(a_state),
                           device_a_state_name(new_state));
            }
        }

        a_state = new_state;

        /* Re-issue reset if Device B has not recovered within RESET_TIMEOUT_MS. */
        if (a_state == DEVICE_A_ERROR)
        {
            error_poll_count++;
            if ((error_poll_count * DEVICE_A_POLL_MS) >= RESET_TIMEOUT_MS)
            {
                logger_log(LOG_SRC_DEVICE_A,
                           "recovery timeout (%u ms) — re-issuing reset",
                           (unsigned)(error_poll_count * DEVICE_A_POLL_MS));
                issue_reset();
                error_poll_count = 0U;
            }
        }
        else
        {
            error_poll_count = 0U;
        }

        if (g_sim_running == pdFALSE)
        {
            logger_log(LOG_SRC_DEVICE_A, "task exiting");
            vTaskDelete(NULL);
        }
    }
}

#endif /* BUILD_TESTS */
