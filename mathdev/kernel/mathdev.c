// SPDX-License-Identifier: GPL-2.0
/*
 * mathdev.c - Linux Kernel Character Device for Math Operations
 *
 * Exposes a chardev at /dev/mathdev that accepts ioctl requests
 * containing two signed 64-bit integers and an operator, returning
 * the computed result.
 *
 * Supported operators: ADD, SUB, MUL, DIV
 *
 * Compatible with Linux kernel >= 5.x
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

#define DEVICE_NAME     "mathdev"
#define CLASS_NAME      "math"
#define NUM_DEVICES     1

static int          major_number;
static struct class  *math_class  = NULL;
static struct device *math_device = NULL;
static struct cdev   math_cdev;

/* Per-open-instance state (not strictly needed for stateless ops, but good practice) */
struct mathdev_instance {
    struct mutex lock;
    u64          op_count;
};

/* ------------------------------------------------------------------ */
/*  file_operations                                                     */
/* ------------------------------------------------------------------ */

static int mathdev_open(struct inode *inode, struct file *file)
{
    struct mathdev_instance *inst;

    inst = kzalloc(sizeof(*inst), GFP_KERNEL);
    if (!inst)
        return -ENOMEM;

    mutex_init(&inst->lock);
    file->private_data = inst;

    pr_info("mathdev: Device opened! (pid=%d)\n", current->pid);
    return 0;
}

static int mathdev_release(struct inode *inode, struct file *file)
{
    struct mathdev_instance *inst = file->private_data;

    if (inst) {
        pr_info("mathdev: Device closed. Total operations performed: %llu\n",
                inst->op_count);
        kfree(inst);
        file->private_data = NULL;
    }
    return 0;
}

/*
 * Core calculation function.  Returns 0 on success, negative errno on error.
 * Result is written into req->result.
 */
static int mathdev_calculate(struct math_request *req)
{
    switch (req->op) {
    case MATH_OP_ADD:
        pr_info("mathdev: Calculating %lld + %lld!\n", req->a, req->b);
        req->result = req->a + req->b;
        break;

    case MATH_OP_SUB:
        pr_info("mathdev: Calculating %lld - %lld!\n", req->a, req->b);
        req->result = req->a - req->b;
        break;

    case MATH_OP_MUL:
        pr_info("mathdev: Calculating %lld * %lld!\n", req->a, req->b);
        req->result = req->a * req->b;
        break;

    case MATH_OP_DIV:
        if (req->b == 0) {
            pr_warn("mathdev: Division by zero requested!\n");
            return -EDOM;           /* errno: math arg out of domain */
        }
        pr_info("mathdev: Calculating %lld / %lld!\n", req->a, req->b);
        req->result = req->a / req->b;
        break;

    default:
        pr_warn("mathdev: Unknown operator %u\n", req->op);
        return -EINVAL;
    }
    return 0;
}

static long mathdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct mathdev_instance *inst = file->private_data;
    struct math_request      req;
    int ret = 0;

    /* Only our own ioctl magic is accepted */
    if (_IOC_TYPE(cmd) != MATHDEV_IOC_MAGIC)
        return -ENOTTY;

    switch (cmd) {
    /* ---- MATH_IOCTL_CALC ------------------------------------------ */
    case MATH_IOCTL_CALC:
        if (copy_from_user(&req, (struct math_request __user *)arg, sizeof(req)))
            return -EFAULT;

        mutex_lock(&inst->lock);
        ret = mathdev_calculate(&req);
        if (ret == 0)
            inst->op_count++;
        mutex_unlock(&inst->lock);

        if (ret == 0) {
            if (copy_to_user((struct math_request __user *)arg, &req, sizeof(req)))
                return -EFAULT;
        }
        return ret;   /* 0 on success, -EDOM on div/0, -EINVAL on bad op */

    /* ---- MATH_IOCTL_QUERY_OPS ------------------------------------- */
    case MATH_IOCTL_QUERY_OPS: {
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

static const struct file_operations mathdev_fops = {
    .owner          = THIS_MODULE,
    .open           = mathdev_open,
    .release        = mathdev_release,
    .unlocked_ioctl = mathdev_ioctl,
};

/* ------------------------------------------------------------------ */
/*  Module init / exit                                                  */
/* ------------------------------------------------------------------ */

static int __init mathdev_init(void)
{
    dev_t dev;
    int   ret;

    /* Allocate a major number dynamically */
    ret = alloc_chrdev_region(&dev, 0, NUM_DEVICES, DEVICE_NAME);
    if (ret < 0) {
        pr_err("mathdev: Failed to allocate chrdev region: %d\n", ret);
        return ret;
    }
    major_number = MAJOR(dev);

    /* Init and add the cdev */
    cdev_init(&math_cdev, &mathdev_fops);
    math_cdev.owner = THIS_MODULE;
    ret = cdev_add(&math_cdev, dev, NUM_DEVICES);
    if (ret < 0) {
        pr_err("mathdev: Failed to add cdev: %d\n", ret);
        goto err_cdev;
    }

    /* Create /sys/class/math */
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

    /* Create /dev/mathdev */
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
