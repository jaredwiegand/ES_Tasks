/* SPDX-License-Identifier: MIT */
/**
 * @file device_b.c
 * @brief Device B (slave) state-machine and FreeRTOS task implementation.
 */

#include <stdlib.h>

#include "device_b.h"

#ifndef BUILD_TESTS
#  include "FreeRTOS.h"
#  include "task.h"
#  include "logger.h"

extern volatile BaseType_t g_sim_running;
#endif /* BUILD_TESTS */

/* ── Pure state-machine functions ────────────────────────────────────────── */

const char *device_b_state_name(device_b_state_t state)
{
    switch (state)
    {
        case DEVICE_B_SLEEP:  return "SLEEP";
        case DEVICE_B_ACTIVE: return "ACTIVE";
        case DEVICE_B_FAULT:  return "FAULT";
        default:              return "UNKNOWN";
    }
}

void device_b_seed_rand(unsigned int seed)
{
    srand(seed);
}

device_b_state_t device_b_next_state(device_b_state_t current)
{
    int r = rand();

    switch (current)
    {
        case DEVICE_B_SLEEP:
            if ((r % 4) == 0) { return DEVICE_B_ACTIVE; }
            return DEVICE_B_SLEEP;

        case DEVICE_B_ACTIVE:
            /* Check FAULT first so rand() values divisible by 40 become FAULT,
               not SLEEP (keeps combined probability well-defined). */
            if ((r % 8) == 0) { return DEVICE_B_FAULT; }
            if ((r % 5) == 0) { return DEVICE_B_SLEEP; }
            return DEVICE_B_ACTIVE;

        case DEVICE_B_FAULT:
            if ((r % 6) == 0) { return DEVICE_B_ACTIVE; }
            return DEVICE_B_FAULT;

        default:
            return current;
    }
}

/* ── FreeRTOS task ───────────────────────────────────────────────────────── */

#ifndef BUILD_TESTS

void device_b_task(void *params)
{
    device_b_state_t b_state = DEVICE_B_SLEEP;
    device_b_state_t new_state;
    device_b_state_t prev_state;
    reset_cmd_t      cmd;

    (void)params;

    device_b_seed_rand(RAND_SEED);

    logger_log(LOG_SRC_DEVICE_B, "task started  state=SLEEP");

    /* Publish initial state so Device A has a baseline to poll. */
    xQueueSend(g_state_queue, &b_state, 0);

    for (;;)
    {
        /* Service any pending reset command before autonomous transitions. */
        if (xQueueReceive(g_reset_queue, &cmd, 0) == pdTRUE)
        {
            prev_state = b_state;
            b_state    = DEVICE_B_SLEEP;
            logger_log(LOG_SRC_DEVICE_B, "%s -> SLEEP  (reset received)",
                       device_b_state_name(prev_state));
            xQueueSend(g_state_queue, &b_state, 0);
            vTaskDelay(pdMS_TO_TICKS(DEVICE_B_TICK_MS));

            if (g_sim_running == pdFALSE) { break; }
            continue;
        }

        /* Autonomous state transition. */
        new_state = device_b_next_state(b_state);

        if (new_state != b_state)
        {
            logger_log(LOG_SRC_DEVICE_B, "%s -> %s",
                       device_b_state_name(b_state),
                       device_b_state_name(new_state));
            b_state = new_state;

            if (xQueueSend(g_state_queue, &b_state, 0) != pdTRUE)
            {
                logger_log(LOG_SRC_DEVICE_B, "state_queue full — update dropped");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(DEVICE_B_TICK_MS));

        if (g_sim_running == pdFALSE) { break; }
    }

    logger_log(LOG_SRC_DEVICE_B, "task exiting");
    vTaskDelete(NULL);
}

#endif /* BUILD_TESTS */
