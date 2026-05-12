/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * mathdev.h - Shared header for mathdev kernel module and userspace clients
 *
 * Defines the ioctl interface, operator constants, and data structures
 * shared between the kernel module and all userspace consumers.
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
   typedef uint8_t  u8;
#endif

/* ------------------------------------------------------------------ */
/*  Math operator constants                                             */
/* ------------------------------------------------------------------ */

#define MATH_OP_ADD   0x01
#define MATH_OP_SUB   0x02
#define MATH_OP_MUL   0x03
#define MATH_OP_DIV   0x04

#define MATH_NUM_OPS  4

/* ------------------------------------------------------------------ */
/*  Data structures                                                     */
/* ------------------------------------------------------------------ */

/**
 * struct math_request - Single math operation request / response
 * @a:      First operand  (signed 64-bit)
 * @b:      Second operand (signed 64-bit)
 * @op:     Operator       (MATH_OP_*)
 * @result: Result written by the kernel (signed 64-bit)
 * @_pad:   Padding to align to 8 bytes
 */
struct math_request {
    s64 a;
    s64 b;
    s64 result;
    u32 op;
    u32 _pad;
} __attribute__((packed));

/**
 * struct math_op_desc - Description of one available operation
 */
#define MATH_OP_NAME_LEN  8
#define MATH_OP_DESC_LEN  48

struct math_op_desc {
    u32  op_code;
    char name[MATH_OP_NAME_LEN];
    char description[MATH_OP_DESC_LEN];
};

/**
 * struct math_ops_info - Full list of operations (returned by QUERY_OPS)
 */
struct math_ops_info {
    u32               num_ops;
    u32               _pad;
    struct math_op_desc ops[MATH_NUM_OPS];
};

/* ------------------------------------------------------------------ */
/*  ioctl definitions                                                   */
/* ------------------------------------------------------------------ */

#define MATHDEV_IOC_MAGIC     'm'

/** Perform a calculation.  arg: pointer to struct math_request (in/out) */
#define MATH_IOCTL_CALC      _IOWR(MATHDEV_IOC_MAGIC, 1, struct math_request)

/** Query available operations. arg: pointer to struct math_ops_info (out) */
#define MATH_IOCTL_QUERY_OPS  _IOR(MATHDEV_IOC_MAGIC, 2, struct math_ops_info)

/* ------------------------------------------------------------------ */
/*  Error codes returned by ioctl (as negative errno values)           */
/*    EINVAL  - unknown operator                                        */
/*    EDOM    - division by zero                                        */
/*    EFAULT  - bad user pointer                                        */
/*    ENOTTY  - unknown ioctl command                                   */
/* ------------------------------------------------------------------ */

#define MATHDEV_PATH "/dev/mathdev"

#endif /* _MATHDEV_H_ */
