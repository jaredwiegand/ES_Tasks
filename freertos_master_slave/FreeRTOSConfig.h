/* SPDX-License-Identifier: MIT */
/**
 * @file FreeRTOSConfig.h
 * @brief FreeRTOS kernel configuration for the freertos_master_slave simulation.
 *
 * Targets the POSIX simulator port (Linux) and the MSVC-MingW port (Windows).
 * Placed in the project root so it is found by the kernel's portable layer.
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* ── Scheduler ───────────────────────────────────────────────────────────── */

#define configUSE_PREEMPTION                        1
#define configUSE_TIME_SLICING                      1
#define configTICK_RATE_HZ                          ( ( TickType_t ) 1000 )
#define configMAX_PRIORITIES                        5
#define configIDLE_SHOULD_YIELD                     1

/* ── Memory ──────────────────────────────────────────────────────────────── */

#define configMINIMAL_STACK_SIZE                    ( ( unsigned short ) 512 )
#define configTOTAL_HEAP_SIZE                       ( ( size_t ) ( 64U * 1024U ) )
#define configSUPPORT_DYNAMIC_ALLOCATION            1
#define configSUPPORT_STATIC_ALLOCATION             0

/* ── Task names ──────────────────────────────────────────────────────────── */

#define configMAX_TASK_NAME_LEN                     16
#define configUSE_16_BIT_TICKS                      0

/* ── Hooks ───────────────────────────────────────────────────────────────── */

#define configUSE_IDLE_HOOK                         1  /* vApplicationIdleHook in main.c */
#define configUSE_TICK_HOOK                         0
#define configUSE_MALLOC_FAILED_HOOK                1  /* vApplicationMallocFailedHook   */
#define configCHECK_FOR_STACK_OVERFLOW              2  /* vApplicationStackOverflowHook  */

/* ── Software timers (not used; keep disabled to reduce overhead) ─────────── */

#define configUSE_TIMERS                            0

/* ── Tracing ─────────────────────────────────────────────────────────────── */

#define configUSE_TRACE_FACILITY                    0
#define configUSE_STATS_FORMATTING_FUNCTIONS        0
#define configGENERATE_RUN_TIME_STATS               0

/* ── Task notifications ──────────────────────────────────────────────────── */

#define configUSE_TASK_NOTIFICATIONS                1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES       1

/* ── Mutexes / semaphores ────────────────────────────────────────────────── */

#define configUSE_MUTEXES                           1
#define configUSE_RECURSIVE_MUTEXES                 0
#define configUSE_COUNTING_SEMAPHORES               0
#define configQUEUE_REGISTRY_SIZE                   0

/* ── Port-optimised selection (not available on POSIX / Win32 ports) ─────── */

#define configUSE_PORT_OPTIMISED_TASK_SELECTION     0

/* ── Assert ──────────────────────────────────────────────────────────────── */

#define configASSERT( x )                                       \
    do { if ( ( x ) == 0 ) { taskDISABLE_INTERRUPTS(); for (;;); } } while (0)

/* ── INCLUDE guards (select which API functions are compiled in) ──────────── */

#define INCLUDE_vTaskDelay                          1
#define INCLUDE_vTaskDelete                         1
#define INCLUDE_vTaskSuspend                        1
#define INCLUDE_xTaskGetTickCount                   1
#define INCLUDE_vTaskEndScheduler                   1
#define INCLUDE_uxTaskGetStackHighWaterMark         1
#define INCLUDE_xTaskGetCurrentTaskHandle           1

/* ── POSIX port compatibility ────────────────────────────────────────────── */

#define configUSE_POSIX_ERRNO                       0

#endif /* FREERTOS_CONFIG_H */
