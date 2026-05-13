/* SPDX-License-Identifier: MIT */
/**
 * @file test_ipc_types.c
 * @brief Unity tests for IPC message types, enum values, and sim_config constants.
 *
 * These tests guard against accidental renumbering of enum values (which would
 * silently break queue consumers), unexpected struct-layout changes that could
 * corrupt queue messages, and invalid tuning-constant values.
 */

#include "unity.h"
#include "ipc.h"
#include "device_a.h"
#include "device_b.h"

void setUp(void)    {}
void tearDown(void) {}

/* ── Device B state enum values ──────────────────────────────────────────── */

void test_device_b_sleep_equals_zero(void)
{
    TEST_ASSERT_EQUAL_INT(0, (int)DEVICE_B_SLEEP);
}

void test_device_b_active_equals_one(void)
{
    TEST_ASSERT_EQUAL_INT(1, (int)DEVICE_B_ACTIVE);
}

void test_device_b_fault_equals_two(void)
{
    TEST_ASSERT_EQUAL_INT(2, (int)DEVICE_B_FAULT);
}

/* ── Device A state enum values ──────────────────────────────────────────── */

void test_device_a_idle_equals_zero(void)
{
    TEST_ASSERT_EQUAL_INT(0, (int)DEVICE_A_IDLE);
}

void test_device_a_processing_equals_one(void)
{
    TEST_ASSERT_EQUAL_INT(1, (int)DEVICE_A_PROCESSING);
}

void test_device_a_error_equals_two(void)
{
    TEST_ASSERT_EQUAL_INT(2, (int)DEVICE_A_ERROR);
}

/* ── Reset command enum ──────────────────────────────────────────────────── */

void test_reset_cmd_reboot_equals_0x01(void)
{
    TEST_ASSERT_EQUAL_INT(0x01, (int)RESET_CMD_REBOOT);
}

/* ── Log source enum ─────────────────────────────────────────────────────── */

void test_log_src_device_a_equals_zero(void)
{
    TEST_ASSERT_EQUAL_INT(0, (int)LOG_SRC_DEVICE_A);
}

void test_log_src_device_b_equals_one(void)
{
    TEST_ASSERT_EQUAL_INT(1, (int)LOG_SRC_DEVICE_B);
}

/* ── log_msg_t struct layout ─────────────────────────────────────────────── */

void test_log_msg_text_length_matches_log_msg_max_len(void)
{
    /* Changing LOG_MSG_MAX_LEN without rebuilding all consumers is unsafe. */
    log_msg_t msg;
    TEST_ASSERT_EQUAL_UINT(LOG_MSG_MAX_LEN, sizeof(msg.text));
}

void test_log_msg_tick_field_is_four_bytes(void)
{
    /* tick is declared uint32_t — verify no accidental type substitution. */
    log_msg_t msg;
    TEST_ASSERT_EQUAL_UINT(4U, sizeof(msg.tick));
}

/* ── sim_config.h constant sanity ────────────────────────────────────────── */

void test_log_msg_max_len_is_at_least_32(void)
{
    TEST_ASSERT_GREATER_OR_EQUAL_UINT(32U, (unsigned)LOG_MSG_MAX_LEN);
}

void test_fault_threshold_is_at_least_one(void)
{
    TEST_ASSERT_GREATER_THAN_UINT(0U, (unsigned)FAULT_THRESHOLD);
}

void test_device_a_poll_ms_is_positive(void)
{
    TEST_ASSERT_GREATER_THAN_UINT(0U, (unsigned)DEVICE_A_POLL_MS);
}

void test_device_b_tick_ms_is_positive(void)
{
    TEST_ASSERT_GREATER_THAN_UINT(0U, (unsigned)DEVICE_B_TICK_MS);
}

void test_reset_timeout_exceeds_one_poll_cycle(void)
{
    /* Reset timeout must be long enough for at least one Device A poll. */
    TEST_ASSERT_GREATER_THAN_UINT((unsigned)DEVICE_A_POLL_MS,
                                  (unsigned)RESET_TIMEOUT_MS);
}

/* ── Entry point ─────────────────────────────────────────────────────────── */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_device_b_sleep_equals_zero);
    RUN_TEST(test_device_b_active_equals_one);
    RUN_TEST(test_device_b_fault_equals_two);

    RUN_TEST(test_device_a_idle_equals_zero);
    RUN_TEST(test_device_a_processing_equals_one);
    RUN_TEST(test_device_a_error_equals_two);

    RUN_TEST(test_reset_cmd_reboot_equals_0x01);

    RUN_TEST(test_log_src_device_a_equals_zero);
    RUN_TEST(test_log_src_device_b_equals_one);

    RUN_TEST(test_log_msg_text_length_matches_log_msg_max_len);
    RUN_TEST(test_log_msg_tick_field_is_four_bytes);

    RUN_TEST(test_log_msg_max_len_is_at_least_32);
    RUN_TEST(test_fault_threshold_is_at_least_one);
    RUN_TEST(test_device_a_poll_ms_is_positive);
    RUN_TEST(test_device_b_tick_ms_is_positive);
    RUN_TEST(test_reset_timeout_exceeds_one_poll_cycle);

    return UNITY_END();
}
