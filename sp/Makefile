# HWRun OS — SP 插件构建
#
# 构建目标：sp/build/sp.so（可被总线 dlopen 加载的插件）
# 平台：POSIX C，MSYS2/本地环境 gcc。
# 安全原语：本环境无 libcrypto 开发库，加解密/哈希/签名/密钥/随机数
#           封装本机 `openssl` 可执行文件（真实可用）。

CC       ?= gcc
CFLAGS   ?= -O2 -Wall -Wextra -fPIC -std=gnu11
CPPFLAGS += -Iinclude -I../include
LDFLAGS  +=
LDLIBS   += -ldl -lpthread

BUILD := build

SRCS := src/sp.c \
        src/sp_util.c \
        src/sp_hash.c \
        src/sp_crypto.c \
        src/sp_key.c \
        src/sp_auth.c

OBJS := $(SRCS:src/%.c=$(BUILD)/%.o)

all: $(BUILD)/sp.so

$(BUILD)/sp.so: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -shared -o $@ $^ $(LDLIBS)

$(BUILD)/%.o: src/%.c include/sp.h src/sp_internal.h ../include/hwrun.h | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

# dlopen 自测程序（可选）
tests/sp_selftest: tests/sp_selftest.c $(BUILD)/sp.so
	$(CC) $(CFLAGS) $(CPPFLAGS) -I. -o $@ tests/sp_selftest.c -ldl

selftest: tests/sp_selftest
	./tests/sp_selftest

install:
	install -D -m 644 $(BUILD)/sp.so $(DESTDIR)$(PREFIX)/sp.so

clean:
	rm -rf $(BUILD) tests/sp_selftest

.PHONY: all install clean selftest