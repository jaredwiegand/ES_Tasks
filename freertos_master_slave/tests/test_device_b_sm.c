/* SPDX-License-Identifier: MIT */
/**
 * @file test_device_b_sm.c
 * @brief Unity unit tests for the Device B pure state-machine function.
 *
 * device_b_next_state() uses rand() internally.  Tests that require a specific
 * transition use probe_seed() to locate the smallest seed that produces a
 * first rand() value satisfying the required condition, avoiding hardcoded
 * magic numbers that would differ between C standard library implementations.
 *
 * Tests that verify a transition can NEVER occur run 1000 iterations from the
 * same starting state with RAND_SEED, exercising a large portion of the rand()
 * sequence.
 */

#include <stdlib.h>
#include "unity.h"
#include "device_b.h"

void setUp(void)    {}
void tearDown(void) {}

/* ── Seed probe helper ───────────────────────────────────────────────────── */

/*
 * Return the smallest seed s (0 … 99999) such that (rand() % mod) == rem
 * after srand(s).  Fails the test if no such seed is found.
 */
static unsigned int probe_seed(int mod, int rem)
{
    unsigned int s;
    for (s = 0U; s < 100000U; s++)
    {
        srand(s);
        if ((rand() % mod) == rem) { return s; }
    }
    TEST_FAIL_MESSAGE("probe_seed: no seed found in range 0..99999");
    return 0U;
}

/* ── SLEEP transitions ───────────────────────────────────────────────────── */

void test_sleep_to_active_when_rand_divisible_by_4(void)
{
    device_b_seed_rand(probe_seed(4, 0));
    TEST_ASSERT_EQUAL(DEVICE_B_ACTIVE, device_b_next_state(DEVICE_B_SLEEP));
}

void test_sleep_stays_sleep_when_rand_not_divisible_by_4(void)
{
    device_b_seed_rand(probe_seed(4, 1));
    TEST_ASSERT_EQUAL(DEVICE_B_SLEEP, device_b_next_state(DEVICE_B_SLEEP));
}

void test_sleep_never_transitions_to_fault(void)
{
    /* SLEEP → FAULT is not in the transition table; verify over 1000 rand() values. */
    unsigned int i;
    device_b_seed_rand(RAND_SEED);
    for (i = 0U; i < 1000U; i++)
    {
        TEST_ASSERT_NOT_EQUAL(DEVICE_B_FAULT, device_b_next_state(DEVICE_B_SLEEP));
    }
}

/* ── ACTIVE transitions ──────────────────────────────────────────────────── */

void test_active_to_fault_when_rand_divisible_by_8(void)
{
    device_b_seed_rand(probe_seed(8, 0));
    TEST_ASSERT_EQUAL(DEVICE_B_FAULT, device_b_next_state(DEVICE_B_ACTIVE));
}

void test_active_to_sleep_when_rand_divisible_by_5_not_8(void)
{
    /* Find first seed where rand() % 8 != 0 AND rand() % 5 == 0. */
    unsigned int s;
    int          r;
    for (s = 0U; s < 100000U; s++)
    {
        srand(s);
        r = rand();
        if (((r % 8) != 0) && ((r % 5) == 0)) { break; }
    }
    device_b_seed_rand(s);
    TEST_ASSERT_EQUAL(DEVICE_B_SLEEP, device_b_next_state(DEVICE_B_ACTIVE));
}

void test_active_stays_active_when_no_transition_condition_met(void)
{
    /* Find first seed where rand() % 8 != 0 AND rand() % 5 != 0. */
    unsigned int s;
    int          r;
    for (s = 0U; s < 100000U; s++)
    {
        srand(s);
        r = rand();
        if (((r % 8) != 0) && ((r % 5) != 0)) { break; }
    }
    device_b_seed_rand(s);
    TEST_ASSERT_EQUAL(DEVICE_B_ACTIVE, device_b_next_state(DEVICE_B_ACTIVE));
}

/* ── FAULT transitions ───────────────────────────────────────────────────── */

void test_fault_to_active_when_rand_divisible_by_6(void)
{
    device_b_seed_rand(probe_seed(6, 0));
    TEST_ASSERT_EQUAL(DEVICE_B_ACTIVE, device_b_next_state(DEVICE_B_FAULT));
}

