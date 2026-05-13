/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/**
 * @file mathdev.h
 * @brief Shared header for the mathdev kernel module and userspace clients.
 *
 * Defines the ioctl interface, operator constants, and data structures
 * shared between the kernel module and all userspace consumers.
 *
 * The same header is compiled in both kernel and userspace contexts; the
 * @c __KERNEL__ guard selects the appropriate type headers for each.
 */

#ifndef _MATHDEV_H_
#define _MATHDEV_H_

#ifdef __KERNEL__
#  include <linux/types.h>
#  include <linux/version.h>
#  include <linux/ioctl.h>
#else
#  include <stdint.h>
#  include <sys/ioctl.h>
   typedef int64_t  s64;
   typedef uint32_t u32;
#endif

/* ------------------------------------------------------------------ */
/*  Math operator constants                                             */
/* ------------------------------------------------------------------ */

/**
 * @defgroup MathOps Math operator codes
 * @brief Numeric codes identifying the requested arithmetic operation.
 *
 * These values are placed in math_request::op before calling
 * MATH_IOCTL_CALC.  They also appear in math_op_desc::op_code entries
 * returned by MATH_IOCTL_QUERY_OPS.
 * @{
 */
#define MATH_OP_ADD   0x01  /**< Addition:       result = a + b */
#define MATH_OP_SUB   0x02  /**< Subtraction:    result = a - b */
#define MATH_OP_MUL   0x03  /**< Multiplication: result = a * b */
#define MATH_OP_DIV   0x04  /**< Division:       result = a / b (integer, truncates toward zero) */
/** @} */

/** @brief Total number of supported math operators. */
#define MATH_NUM_OPS  4

/* ------------------------------------------------------------------ */
/*  Data structures                                                     */
/* ------------------------------------------------------------------ */

/**
 * @brief Single math operation request and response container.
 *
 * Passed as the @c arg to MATH_IOCTL_CALC.  The caller populates
 * @a a, @a b, and @a op before the ioctl call; the kernel writes
 * @a result on success.
 *
 * @note The struct is packed so that the kernel and userspace see an
 *       identical memory layout regardless of compiler padding rules.
 *
 * @var math_request::a      First operand (signed 64-bit).
 * @var math_request::b      Second operand (signed 64-bit).
 * @var math_request::result Result written by the kernel on success.
 * @var math_request::op     Operator code (one of MATH_OP_ADD .. MATH_OP_DIV).
 * @var math_request::_pad   Explicit padding; keeps the struct 8-byte aligned.
 */
struct math_request {
    s64 a;
    s64 b;
    s64 result;
    u32 op;
    u32 _pad;
} __attribute__((packed));

/** @brief Maximum length of an operator short name string (including NUL terminator). */
#define MATH_OP_NAME_LEN  8

/** @brief Maximum length of an operator description string (including NUL terminator). */
#define MATH_OP_DESC_LEN  48

/**
 * @brief Descriptor for a single available math operation.
 *
 * Populated by the kernel and returned inside math_ops_info when the
 * caller issues MATH_IOCTL_QUERY_OPS.
 *
 * @var math_op_desc::op_code     Operator code (one of MATH_OP_*).
 * @var math_op_desc::name        Short operator name, NUL-terminated (e.g. "ADD").
 * @var math_op_desc::description Human-readable description, NUL-terminated.
 */
struct math_op_desc {
    u32  op_code;
    char name[MATH_OP_NAME_LEN];
    char description[MATH_OP_DESC_LEN];
};

/**
 * @brief Full list of available operations, returned by MATH_IOCTL_QUERY_OPS.
 *
 * @var math_ops_info::num_ops  Number of valid entries in @a ops.
 * @var math_ops_info::_pad     Alignment padding; do not use.
 * @var math_ops_info::ops      Array of operation descriptors.
 */
struct math_ops_info {
    u32               num_ops;
    u32               _pad;
    struct math_op_desc ops[MATH_NUM_OPS];
};

/* ------------------------------------------------------------------ */
/*  ioctl definitions                                                   */
/* ------------------------------------------------------------------ */

/** @brief Magic number identifying mathdev ioctl commands. */
#define MATHDEV_IOC_MAGIC     'm'

/**
 * @brief Perform a math calculation.
 *
 * Direction: read/write (user → kernel for inputs, kernel → user for result).
 * @c arg must be a pointer to struct math_request.
 *
 * The caller sets @c a, @c b, and @c op before the call.  On success the
 * kernel writes @c result back through the same pointer.
 *
 * @return 0 on success.
 * @retval -EDOM   Division by zero was requested.
 * @retval -EINVAL Unknown operator code.
 * @retval -EFAULT Bad user-space pointer.
 */
#define MATH_IOCTL_CALC      _IOWR(MATHDEV_IOC_MAGIC, 1, struct math_request)

/**
 * @brief Query the list of available operations.
 *
 * Direction: read (kernel → user).
 * @c arg must be a pointer to struct math_ops_info.
 *
 * @return 0 on success.
 * @retval -EFAULT Bad user-space pointer.
 */
#define MATH_IOCTL_QUERY_OPS  _IOR(MATHDEV_IOC_MAGIC, 2, struct math_ops_info)

/* ------------------------------------------------------------------ */
/*  Error codes returned by ioctl (as negative errno values)           */
/*    EINVAL  - unknown operator                                        */
/*    EDOM    - division by zero                                        */
/*    EFAULT  - bad user pointer                                        */
/*    ENOTTY  - unknown ioctl command                                   */
/* ------------------------------------------------------------------ */

/** @brief Filesystem path of the mathdev character device node. */
#define MATHDEV_PATH "/dev/mathdev"

#endif /* _MATHDEV_H_ */
