/* SPDX-License-Identifier: MIT */
/**
 * @file main.c
 * @brief Simulation entry point.
 *
 * Creates IPC queues and FreeRTOS tasks, starts the scheduler, and prints
 * a completion message after the scheduler stops.  On POSIX and Win32
 * simulator builds the scheduler returns from vTaskStartScheduler() after
 * vTaskEndScheduler() is called by the logger task once the simulation
 * duration elapses and the log queue drains.
 */

#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "ipc.h"
#include "device_a.h"
#include "device_b.h"
#include "logger.h"
#include "sim_config.h"

/* Observed by device tasks and the logger to detect simulation end. */
volatile BaseType_t g_sim_running = pdTRUE;

/* ── Sim-timer task ──────────────────────────────────────────────────────── */

static void sim_timer_task(void *params)
{
    (void)params;
    vTaskDelay(pdMS_TO_TICKS(SIM_DURATION_MS));
    g_sim_running = pdFALSE;
    vTaskDelete(NULL);
}

/* ── FreeRTOS application hooks ─────────────────────────────────────────── */

void vApplicationMallocFailedHook(void)
{
    /* Heap exhaustion is fatal for this simulation. */
    configASSERT(0);
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    configASSERT(0);
}

void vApplicationIdleHook(void)
{
    /* Nothing to do in idle for this simulation. */
}

/* ── Entry point ─────────────────────────────────────────────────────────── */

int main(void)
{
    printf("freertos_master_slave simulation\n");
    printf("=================================\n");
    printf("Duration : %u ms\n", SIM_DURATION_MS);
    printf("Threshold: %u consecutive FAULT observations\n\n", FAULT_THRESHOLD);

    if (ipc_init() != pdPASS)
    {
        fprintf(stderr, "ipc_init: queue creation failed\n");
        return 1;
    }

    /* Logger is created first so it is ready before device tasks log. */
    configASSERT(xTaskCreate(logger_task,    "Logger",   STACK_LOGGER,
                             NULL, PRIORITY_LOGGER,    NULL) == pdPASS);

    configASSERT(xTaskCreate(device_b_task,  "DeviceB",  STACK_DEVICE,
                             NULL, PRIORITY_DEVICE,    NULL) == pdPASS);

    configASSERT(xTaskCreate(device_a_task,  "DeviceA",  STACK_DEVICE,
                             NULL, PRIORITY_DEVICE,    NULL) == pdPASS);

    configASSERT(xTaskCreate(sim_timer_task, "SimTimer", STACK_SIM_TIMER,
                             NULL, PRIORITY_SIM_TIMER, NULL) == pdPASS);

    /* Starts the scheduler.  Returns only on simulator builds (POSIX / Win32)
       after vTaskEndScheduler() is called by the logger task. */
    vTaskStartScheduler();

    printf("\nSimulation complete.\n");
    return 0;
}
