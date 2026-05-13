/* SPDX-License-Identifier: MIT */
/**
 * @file sim_config.h
 * @brief Compile-time tuning constants for the freertos_master_slave simulation.
 *
 * All timing values are in milliseconds.  Override any constant by passing
 * -D<NAME>=<value> on the compiler command line or via CMake.
 */

#ifndef SIM_CONFIG_H
#define SIM_CONFIG_H

/* ── Simulation duration ─────────────────────────────────────────────────── */

/** Total simulation run time in milliseconds. */
#ifndef SIM_DURATION_MS
#  define SIM_DURATION_MS     30000U
#endif

/* ── Task periods ────────────────────────────────────────────────────────── */

/** Device A evaluation-cycle period in milliseconds. */
#ifndef DEVICE_A_POLL_MS
#  define DEVICE_A_POLL_MS      250U
#endif

/** Device B autonomous-transition check interval in milliseconds. */
#ifndef DEVICE_B_TICK_MS
#  define DEVICE_B_TICK_MS      500U
#endif

/* ── Fault handling ──────────────────────────────────────────────────────── */

/** Consecutive FAULT observations before Device A issues a reset command. */
#ifndef FAULT_THRESHOLD
#  define FAULT_THRESHOLD         3U
#endif

/** Time (ms) Device A waits for Device B recovery before re-issuing reset. */
#ifndef RESET_TIMEOUT_MS
#  define RESET_TIMEOUT_MS     2000U
#endif

/* ── Reproducibility ─────────────────────────────────────────────────────── */

/** Fixed seed for rand() — change for a different transition sequence. */
#ifndef RAND_SEED
#  define RAND_SEED              42U
#endif

/* ── Logging ─────────────────────────────────────────────────────────────── */

/** Maximum log message text length in bytes, including the NUL terminator. */
#ifndef LOG_MSG_MAX_LEN
#  define LOG_MSG_MAX_LEN        64U
#endif

/* ── Task priorities (0 = idle, must be < configMAX_PRIORITIES = 5) ──────── */

#define PRIORITY_LOGGER      1U
#define PRIORITY_SIM_TIMER   2U
#define PRIORITY_DEVICE      3U

/* ── Task stack sizes (words; POSIX port maps to bytes × sizeof(size_t)) ─── */

#define STACK_DEVICE      1024U
#define STACK_LOGGER       512U
#define STACK_SIM_TIMER    256U

/* ── Queue depths ────────────────────────────────────────────────────────── */

#define STATE_QUEUE_DEPTH    8U
#define RESET_QUEUE_DEPTH    1U
#define LOG_QUEUE_DEPTH     32U

#endif /* SIM_CONFIG_H */
