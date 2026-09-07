# PMP（进程管理协议）插件构建
#
# 产出：pmp/build/pmp.so（导出 hw_plugin_entry()）
# 在 pmp/ 目录下执行 make：  make
#
# 平台：POSIX C（Linux 目标），MSYS2 环境交叉适配。
# 用法（参考顶层 README / 编译命令）：
#   make            # 构建 build/pmp.so
#   make clean

CURDIR ?= .

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -fPIC -std=gnu11
CPPFLAGS += -I$(CURDIR)/../include -I$(CURDIR)/../bus/src -I$(CURDIR)/include
LDLIBS  +=

SRCS  := src/pmp.c src/pmp_list.c src/pmp_proc.c src/pmp_task.c
OBJS  := $(SRCS:.c=.o)

OUT := build/pmp.so

all: $(OUT)

$(OUT): $(OBJS) | build
	$(CC) $(CFLAGS) $(LDFLAGS) -shared -o $@ $^ $(LDLIBS)

build:
	mkdir -p build

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

install: all
	install -D -m 644 $(OUT) /usr/lib/hwrun/plugins/pmp.so

clean:
	rm -rf build $(OBJS)

.PHONY: all install clean