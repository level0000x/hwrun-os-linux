// HWRun kernel boundary shared by BUS and kernel protocol modules.
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/uaccess.h>

#include "uapi/hwrun.h"

static long hwrun_ioctl(struct file *file, unsigned int command, unsigned long argument)
{
    __u32 value;
    (void)file;

    switch (command) {
    case HWRUN_IOC_GET_ABI:
        value = HWRUN_ABI_VERSION;
        return copy_to_user((void __user *)argument, &value, sizeof(value)) ? -EFAULT : 0;
    case HWRUN_IOC_PING:
        if (copy_from_user(&value, (void __user *)argument, sizeof(value)))
            return -EFAULT;
        value = ~value;
        return copy_to_user((void __user *)argument, &value, sizeof(value)) ? -EFAULT : 0;
    default:
        return -ENOTTY;
    }
}

static const struct file_operations hwrun_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = hwrun_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl = hwrun_ioctl,
#endif
};

static struct miscdevice hwrun_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = HWRUN_DEVICE_NAME,
    .fops = &hwrun_fops,
    .mode = 0600,
};

static int __init hwrun_core_init(void)
{
    return misc_register(&hwrun_device);
}

static void __exit hwrun_core_exit(void)
{
    misc_deregister(&hwrun_device);
}

module_init(hwrun_core_init);
module_exit(hwrun_core_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("HWRun Linux kernel boundary");
MODULE_VERSION("1.0");
