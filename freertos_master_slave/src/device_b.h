/* SPDX-License-Identifier: MIT */
/**
 * @file device_b.h
 * @brief Device B (slave) public API.
 *
 * device_b_next_state() and device_b_seed_rand() are FreeRTOS-free and safe
 * to call from unit tests compiled with BUILD_TESTS.
 * device_b_task() is excluded from test builds.
 */

#ifndef DEVICE_B_H
#define DEVICE_B_H

#include "ipc.h"  /* device_b_state_t */

/* ── Pure state-machine functions (no FreeRTOS dependency) ──────────────── */

/**
 * @brief Return a human-readable name for a Device B state.
 * @param state  The state to name.
 * @return Pointer to a static string literal.
 */
const char *device_b_state_name(device_b_state_t state);

/**
 * @brief Seed the internal pseudo-random number generator.
 *
 * Must be called before the first call to device_b_next_state().
 * In the production task this is called once with RAND_SEED.  Tests call
 * it with a known value to produce a deterministic transition sequence.
 *
 * @param seed  Seed value for srand().
 */
void device_b_seed_rand(unsigned int seed);

/**
 * @brief Compute the next autonomous Device B state.
 *
 * Uses rand() internally; call device_b_seed_rand() first for
 * reproducible results.  No FreeRTOS calls; no side effects beyond
 * advancing the rand() sequence.
 *
 * Transition probabilities (per call):
 *   SLEEP  → ACTIVE :  25 %   (rand() % 4 == 0)
 *   ACTIVE → FAULT  :  12.5 % (rand() % 8 == 0, checked first)
 *   ACTIVE → SLEEP  :  ~17.5% (rand() % 5 == 0, after FAULT check)
 *   FAULT  → ACTIVE :  ~16.7% (rand() % 6 == 0)
 *
 * @param current  Current Device B state.
 * @return Next Device B state (may equal current if no transition occurs).
 */
device_b_state_t device_b_next_state(device_b_state_t current);

/* ── FreeRTOS task (excluded from unit-test builds) ─────────────────────── */

#ifndef BUILD_TESTS
/**
 * @brief Device B FreeRTOS task entry point.
 *
 * Seeds rand(), publishes the initial SLEEP state, then loops:
 * checks g_reset_queue (non-blocking), computes the next state via
 * device_b_next_state(), posts state changes to g_state_queue, and
 * delays DEVICE_B_TICK_MS.  Deletes itself when g_sim_running becomes pdFALSE.
 */
void device_b_task(void *params);
#endif

#endif /* DEVICE_B_H */
