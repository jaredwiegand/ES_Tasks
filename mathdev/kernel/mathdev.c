// SPDX-License-Identifier: GPL-2.0
/**
 * @file mathdev.c
 * @brief Linux kernel character device for math operations.
 *
 * Registers a character device at /dev/mathdev that accepts ioctl requests
 * containing two signed 64-bit integers and an arithmetic operator, then
 * returns the computed result.
 *
 * Supported operators: ADD, SUB, MUL, DIV (see MATH_OP_* in mathdev.h).
 *
 * Compatible with Linux kernel >= 5.x.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/ioctl.h>
#include <linux/slab.h>
#include <linux/mutex.h>

#include "mathdev.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("mathdev-project");
MODULE_DESCRIPTION("Character device for basic math operations on signed integers");
MODULE_VERSION("1.0.0");

#define DEVICE_NAME     "mathdev"   /**< Name used for the cdev and device node. */
#define CLASS_NAME      "math"      /**< sysfs class name (/sys/class/math). */
#define NUM_DEVICES     1           /**< Number of minor devices to register. */

static int          major_number;
static struct class  *math_class  = NULL;
static struct device *math_device = NULL;
static struct cdev   math_cdev;

/**
 * @brief Per-open-instance state allocated on each open() call.
 *
 * Holds a per-fd mutex and an operation counter.  Although the math
 * operations themselves are stateless, this structure follows the
 * standard pattern for character devices that may later need per-fd state.
 *
 * @var mathdev_instance::lock      Serialises concurrent ioctl calls on the same fd.
 * @var mathdev_instance::op_count  Cumulative count of successful operations on this fd.
 */
struct mathdev_instance {
    struct mutex lock;
    u64          op_count;
};

/* ------------------------------------------------------------------ */
/*  file_operations                                                     */
/* ------------------------------------------------------------------ */

/**
 * @brief Open handler: allocates and initialises per-fd instance state.
 *
 * @param inode Inode of the device file (unused).
 * @param file  File structure; @c private_data is set to the new instance.
 *
 * @return 0 on success, -ENOMEM if instance allocation fails.
 */
static int mathdev_open(struct inode *inode, struct file *file)
{
    struct mathdev_instance *inst;
    (void)inode;

    inst = kzalloc(sizeof(*inst), GFP_KERNEL);
    if (!inst)
        return -ENOMEM;

    mutex_init(&inst->lock);
    file->private_data = inst;

    pr_info("mathdev: Device opened! (pid=%d)\n", current->pid);
    return 0;
}

/**
 * @brief Release handler: logs operation count and frees per-fd state.
 *
 * @param inode Inode of the device file (unused).
 * @param file  File structure; @c private_data points to the instance to free.
 *
 * @return Always 0.
 */
static int mathdev_release(struct inode *inode, struct file *file)
{
    struct mathdev_instance *inst = file->private_data;
    (void)inode;

    if (inst) {
        pr_info("mathdev: Device closed. Total operations performed: %llu\n",
                inst->op_count);
        kfree(inst);
        file->private_data = NULL;
    }
    return 0;
}

/**
 * @brief Core calculation function.
 *
 * Performs the arithmetic described by @p req->op on @p req->a and
 * @p req->b, writing the result into @p req->result.
 *
 * @param req  Pointer to the math request structure (modified in place).
 *
 * @return 0 on success.
 * @retval -EDOM   @p req->b is zero and @p req->op is MATH_OP_DIV.
 * @retval -EINVAL @p req->op is not a recognised operator code.
 */
static int mathdev_calculate(struct math_request *req)
{
    switch (req->op) {
    case MATH_OP_ADD:
        pr_debug("mathdev: Calculating %lld + %lld!\n", req->a, req->b);
        req->result = req->a + req->b;
        break;

    case MATH_OP_SUB:
        pr_debug("mathdev: Calculating %lld - %lld!\n", req->a, req->b);
        req->result = req->a - req->b;
        break;

    case MATH_OP_MUL:
        pr_debug("mathdev: Calculating %lld * %lld!\n", req->a, req->b);
        req->result = req->a * req->b;
        break;

    case MATH_OP_DIV:
        if (req->b == 0) {
            pr_warn("mathdev: Division by zero requested!\n");
            return -EDOM;   /* POSIX errno: argument out of mathematical domain */
        }
        pr_debug("mathdev: Calculating %lld / %lld!\n", req->a, req->b);
        req->result = req->a / req->b;
        break;

    default:
        pr_warn("mathdev: Unknown operator %u\n", req->op);
        return -EINVAL;
    }
    return 0;
}

/**
 * @brief ioctl handler: dispatches MATH_IOCTL_CALC and MATH_IOCTL_QUERY_OPS.
 *
 * Rejects any command whose magic number does not match MATHDEV_IOC_MAGIC.
 * For MATH_IOCTL_CALC the request is copied from userspace, computed under
 * the per-fd mutex, and the result is copied back only on success.
 *
 * @param file Pointer to the open file (carries per-fd instance state).
 * @param cmd  ioctl command number.
 * @param arg  ioctl argument (user-space pointer, command-dependent).
 *
 * @return 0 on success, or a negative errno value on failure.
 * @retval -ENOTTY  @p cmd does not belong to this device.
 * @retval -EFAULT  User-space pointer copy failed.
 * @retval -EDOM    Division by zero (propagated from mathdev_calculate).
 * @retval -EINVAL  Unknown math operator (propagated from mathdev_calculate).
 */
