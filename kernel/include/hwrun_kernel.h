#ifndef HWRUN_KERNEL_H
#define HWRUN_KERNEL_H

#include <linux/types.h>

int hwrun_protocol_register(const char *protocol, const char *version,
                            const char *provider, void *implementation);
int hwrun_protocol_unregister(const char *protocol, const char *provider);
int hwrun_protocol_resolve(const char *protocol, char *version, size_t version_size,
                           char *provider, size_t provider_size, void **implementation);

#endif