/* SPDX-License-Identifier: MIT */
/**
 * @file ipc.h
 * @brief IPC message types and FreeRTOS queue handles for freertos_master_slave.
 *
 * The message-type definitions (enums and log_msg_t) have no FreeRTOS
 * dependency and are always available, including in unit-test builds
 * (BUILD_TESTS defined).  The queue-handle declarations and ipc_init()
 * require FreeRTOS and are guarded by #ifndef BUILD_TESTS.
 */

#ifndef IPC_H
#define IPC_H

#include <stdint.h>
#include "sim_config.h"

/* ── Message and state types ─────────────────────────────────────────────── */

/** Device B observable states — also the item type of g_state_queue. */
typedef enum
{
    DEVICE_B_SLEEP  = 0,
    DEVICE_B_ACTIVE = 1,
    DEVICE_B_FAULT  = 2,
} device_b_state_t;

/** Command sent by Device A to Device B via g_reset_queue. */
typedef enum
{
    RESET_CMD_REBOOT = 0x01,
} reset_cmd_t;

/** Log-message source identifier. */
typedef enum
{
    LOG_SRC_DEVICE_A = 0,
    LOG_SRC_DEVICE_B = 1,
} log_source_t;

/**
 * @brief Log message enqueued by device tasks and consumed by the logger task.
 *
 * tick uses uint32_t rather than TickType_t so this header remains
 * FreeRTOS-free for unit-test builds.
 */
typedef struct
{
    uint32_t     tick;                   /**< xTaskGetTickCount() at enqueue time. */
    log_source_t source;                 /**< Originating device. */
    char         text[LOG_MSG_MAX_LEN];  /**< NUL-terminated message string. */
} log_msg_t;

/* ── Queue handles (requires FreeRTOS) ───────────────────────────────────── */

#ifndef BUILD_TESTS

#  include "FreeRTOS.h"
#  include "queue.h"

/** Device B → Device A: Device B's current state after each transition. */
extern QueueHandle_t g_state_queue;

/** Device A → Device B: reset command; depth 1 (at most one outstanding reset). */
extern QueueHandle_t g_reset_queue;

/** Device A, B → Logger: log messages for serialised stdout output. */
extern QueueHandle_t g_log_queue;

/**
 * @brief Create all IPC queues.
 * @return pdPASS on success, pdFAIL if any queue creation fails.
 */
BaseType_t ipc_init(void);

#endif /* BUILD_TESTS */

#endif /* IPC_H */
