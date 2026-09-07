# FSP 插件构建
#
# 产出：fsp/build/fsp.so（dlopen 加载的协议插件，导出 hw_plugin_entry()）
# 平台：POSIX C（MSYS2 / Linux）
# 用法：
#   make            # 构建 fsp.so
#   make clean

CC       ?= gcc
BASE     := ..
CFLAGS   += -O2 -Wall -Wextra -fPIC -std=gnu11
CPPFLAGS += -I$(BASE)/include -Iinclude

SRCS := src/fsp.c \
        src/fsp_file.c \
        src/fsp_dir.c \
        src/fsp_perm.c \
        src/fsp_path.c \
        src/fsp_mount.c

OBJS   := $(SRCS:.c=.o)
OUT    := build/fsp.so

all: $(OUT)

$(OUT): $(OBJS)
	@mkdir -p build
	$(CC) $(CFLAGS) -shared -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

clean:
	rm -rf build $(SRCS:.c=.o)

install: all
	install -D -m 755 $(OUT) /usr/lib/hwrun/plugins/fsp.so

.PHONY: all clean install