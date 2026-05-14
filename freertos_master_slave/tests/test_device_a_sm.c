/* SPDX-License-Identifier: MIT */
/**
 * @file test_device_a_sm.c
 * @brief Unity unit tests for the Device A pure state-machine function.
 *
 * device_a_process_event() has no FreeRTOS dependency, so these tests run
 * without a scheduler.  Every valid transition in the state table is covered,
 * plus boundary conditions on the fault threshold.
 */

#include "unity.h"
#include "device_a.h"

void setUp(void)    {}
void tearDown(void) {}

/* ── IDLE transitions ────────────────────────────────────────────────────── */

void test_idle_b_sleep_stays_idle(void)
{
    TEST_ASSERT_EQUAL(DEVICE_A_IDLE,
        device_a_process_event(DEVICE_A_IDLE, DEVICE_B_SLEEP, 0U));
}

void test_idle_b_active_goes_processing(void)
{
    TEST_ASSERT_EQUAL(DEVICE_A_PROCESSING,
        device_a_process_event(DEVICE_A_IDLE, DEVICE_B_ACTIVE, 0U));
}

void test_idle_b_fault_below_threshold_stays_idle(void)
{
    TEST_ASSERT_EQUAL(DEVICE_A_IDLE,
        device_a_process_event(DEVICE_A_IDLE, DEVICE_B_FAULT, FAULT_THRESHOLD - 1U));
}

void test_idle_b_fault_at_threshold_goes_error(void)
{
    TEST_ASSERT_EQUAL(DEVICE_A_ERROR,
        device_a_process_event(DEVICE_A_IDLE, DEVICE_B_FAULT, FAULT_THRESHOLD));
}

void test_idle_b_fault_above_threshold_goes_error(void)
{
    TEST_ASSERT_EQUAL(DEVICE_A_ERROR,
        device_a_process_event(DEVICE_A_IDLE, DEVICE_B_FAULT, FAULT_THRESHOLD + 1U));
}

/* ── PROCESSING transitions ──────────────────────────────────────────────── */

void test_processing_b_active_stays_processing(void)
{
    TEST_ASSERT_EQUAL(DEVICE_A_PROCESSING,
        device_a_process_event(DEVICE_A_PROCESSING, DEVICE_B_ACTIVE, 0U));
}

void test_processing_b_sleep_goes_idle(void)
{
    TEST_ASSERT_EQUAL(DEVICE_A_IDLE,
        device_a_process_event(DEVICE_A_PROCESSING, DEVICE_B_SLEEP, 0U));
}

void test_processing_b_fault_below_threshold_stays_processing(void)
{
    TEST_ASSERT_EQUAL(DEVICE_A_PROCESSING,
        device_a_process_event(DEVICE_A_PROCESSING, DEVICE_B_FAULT, FAULT_THRESHOLD - 1U));
}

void test_processing_b_fault_at_threshold_goes_error(void)
{
    TEST_ASSERT_EQUAL(DEVICE_A_ERROR,
        device_a_process_event(DEVICE_A_PROCESSING, DEVICE_B_FAULT, FAULT_THRESHOLD));
}

void test_processing_fault_count_at_threshold_overrides_active_state(void)
{
    /*
     * Demonstrates that fault_count reaching FAULT_THRESHOLD takes priority
     * regardless of the current b_state value.  In normal task operation
     * fault_count resets when b_state != FAULT, so this combination does not
     * arise at runtime; the pure function is still required to handle it
     * predictably.
     */
    TEST_ASSERT_EQUAL(DEVICE_A_ERROR,
        device_a_process_event(DEVICE_A_PROCESSING, DEVICE_B_ACTIVE, FAULT_THRESHOLD));
}

/* ── ERROR transitions ───────────────────────────────────────────────────── */

void test_error_b_fault_stays_error(void)
{
    TEST_ASSERT_EQUAL(DEVICE_A_ERROR,
        device_a_process_event(DEVICE_A_ERROR, DEVICE_B_FAULT, 0U));
}

void test_error_b_active_stays_error(void)
{
    TEST_ASSERT_EQUAL(DEVICE_A_ERROR,
        device_a_process_event(DEVICE_A_ERROR, DEVICE_B_ACTIVE, 0U));
}

void test_error_b_sleep_goes_idle(void)
{
    TEST_ASSERT_EQUAL(DEVICE_A_IDLE,
        device_a_process_event(DEVICE_A_ERROR, DEVICE_B_SLEEP, 0U));
}

void test_error_b_sleep_ignores_fault_count(void)
{
    /* fault_count is irrelevant in ERROR — only b_state == SLEEP triggers exit. */
    TEST_ASSERT_EQUAL(DEVICE_A_IDLE,
        device_a_process_event(DEVICE_A_ERROR, DEVICE_B_SLEEP, FAULT_THRESHOLD));
}

/* ── Fault-threshold boundary ────────────────────────────────────────────── */

void test_fault_threshold_minus_one_does_not_trigger_error(void)
{
    TEST_ASSERT_NOT_EQUAL(DEVICE_A_ERROR,
        device_a_process_event(DEVICE_A_IDLE, DEVICE_B_FAULT, FAULT_THRESHOLD - 1U));
}

void test_fault_threshold_exact_triggers_error(void)
{
    TEST_ASSERT_EQUAL(DEVICE_A_ERROR,
        device_a_process_event(DEVICE_A_IDLE, DEVICE_B_FAULT, FAULT_THRESHOLD));
}

/* ── State name helper ───────────────────────────────────────────────────── */

void test_state_name_idle(void)
{
    TEST_ASSERT_EQUAL_STRING("IDLE", device_a_state_name(DEVICE_A_IDLE));
}

void test_state_name_processing(void)
{
    TEST_ASSERT_EQUAL_STRING("PROCESSING", device_a_state_name(DEVICE_A_PROCESSING));
}

void test_state_name_error(void)
{
    TEST_ASSERT_EQUAL_STRING("ERROR", device_a_state_name(DEVICE_A_ERROR));
}

/* ── Entry point ─────────────────────────────────────────────────────────── */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_idle_b_sleep_stays_idle);
    RUN_TEST(test_idle_b_active_goes_processing);
    RUN_TEST(test_idle_b_fault_below_threshold_stays_idle);
    RUN_TEST(test_idle_b_fault_at_threshold_goes_error);
    RUN_TEST(test_idle_b_fault_above_threshold_goes_error);

    RUN_TEST(test_processing_b_active_stays_processing);
    RUN_TEST(test_processing_b_sleep_goes_idle);
    RUN_TEST(test_processing_b_fault_below_threshold_stays_processing);
    RUN_TEST(test_processing_b_fault_at_threshold_goes_error);
    RUN_TEST(test_processing_fault_count_at_threshold_overrides_active_state);

    RUN_TEST(test_error_b_fault_stays_error);
    RUN_TEST(test_error_b_active_stays_error);
    RUN_TEST(test_error_b_sleep_goes_idle);
    RUN_TEST(test_error_b_sleep_ignores_fault_count);

    RUN_TEST(test_fault_threshold_minus_one_does_not_trigger_error);
    RUN_TEST(test_fault_threshold_exact_triggers_error);

    RUN_TEST(test_state_name_idle);
    RUN_TEST(test_state_name_processing);
    RUN_TEST(test_state_name_error);

    return UNITY_END();
}
