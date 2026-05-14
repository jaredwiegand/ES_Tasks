/* SPDX-License-Identifier: MIT */
/**
 * @file logger.h
 * @brief Thread-safe logger: enqueues formatted messages from device tasks;
 *        a dedicated low-priority task serialises them to stdout.
 *
 * In BUILD_TESTS builds both logger_log() and logger_task() are omitted
 * because the pure state-machine functions under test do not call them.
 */

#ifndef LOGGER_H
#define LOGGER_H

#include "ipc.h"  /* log_source_t, log_msg_t */

#ifndef BUILD_TESTS

/**
 * @brief Enqueue a formatted log message (non-blocking).
 *
 * Captures xTaskGetTickCount() at call time.  The message is silently
 * dropped if g_log_queue is full.  Safe to call from any task context.
 *
 * @param source  Originating device (LOG_SRC_DEVICE_A or LOG_SRC_DEVICE_B).
 * @param fmt     printf-style format string.
 */
void logger_log(log_source_t source, const char *fmt, ...);

/**
 * @brief Logger FreeRTOS task function.
 *
 * Blocks on g_log_queue.  When g_sim_running becomes pdFALSE and the queue
 * drains, calls vTaskEndScheduler() to return control to main().
 */
void logger_task(void *params);

#endif /* BUILD_TESTS */

#endif /* LOGGER_H */
