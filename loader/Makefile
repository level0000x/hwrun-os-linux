# HWRun OS — LOADER 插件构建
#
# 构建目标：loader/build/loader.so（可被总线 dlopen 加载的插件）
# 平台：POSIX C，MSYS2/本地环境可用 gcc 交叉适配。

CC       ?= gcc
CFLAGS   ?= -O2 -Wall -Wextra -fPIC -std=gnu11
CPPFLAGS += -Iinclude -I../include
LDFLAGS  +=
LDLIBS   += -ldl

BUILD := build

SRCS := src/loader.c \
        src/loader_detect.c \
        src/loader_elf.c \
        src/loader_run.c

OBJS := $(SRCS:src/%.c=$(BUILD)/%.o)

all: $(BUILD)/loader.so

$(BUILD)/loader.so: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -shared -o $@ $^ $(LDLIBS)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

install:
	install -D -m 644 $(BUILD)/loader.so $(DESTDIR)$(PREFIX)/loader.so

clean:
	rm -rf $(BUILD)

.PHONY: all install clean