void test_fault_stays_fault_when_rand_not_divisible_by_6(void)
{
    device_b_seed_rand(probe_seed(6, 1));
    TEST_ASSERT_EQUAL(DEVICE_B_FAULT, device_b_next_state(DEVICE_B_FAULT));
}

void test_fault_never_transitions_to_sleep_autonomously(void)
{
    /*
     * FAULT → SLEEP requires a reset command handled in device_b_task();
     * device_b_next_state() must never produce SLEEP from FAULT.
     */
    unsigned int i;
    device_b_seed_rand(RAND_SEED);
    for (i = 0U; i < 1000U; i++)
    {
        TEST_ASSERT_NOT_EQUAL(DEVICE_B_SLEEP, device_b_next_state(DEVICE_B_FAULT));
    }
}

/* ── Result-set constraints ──────────────────────────────────────────────── */

void test_sleep_result_is_always_sleep_or_active(void)
{
    unsigned int     i;
    device_b_state_t next;
    device_b_seed_rand(RAND_SEED);
    for (i = 0U; i < 1000U; i++)
    {
        next = device_b_next_state(DEVICE_B_SLEEP);
        TEST_ASSERT_TRUE((next == DEVICE_B_SLEEP) || (next == DEVICE_B_ACTIVE));
    }
}

void test_fault_result_is_always_fault_or_active(void)
{
    unsigned int     i;
    device_b_state_t next;
    device_b_seed_rand(RAND_SEED);
    for (i = 0U; i < 1000U; i++)
    {
        next = device_b_next_state(DEVICE_B_FAULT);
        TEST_ASSERT_TRUE((next == DEVICE_B_FAULT) || (next == DEVICE_B_ACTIVE));
    }
}

/* ── Reproducibility ─────────────────────────────────────────────────────── */

void test_same_seed_produces_identical_sequence(void)
{
    device_b_state_t seq1[10];
    device_b_state_t seq2[10];
    device_b_state_t state;
    unsigned int     i;

    device_b_seed_rand(RAND_SEED);
    state = DEVICE_B_SLEEP;
    for (i = 0U; i < 10U; i++) { seq1[i] = device_b_next_state(state); state = seq1[i]; }

    device_b_seed_rand(RAND_SEED);
    state = DEVICE_B_SLEEP;
    for (i = 0U; i < 10U; i++) { seq2[i] = device_b_next_state(state); state = seq2[i]; }

    TEST_ASSERT_EQUAL_MEMORY(seq1, seq2, sizeof(seq1));
}

/* ── State name helper ───────────────────────────────────────────────────── */

void test_state_name_sleep(void)
{
    TEST_ASSERT_EQUAL_STRING("SLEEP", device_b_state_name(DEVICE_B_SLEEP));
}

void test_state_name_active(void)
{
    TEST_ASSERT_EQUAL_STRING("ACTIVE", device_b_state_name(DEVICE_B_ACTIVE));
}

void test_state_name_fault(void)
{
    TEST_ASSERT_EQUAL_STRING("FAULT", device_b_state_name(DEVICE_B_FAULT));
}

/* ── Entry point ─────────────────────────────────────────────────────────── */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_sleep_to_active_when_rand_divisible_by_4);
    RUN_TEST(test_sleep_stays_sleep_when_rand_not_divisible_by_4);
    RUN_TEST(test_sleep_never_transitions_to_fault);

    RUN_TEST(test_active_to_fault_when_rand_divisible_by_8);
    RUN_TEST(test_active_to_sleep_when_rand_divisible_by_5_not_8);
    RUN_TEST(test_active_stays_active_when_no_transition_condition_met);

    RUN_TEST(test_fault_to_active_when_rand_divisible_by_6);
    RUN_TEST(test_fault_stays_fault_when_rand_not_divisible_by_6);
    RUN_TEST(test_fault_never_transitions_to_sleep_autonomously);

    RUN_TEST(test_sleep_result_is_always_sleep_or_active);
    RUN_TEST(test_fault_result_is_always_fault_or_active);

    RUN_TEST(test_same_seed_produces_identical_sequence);

    RUN_TEST(test_state_name_sleep);
    RUN_TEST(test_state_name_active);
    RUN_TEST(test_state_name_fault);

    return UNITY_END();
}
