# HWRun Linux kernel integration layer.
# The Linux source tree is intentionally external; this project owns only
# the HWRun configuration and kernel modules built against that tree.

LINUX_SRC ?= $(CURDIR)/../linux-src
KBUILD_OUT ?= $(CURDIR)/build/linux
MODULE_DIR := $(CURDIR)/modules
PROFILE ?= minimal
# 配置片段叠加：minimal/host 用单片段；qemu 在 minimal 之上叠加 qemu.config
ifeq ($(PROFILE),qemu)
CONFIG_FRAGMENTS := config/minimal.config config/qemu.config
else
CONFIG_FRAGMENTS := config/$(PROFILE).config
endif
JOBS ?= 1

.PHONY: all config prune kernel modules menuconfig oldconfig clean help

all: kernel modules

config:
	$(MAKE) -C $(LINUX_SRC) O=$(KBUILD_OUT) allnoconfig
	$(LINUX_SRC)/scripts/kconfig/merge_config.sh -m -O $(KBUILD_OUT) \
		$(KBUILD_OUT)/.config $(CONFIG_FRAGMENTS)
	$(MAKE) -C $(LINUX_SRC) O=$(KBUILD_OUT) olddefconfig
	$(MAKE) -C $(LINUX_SRC) O=$(KBUILD_OUT) modules_prepare

prune: config

kernel: config
	$(MAKE) -C $(LINUX_SRC) O=$(KBUILD_OUT) \
		-j$(JOBS) bzImage modules

modules: config
	$(MAKE) -C $(LINUX_SRC) O=$(KBUILD_OUT) M=$(MODULE_DIR) modules

menuconfig: config
	$(MAKE) -C $(LINUX_SRC) O=$(KBUILD_OUT) \
		menuconfig

oldconfig: config
	$(MAKE) -C $(LINUX_SRC) O=$(KBUILD_OUT) olddefconfig

clean:
	$(MAKE) -C $(LINUX_SRC) O=$(KBUILD_OUT) M=$(MODULE_DIR) clean
	rm -rf $(KBUILD_OUT)

help:
	@echo "Usage: make -C kernel PROFILE=minimal|host|qemu [LINUX_SRC=/path/to/linux-src] [kernel|modules|all]"
