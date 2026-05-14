/* SPDX-License-Identifier: MIT */
/**
 * @file ipc.c
 * @brief FreeRTOS queue handle definitions and initialisation.
 */

#include "ipc.h"

QueueHandle_t g_state_queue;
QueueHandle_t g_reset_queue;
QueueHandle_t g_log_queue;

BaseType_t ipc_init(void)
{
    g_state_queue = xQueueCreate(STATE_QUEUE_DEPTH, sizeof(device_b_state_t));
    g_reset_queue = xQueueCreate(RESET_QUEUE_DEPTH, sizeof(reset_cmd_t));
    g_log_queue   = xQueueCreate(LOG_QUEUE_DEPTH,   sizeof(log_msg_t));

    if ((g_state_queue == NULL) || (g_reset_queue == NULL) || (g_log_queue == NULL))
    {
        return pdFAIL;
    }
    return pdPASS;
}
