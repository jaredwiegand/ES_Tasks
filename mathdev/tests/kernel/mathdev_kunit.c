// SPDX-License-Identifier: GPL-2.0
/**
 * @file mathdev_kunit.c
 * @brief KUnit tests for the mathdev kernel module.
 *
 * Tests the mathdev_calculate() logic directly inside the kernel using
 * a locally-replicated copy of the function (see note below).
 *
 * @par Build & run
 * @code
 *   make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
 *   sudo insmod mathdev_kunit.ko
 *   sudo dmesg | grep -E "PASSED|FAILED|kunit"
 * @endcode
 *
 * Or with kunit_tool (if available):
 * @code
 *   ./tools/testing/kunit/kunit.py run \
 *       --kunitconfig=tests/kernel/.kunitconfig
 * @endcode
 *
 * @note Requires Linux kernel >= 5.4 with CONFIG_KUNIT=y or =m.
 */

#include <kunit/test.h>
#include <linux/module.h>
#include <linux/kernel.h>

/*
 * We test mathdev_calculate() directly.  To avoid linking the whole
 * mathdev module, we replicate the minimal types and the function here.
 * In a production setup you would export mathdev_calculate() with
 * EXPORT_SYMBOL_GPL and link against the module.
 */

#include "../../kernel/mathdev.h"

/* ── replicated mathdev_calculate() for testing ──────────────────────────── */

/**
 * @brief Test-local replica of the kernel's mathdev_calculate().
 *
 * Intentionally omits the pr_info/pr_warn calls so test output stays clean.
 * The logic must be kept in sync with the production implementation in
 * mathdev.c.
 *
 * @param req  Pointer to the math request; result is written in place.
 *
 * @return 0 on success, -EDOM on division by zero, -EINVAL on unknown op.
 */
static int mathdev_calculate_test(struct math_request *req)
{
    switch (req->op) {
    case MATH_OP_ADD:
        req->result = req->a + req->b;
        break;
    case MATH_OP_SUB:
        req->result = req->a - req->b;
        break;
    case MATH_OP_MUL:
        req->result = req->a * req->b;
        break;
    case MATH_OP_DIV:
        if (req->b == 0)
            return -EDOM;
        req->result = req->a / req->b;
        break;
    default:
        return -EINVAL;
    }
    return 0;
}

/* ── ADD tests ────────────────────────────────────────────────────────────── */

/**
 * @brief ADD: two positive operands produce the correct sum.
 */
static void test_add_positive(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_ADD, .a = 42, .b = 37 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), 0);
    KUNIT_EXPECT_EQ(test, req.result, (s64)79);
}

/**
 * @brief ADD: two negative operands produce a negative sum.
 */
static void test_add_negative(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_ADD, .a = -10, .b = -5 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), 0);
    KUNIT_EXPECT_EQ(test, req.result, (s64)-15);
}

/**
 * @brief ADD: adding zero to zero yields zero.
 */
static void test_add_zero(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_ADD, .a = 0, .b = 0 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), 0);
    KUNIT_EXPECT_EQ(test, req.result, (s64)0);
}

/**
 * @brief ADD: mixed-sign operands where result is positive.
 */
static void test_add_mixed_sign(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_ADD, .a = -100, .b = 200 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), 0);
    KUNIT_EXPECT_EQ(test, req.result, (s64)100);
}

/**
 * @brief ADD: large values that exceed 32-bit range to exercise 64-bit arithmetic.
 */
static void test_add_large_values(struct kunit *test)
{
    struct math_request req = {
        .op = MATH_OP_ADD,
        .a  = (s64)2000000000LL,
        .b  = (s64)2000000000LL,
    };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), 0);
    KUNIT_EXPECT_EQ(test, req.result, (s64)4000000000LL);
}

/* ── SUB tests ────────────────────────────────────────────────────────────── */

/**
 * @brief SUB: larger minus smaller gives a positive result.
 */
static void test_sub_positive_result(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_SUB, .a = 10, .b = 3 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), 0);
    KUNIT_EXPECT_EQ(test, req.result, (s64)7);
}

/**
 * @brief SUB: smaller minus larger gives a negative result.
 */
static void test_sub_negative_result(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_SUB, .a = 3, .b = 10 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), 0);
    KUNIT_EXPECT_EQ(test, req.result, (s64)-7);
}

/**
 * @brief SUB: equal operands give zero.
 */
static void test_sub_zero_result(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_SUB, .a = 42, .b = 42 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), 0);
    KUNIT_EXPECT_EQ(test, req.result, (s64)0);
}

/**
 * @brief SUB: both operands negative and equal gives zero.
 */
static void test_sub_both_negative(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_SUB, .a = -3, .b = -3 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), 0);
    KUNIT_EXPECT_EQ(test, req.result, (s64)0);
}

/* ── MUL tests ────────────────────────────────────────────────────────────── */

/**
 * @brief MUL: two positive operands.
 */
static void test_mul_positive(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_MUL, .a = 6, .b = 7 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), 0);
    KUNIT_EXPECT_EQ(test, req.result, (s64)42);
}

/**
 * @brief MUL: anything times zero is zero.
 */
static void test_mul_by_zero(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_MUL, .a = 999, .b = 0 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), 0);
    KUNIT_EXPECT_EQ(test, req.result, (s64)0);
}

/**
 * @brief MUL: negative times negative gives positive.
 */
static void test_mul_negative_both(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_MUL, .a = -3, .b = -4 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), 0);
    KUNIT_EXPECT_EQ(test, req.result, (s64)12);
}

/**
 * @brief MUL: positive times negative gives negative.
 */
