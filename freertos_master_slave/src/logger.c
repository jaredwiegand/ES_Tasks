/* SPDX-License-Identifier: MIT */
/**
 * @file logger.c
 * @brief Logger task and logger_log() helper implementation.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "logger.h"

extern volatile BaseType_t g_sim_running;

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static const char *source_name(log_source_t src)
{
    switch (src)
    {
        case LOG_SRC_DEVICE_A: return "DEVICE_A";
        case LOG_SRC_DEVICE_B: return "DEVICE_B";
        default:               return "UNKNOWN";
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void logger_log(log_source_t source, const char *fmt, ...)
{
    log_msg_t msg;
    va_list   args;

    msg.tick   = (uint32_t)xTaskGetTickCount();
    msg.source = source;

    va_start(args, fmt);
    vsnprintf(msg.text, LOG_MSG_MAX_LEN, fmt, args);
    va_end(args);

    /* Non-blocking: drop the message rather than stall the calling task. */
    xQueueSend(g_log_queue, &msg, 0);
}

void logger_task(void *params)
{
    log_msg_t msg;

    (void)params;

    for (;;)
    {
        if (xQueueReceive(g_log_queue, &msg, pdMS_TO_TICKS(200U)) == pdTRUE)
        {
            printf("[TICK:%010lu] [%s] %s\n",
                   (unsigned long)msg.tick,
                   source_name(msg.source),
                   msg.text);
            fflush(stdout);
        }
        else if (g_sim_running == pdFALSE)
        {
            /* Queue drained and simulation complete: stop the scheduler. */
            vTaskEndScheduler();
            vTaskDelete(NULL); /* safety net — not normally reached */
        }
    }
}
