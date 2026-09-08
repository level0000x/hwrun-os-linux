// HWRun kernel boundary shared by BUS and kernel protocol modules.
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include "hwrun_kernel.h"
#include "uapi/hwrun.h"

#include <linux/stddef.h>

// ABI 固化：与用户态 bus/src/kctl.c 的 hwrun_kctl_desc_t 逐字节对齐。
// 改动本结构或 kctl.h 的任一 MAX 常量都会在此编译期失败。
_Static_assert(sizeof(struct hwrun_protocol_desc) == 152,
               "uapi protocol desc ABI size drift");
_Static_assert(offsetof(struct hwrun_protocol_desc, protocol) == 0,
               "uapi desc protocol offset");
_Static_assert(offsetof(struct hwrun_protocol_desc, version) == 64,
               "uapi desc version offset");
_Static_assert(offsetof(struct hwrun_protocol_desc, provider) == 80,
               "uapi desc provider offset");
_Static_assert(offsetof(struct hwrun_protocol_desc, implementation) == 144,
               "uapi desc impl offset");

struct hwrun_protocol {
    struct list_head node;
    char protocol[HWRUN_PROTOCOL_NAME_MAX];
    char version[HWRUN_PROTOCOL_VERSION_MAX];
    char provider[HWRUN_PROVIDER_NAME_MAX];
    void *implementation;
};

static LIST_HEAD(hwrun_protocols);
static DEFINE_MUTEX(hwrun_protocols_lock);

int hwrun_protocol_register(const char *protocol, const char *version,
                            const char *provider, void *implementation)
{
    struct hwrun_protocol *entry;

    if (!protocol || !version || !provider || !protocol[0] || !provider[0])
        return -EINVAL;
    mutex_lock(&hwrun_protocols_lock);
    list_for_each_entry(entry, &hwrun_protocols, node) {
        if (!strcmp(entry->protocol, protocol)) {
            mutex_unlock(&hwrun_protocols_lock);
            return -EEXIST;
        }
    }
    entry = kzalloc(sizeof(*entry), GFP_KERNEL);
    if (!entry) {
        mutex_unlock(&hwrun_protocols_lock);
        return -ENOMEM;
    }
    strscpy(entry->protocol, protocol, sizeof(entry->protocol));
    strscpy(entry->version, version, sizeof(entry->version));
    strscpy(entry->provider, provider, sizeof(entry->provider));
    entry->implementation = implementation;
    list_add_tail(&entry->node, &hwrun_protocols);
    mutex_unlock(&hwrun_protocols_lock);
    return 0;
}
EXPORT_SYMBOL_GPL(hwrun_protocol_register);

int hwrun_protocol_unregister(const char *protocol, const char *provider)
{
    struct hwrun_protocol *entry, *tmp;
    int result = -ENOENT;

    if (!protocol || !provider) return -EINVAL;
    mutex_lock(&hwrun_protocols_lock);
    list_for_each_entry_safe(entry, tmp, &hwrun_protocols, node) {
        if (!strcmp(entry->protocol, protocol) && !strcmp(entry->provider, provider)) {
            list_del(&entry->node);
            kfree(entry);
            result = 0;
            break;
        }
    }
    mutex_unlock(&hwrun_protocols_lock);
    return result;
}
EXPORT_SYMBOL_GPL(hwrun_protocol_unregister);

int hwrun_protocol_resolve(const char *protocol, char *version, size_t version_size,
                           char *provider, size_t provider_size, void **implementation)
{
    struct hwrun_protocol *entry;
    int result = -ENOENT;

    if (!protocol || !version || !provider || !implementation) return -EINVAL;
    mutex_lock(&hwrun_protocols_lock);
    list_for_each_entry(entry, &hwrun_protocols, node) {
        if (!strcmp(entry->protocol, protocol)) {
            strscpy(version, entry->version, version_size);
            strscpy(provider, entry->provider, provider_size);
            *implementation = entry->implementation;
            result = 0;
            break;
        }
    }
    mutex_unlock(&hwrun_protocols_lock);
    return result;
}
EXPORT_SYMBOL_GPL(hwrun_protocol_resolve);

static long hwrun_ioctl(struct file *file, unsigned int command, unsigned long argument)
{
    __u32 value;
    struct hwrun_protocol_desc descriptor;
    char version[HWRUN_PROTOCOL_VERSION_MAX];
    char provider[HWRUN_PROVIDER_NAME_MAX];
    void *implementation;
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
    case HWRUN_IOC_PROTOCOL_REGISTER:
        if (copy_from_user(&descriptor, (void __user *)argument, sizeof(descriptor)))
            return -EFAULT;
        return hwrun_protocol_register(descriptor.protocol, descriptor.version,
                                       descriptor.provider, NULL);
    case HWRUN_IOC_PROTOCOL_UNREGISTER:
        if (copy_from_user(&descriptor, (void __user *)argument, sizeof(descriptor)))
            return -EFAULT;
        return hwrun_protocol_unregister(descriptor.protocol, descriptor.provider);
    case HWRUN_IOC_PROTOCOL_RESOLVE:
        if (copy_from_user(&descriptor, (void __user *)argument, sizeof(descriptor)))
            return -EFAULT;
        if (hwrun_protocol_resolve(descriptor.protocol, version, sizeof(version),
                                   provider, sizeof(provider), &implementation))
            return -ENOENT;
        memset(&descriptor, 0, sizeof(descriptor));
        strscpy(descriptor.version, version, sizeof(descriptor.version));
        strscpy(descriptor.provider, provider, sizeof(descriptor.provider));
        descriptor.implementation = (__u64)(uintptr_t)implementation;
        return copy_to_user((void __user *)argument, &descriptor, sizeof(descriptor)) ? -EFAULT : 0;
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