static long mathdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct mathdev_instance *inst = file->private_data;
    struct math_request      req;
    int ret = 0;

    /* Reject commands from other subsystems early to avoid spurious processing. */
    if (_IOC_TYPE(cmd) != MATHDEV_IOC_MAGIC)
        return -ENOTTY;

    switch (cmd) {
    /* ---- MATH_IOCTL_CALC ------------------------------------------ */
    case MATH_IOCTL_CALC:
        if (copy_from_user(&req, (struct math_request __user *)arg, sizeof(req)))
            return -EFAULT;

        /* Serialise per-fd to prevent races on op_count from concurrent ioctls. */
        mutex_lock(&inst->lock);
        ret = mathdev_calculate(&req);
        if (ret == 0)
            inst->op_count++;
        mutex_unlock(&inst->lock);

        /* Only write back if calculation succeeded; on error result is undefined. */
        if (ret == 0) {
            if (copy_to_user((struct math_request __user *)arg, &req, sizeof(req)))
                return -EFAULT;
        }
        return ret;

    /* ---- MATH_IOCTL_QUERY_OPS ------------------------------------- */
    case MATH_IOCTL_QUERY_OPS: {
        /* Build the ops table inline; it is constant and small enough for the stack. */
        struct math_ops_info info = {
            .num_ops = MATH_NUM_OPS,
            .ops = {
                { MATH_OP_ADD, "ADD", "Add two signed integers"      },
                { MATH_OP_SUB, "SUB", "Subtract two signed integers" },
                { MATH_OP_MUL, "MUL", "Multiply two signed integers" },
                { MATH_OP_DIV, "DIV", "Divide two signed integers"   },
            },
        };
        if (copy_to_user((struct math_ops_info __user *)arg, &info, sizeof(info)))
            return -EFAULT;
        return 0;
    }

    default:
        return -ENOTTY;
    }
}

/** @brief File operations table registered with the kernel for this device. */
static const struct file_operations mathdev_fops = {
    .owner          = THIS_MODULE,
    .open           = mathdev_open,
    .release        = mathdev_release,
    .unlocked_ioctl = mathdev_ioctl,
};

/* ------------------------------------------------------------------ */
/*  Module init / exit                                                  */
/* ------------------------------------------------------------------ */

/**
 * @brief Module initialisation: allocates the major number, creates the cdev,
 *        sysfs class, and device node.
 *
 * Uses a goto-based unwind chain so that every resource acquired is released
 * exactly once if any later step fails.
 *
 * @return 0 on success, or a negative errno value if any step fails.
 */
static int __init mathdev_init(void)
{
    dev_t dev;
    int   ret;

    /* Dynamically allocate a major number; avoids hard-coding and conflicts. */
    ret = alloc_chrdev_region(&dev, 0, NUM_DEVICES, DEVICE_NAME);
    if (ret < 0) {
        pr_err("mathdev: Failed to allocate chrdev region: %d\n", ret);
        return ret;
    }
    major_number = MAJOR(dev);

    /* Initialise the embedded cdev and add it to the kernel's device table. */
    cdev_init(&math_cdev, &mathdev_fops);
    math_cdev.owner = THIS_MODULE;
    ret = cdev_add(&math_cdev, dev, NUM_DEVICES);
    if (ret < 0) {
        pr_err("mathdev: Failed to add cdev: %d\n", ret);
        goto err_cdev;
    }

    /* class_create signature changed in kernel 6.4: the THIS_MODULE arg was dropped. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    math_class = class_create(CLASS_NAME);
#else
    math_class = class_create(THIS_MODULE, CLASS_NAME);
#endif
    if (IS_ERR(math_class)) {
        ret = PTR_ERR(math_class);
        pr_err("mathdev: Failed to create class: %d\n", ret);
        goto err_class;
    }

    /* device_create populates /dev/mathdev via udev/mdev. */
    math_device = device_create(math_class, NULL, dev, NULL, DEVICE_NAME);
    if (IS_ERR(math_device)) {
        ret = PTR_ERR(math_device);
        pr_err("mathdev: Failed to create device: %d\n", ret);
        goto err_device;
    }

    pr_info("mathdev: Module loaded. Major=%d  Device=/dev/%s\n",
            major_number, DEVICE_NAME);
    return 0;

err_device:
    class_destroy(math_class);
err_class:
    cdev_del(&math_cdev);
err_cdev:
    unregister_chrdev_region(dev, NUM_DEVICES);
    return ret;
}

/**
 * @brief Module teardown: removes the device node, sysfs class, cdev, and
 *        releases the major number.
 *
 * Mirrors mathdev_init() in reverse order to ensure clean teardown.
 */
static void __exit mathdev_exit(void)
{
    dev_t dev = MKDEV(major_number, 0);

    device_destroy(math_class, dev);
    class_destroy(math_class);
    cdev_del(&math_cdev);
    unregister_chrdev_region(dev, NUM_DEVICES);

    pr_info("mathdev: Module unloaded.\n");
}

module_init(mathdev_init);
module_exit(mathdev_exit);
