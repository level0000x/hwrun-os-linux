#ifndef HWRUN_UAPI_H
#define HWRUN_UAPI_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define HWRUN_ABI_VERSION 1
#define HWRUN_DEVICE_NAME "hwrun"
#define HWRUN_IOC_MAGIC 0x48
#define HWRUN_IOC_GET_ABI _IOR(HWRUN_IOC_MAGIC, 0x00, __u32)
#define HWRUN_IOC_PING _IOWR(HWRUN_IOC_MAGIC, 0x01, __u32)

#define HWRUN_PROTOCOL_NAME_MAX 64
#define HWRUN_PROTOCOL_VERSION_MAX 16
#define HWRUN_PROVIDER_NAME_MAX 64

struct hwrun_protocol_desc {
	char protocol[HWRUN_PROTOCOL_NAME_MAX];
	char version[HWRUN_PROTOCOL_VERSION_MAX];
	char provider[HWRUN_PROVIDER_NAME_MAX];
	__u64 implementation;
};

#define HWRUN_IOC_PROTOCOL_REGISTER _IOW(HWRUN_IOC_MAGIC, 0x10, struct hwrun_protocol_desc)
#define HWRUN_IOC_PROTOCOL_UNREGISTER _IOW(HWRUN_IOC_MAGIC, 0x11, struct hwrun_protocol_desc)
#define HWRUN_IOC_PROTOCOL_RESOLVE _IOWR(HWRUN_IOC_MAGIC, 0x12, struct hwrun_protocol_desc)

#endif
