// SPDX-License-Identifier: GPL-2.0
/*
 * mathdev_kunit.c  –  KUnit tests for the mathdev kernel module
 *
 * Tests the mathdev_calculate() logic directly inside the kernel.
 *
 * Build & run:
 *   make -C /lib/modules/$(uname -r)/build \
 *        M=$(pwd) modules
 *   sudo insmod mathdev_kunit.ko
 *   sudo dmesg | grep -E "PASSED|FAILED|kunit"
 *
 * Or with kunit_tool (if available):
 *   ./tools/testing/kunit/kunit.py run --kunitconfig=tests/kernel/.kunitconfig
 *
 * Requires: Linux kernel >= 5.4 with CONFIG_KUNIT=y or =m
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

/* ── replicate mathdev_calculate() locally for testing ───────────────────── */

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

static void test_add_positive(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_ADD, .a = 42, .b = 37 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), 0);
    KUNIT_EXPECT_EQ(test, req.result, (s64)79);
}

static void test_add_negative(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_ADD, .a = -10, .b = -5 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), 0);
    KUNIT_EXPECT_EQ(test, req.result, (s64)-15);
}

static void test_add_zero(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_ADD, .a = 0, .b = 0 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), 0);
    KUNIT_EXPECT_EQ(test, req.result, (s64)0);
}

static void test_add_mixed_sign(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_ADD, .a = -100, .b = 200 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), 0);
    KUNIT_EXPECT_EQ(test, req.result, (s64)100);
}

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

static void test_sub_positive_result(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_SUB, .a = 10, .b = 3 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), 0);
    KUNIT_EXPECT_EQ(test, req.result, (s64)7);
}

static void test_sub_negative_result(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_SUB, .a = 3, .b = 10 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), 0);
    KUNIT_EXPECT_EQ(test, req.result, (s64)-7);
}

static void test_sub_zero_result(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_SUB, .a = 42, .b = 42 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), 0);
    KUNIT_EXPECT_EQ(test, req.result, (s64)0);
}

static void test_sub_both_negative(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_SUB, .a = -3, .b = -3 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), 0);
    KUNIT_EXPECT_EQ(test, req.result, (s64)0);
}

/* ── MUL tests ────────────────────────────────────────────────────────────── */

static void test_mul_positive(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_MUL, .a = 6, .b = 7 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), 0);
    KUNIT_EXPECT_EQ(test, req.result, (s64)42);
}

static void test_mul_by_zero(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_MUL, .a = 999, .b = 0 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), 0);
    KUNIT_EXPECT_EQ(test, req.result, (s64)0);
}

static void test_mul_negative_both(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_MUL, .a = -3, .b = -4 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), 0);
    KUNIT_EXPECT_EQ(test, req.result, (s64)12);
}

static void test_mul_mixed_sign(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_MUL, .a = -5, .b = 4 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), 0);
    KUNIT_EXPECT_EQ(test, req.result, (s64)-20);
}

static void test_mul_by_one(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_MUL, .a = 12345, .b = 1 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), 0);
    KUNIT_EXPECT_EQ(test, req.result, (s64)12345);
}

/* ── DIV tests ────────────────────────────────────────────────────────────── */

static void test_div_exact(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_DIV, .a = 10, .b = 2 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), 0);
    KUNIT_EXPECT_EQ(test, req.result, (s64)5);
}

static void test_div_truncates(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_DIV, .a = 7, .b = 2 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), 0);
    KUNIT_EXPECT_EQ(test, req.result, (s64)3);
}

static void test_div_negative(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_DIV, .a = -10, .b = 2 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), 0);
    KUNIT_EXPECT_EQ(test, req.result, (s64)-5);
}

static void test_div_by_one(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_DIV, .a = 42, .b = 1 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), 0);
    KUNIT_EXPECT_EQ(test, req.result, (s64)42);
}

static void test_div_by_zero_returns_edom(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_DIV, .a = 10, .b = 0 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), -EDOM);
}

static void test_div_zero_does_not_modify_result(struct kunit *test)
{
    struct math_request req = { .op = MATH_OP_DIV, .a = 10, .b = 0, .result = 0xDEAD };
    mathdev_calculate_test(&req);
    /* result must be unchanged when division by zero occurs */
    KUNIT_EXPECT_EQ(test, req.result, (s64)0xDEAD);
}

/* ── Error handling ───────────────────────────────────────────────────────── */

static void test_unknown_op_returns_einval(struct kunit *test)
{
    struct math_request req = { .op = 0xFF, .a = 1, .b = 2 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), -EINVAL);
}

static void test_op_zero_returns_einval(struct kunit *test)
{
    struct math_request req = { .op = 0, .a = 1, .b = 2 };
    KUNIT_EXPECT_EQ(test, mathdev_calculate_test(&req), -EINVAL);
}

/* ── Test suite definitions ───────────────────────────────────────────────── */

static struct kunit_case mathdev_add_cases[] = {
    KUNIT_CASE(test_add_positive),
    KUNIT_CASE(test_add_negative),
    KUNIT_CASE(test_add_zero),
    KUNIT_CASE(test_add_mixed_sign),
    KUNIT_CASE(test_add_large_values),
    {}
};

static struct kunit_case mathdev_sub_cases[] = {
    KUNIT_CASE(test_sub_positive_result),
    KUNIT_CASE(test_sub_negative_result),
    KUNIT_CASE(test_sub_zero_result),
    KUNIT_CASE(test_sub_both_negative),
    {}
};

static struct kunit_case mathdev_mul_cases[] = {
    KUNIT_CASE(test_mul_positive),
    KUNIT_CASE(test_mul_by_zero),
    KUNIT_CASE(test_mul_negative_both),
    KUNIT_CASE(test_mul_mixed_sign),
    KUNIT_CASE(test_mul_by_one),
    {}
};

static struct kunit_case mathdev_div_cases[] = {
    KUNIT_CASE(test_div_exact),
    KUNIT_CASE(test_div_truncates),
    KUNIT_CASE(test_div_negative),
    KUNIT_CASE(test_div_by_one),
    KUNIT_CASE(test_div_by_zero_returns_edom),
    KUNIT_CASE(test_div_zero_does_not_modify_result),
    {}
};

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