static void test_mul_mixed_sign(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_MUL, .a = -5, .b = 4 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), 0);
    KUNIT_EXPECT_EQ(test, req.result, (s64)-20);
}

/**
 * @brief MUL: identity element — anything times one equals itself.
 */
static void test_mul_by_one(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_MUL, .a = 12345, .b = 1 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), 0);
    KUNIT_EXPECT_EQ(test, req.result, (s64)12345);
}

/* ── DIV tests ────────────────────────────────────────────────────────────── */

/**
 * @brief DIV: exact division with no remainder.
 */
static void test_div_exact(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_DIV, .a = 10, .b = 2 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), 0);
    KUNIT_EXPECT_EQ(test, req.result, (s64)5);
}

/**
 * @brief DIV: result truncates toward zero (C integer semantics).
 */
static void test_div_truncates(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_DIV, .a = 7, .b = 2 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), 0);
    KUNIT_EXPECT_EQ(test, req.result, (s64)3);
}

/**
 * @brief DIV: negative dividend, positive divisor gives negative quotient.
 */
static void test_div_negative(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_DIV, .a = -10, .b = 2 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), 0);
    KUNIT_EXPECT_EQ(test, req.result, (s64)-5);
}

/**
 * @brief DIV: identity element — anything divided by one equals itself.
 */
static void test_div_by_one(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_DIV, .a = 42, .b = 1 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), 0);
    KUNIT_EXPECT_EQ(test, req.result, (s64)42);
}

/**
 * @brief DIV: division by zero returns -EDOM.
 */
static void test_div_by_zero_returns_edom(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_DIV, .a = 10, .b = 0 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), -EDOM);
}

/**
 * @brief DIV: the result field must not be modified when division by zero occurs.
 *
 * Verifies that a failed calculation leaves the caller's result value intact,
 * preventing use of stale or uninitialised data.
 */
static void test_div_zero_does_not_modify_result(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_DIV, .a = 10, .b = 0, .result = 0xDEAD };
    mathdev_calculate_test(&req);
    KUNIT_EXPECT_EQ(test, req.result, (s64)0xDEAD);
}

/* ── Error handling ───────────────────────────────────────────────────────── */

/**
 * @brief Unknown op code (0xFF) returns -EINVAL.
 */
static void test_unknown_op_returns_einval(struct kunit *test)
{
    struct math_request req = { .op = 0xFF, .a = 1, .b = 2 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), -EINVAL);
}

/**
 * @brief Op code 0 (not assigned) returns -EINVAL.
 */
static void test_op_zero_returns_einval(struct kunit *test)
{
    struct math_request req = { .op = 0, .a = 1, .b = 2 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), -EINVAL);
}

/* ── Test suite definitions ───────────────────────────────────────────────── */

/** @brief KUnit test cases for the ADD operator. */
static struct kunit_case mathdev_add_cases[] = {
    KUNIT_CASE(test_add_positive),
    KUNIT_CASE(test_add_negative),
    KUNIT_CASE(test_add_zero),
    KUNIT_CASE(test_add_mixed_sign),
    KUNIT_CASE(test_add_large_values),
    {}
};

/** @brief KUnit test cases for the SUB operator. */
static struct kunit_case mathdev_sub_cases[] = {
    KUNIT_CASE(test_sub_positive_result),
    KUNIT_CASE(test_sub_negative_result),
    KUNIT_CASE(test_sub_zero_result),
    KUNIT_CASE(test_sub_both_negative),
    {}
};

/** @brief KUnit test cases for the MUL operator. */
static struct kunit_case mathdev_mul_cases[] = {
    KUNIT_CASE(test_mul_positive),
    KUNIT_CASE(test_mul_by_zero),
    KUNIT_CASE(test_mul_negative_both),
    KUNIT_CASE(test_mul_mixed_sign),
    KUNIT_CASE(test_mul_by_one),
    {}
};

/** @brief KUnit test cases for the DIV operator. */
static struct kunit_case mathdev_div_cases[] = {
    KUNIT_CASE(test_div_exact),
    KUNIT_CASE(test_div_truncates),
    KUNIT_CASE(test_div_negative),
    KUNIT_CASE(test_div_by_one),
    KUNIT_CASE(test_div_by_zero_returns_edom),
    KUNIT_CASE(test_div_zero_does_not_modify_result),
    {}
};

/** @brief KUnit test cases for error / boundary handling. */
static struct kunit_case mathdev_error_cases[] = {
    KUNIT_CASE(test_unknown_op_returns_einval),
    KUNIT_CASE(test_op_zero_returns_einval),
    {}
};

static struct kunit_suite mathdev_add_suite = {
    .name  = "mathdev_add",
    .test_cases = mathdev_add_cases,
};

static struct kunit_suite mathdev_sub_suite = {
    .name  = "mathdev_sub",
    .test_cases = mathdev_sub_cases,
};

static struct kunit_suite mathdev_mul_suite = {
    .name  = "mathdev_mul",
    .test_cases = mathdev_mul_cases,
};

static struct kunit_suite mathdev_div_suite = {
    .name  = "mathdev_div",
    .test_cases = mathdev_div_cases,
};

static struct kunit_suite mathdev_error_suite = {
    .name  = "mathdev_errors",
    .test_cases = mathdev_error_cases,
};

kunit_test_suites(
    &mathdev_add_suite,
    &mathdev_sub_suite,
    &mathdev_mul_suite,
    &mathdev_div_suite,
    &mathdev_error_suite,
);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("mathdev-project");
MODULE_DESCRIPTION("KUnit tests for mathdev kernel module");
