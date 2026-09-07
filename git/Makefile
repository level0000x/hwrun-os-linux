# ============================================================
# HWRun OS — GIT 插件构建
#
# 构建产物：build/git.so（可被 dlopen 加载的共享库）
#
# 平台：POSIX C（Linux 目标）。在 MSYS2/局域环境交叉适配。
# 用法：
#   make            # 构建 git.so
#   make clean
# ============================================================

CC       ?= gcc
CFLAGS   ?= -O2 -Wall -Wextra -fPIC -std=gnu11
CPPFLAGS += -I../include -Iinclude -Isrc
LDFLAGS  += -shared

SRCS := src/git.c      \
        src/executor.c \
        src/storage.c  \
        src/version.c  \
        src/history.c  \
        src/branch.c   \
        src/compact.c  \
        src/audit.c

OBJS := $(SRCS:.c=.o)

BUILD_DIR := build
TARGET    := $(BUILD_DIR)/git.so

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

install: all
	install -D -m 755 $(TARGET) $(PREFIX)/git.so

.PHONY: all clean install