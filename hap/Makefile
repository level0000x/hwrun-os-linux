# HWRun OS — HAP 插件构建
#
# 构建目标：hap/build/hap.so（可被总线 dlopen 加载的插件）
# 平台：POSIX C，MSYS2/本地环境可用 gcc 交叉适配。

CC       ?= gcc
CFLAGS   ?= -O2 -Wall -Wextra -fPIC -std=gnu11
CPPFLAGS += -Iinclude -I../include
LDFLAGS  +=
LDLIBS   += -ldl

BUILD := build

SRCS := src/hap.c \
        src/hap_cpu.c \
        src/hap_memory.c \
        src/hap_disk.c \
        src/hap_net.c \
        src/hap_system.c

OBJS := $(SRCS:src/%.c=$(BUILD)/%.o)

all: $(BUILD)/hap.so

$(BUILD)/hap.so: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -shared -o $@ $^ $(LDLIBS)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

install:
	install -D -m 644 $(BUILD)/hap.so $(DESTDIR)$(PREFIX)/hap.so

clean:
	rm -rf $(BUILD)

.PHONY: all install clean