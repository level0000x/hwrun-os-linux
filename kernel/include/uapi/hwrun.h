#ifndef HWRUN_UAPI_H
#define HWRUN_UAPI_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define HWRUN_ABI_VERSION 1
#define HWRUN_DEVICE_NAME "hwrun"
#define HWRUN_IOC_MAGIC 0x48
#define HWRUN_IOC_GET_ABI _IOR(HWRUN_IOC_MAGIC, 0x00, __u32)
#define HWRUN_IOC_PING _IOWR(HWRUN_IOC_MAGIC, 0x01, __u32)

#endif
