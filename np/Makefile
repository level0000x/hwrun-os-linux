# HWRun OS — NP 插件构建
#
# 构建目标：np/build/np.so（可被总线 dlopen 加载的插件）
# 平台：POSIX C，MSYS2/本地环境可用 gcc 交叉适配。

CC       ?= gcc
CFLAGS   ?= -O2 -Wall -Wextra -fPIC -std=gnu11
CPPFLAGS += -Iinclude -I../include
LDFLAGS  +=
LDLIBS   += -ldl -lpthread

BUILD := build

SRCS := src/np.c \
        src/np_socket.c \
        src/np_resolve.c \
        src/np_interface.c \
        src/np_conn.c

OBJS := $(SRCS:src/%.c=$(BUILD)/%.o)

all: $(BUILD)/np.so

$(BUILD)/np.so: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -shared -o $@ $^ $(LDLIBS)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

install:
	install -D -m 644 $(BUILD)/np.so $(DESTDIR)$(PREFIX)/np.so

clean:
	rm -rf $(BUILD)

.PHONY: all install clean