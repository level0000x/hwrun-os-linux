# compress/Makefile — COMPRESS 协议插件构建（legacy 轨道）
#
# 产出 build/compress.so（导出 hw_plugin_entry()）。链接 zlib + libzstd。
# 用法：
#   make                    # Linux/WSL
#   make selftest           # dlopen 自测（占位：真测走 tests/test_compress.c）

BUILD := build
SRC   := src
INC   := include ../include
SO    := $(BUILD)/compress.so

CC       ?= gcc
CPPFLAGS := $(addprefix -I,$(INC))
CFLAGS   := -O2 -Wall -Wextra -fPIC -fvisibility=hidden -std=gnu11
LDFLAGS  := -shared -fPIC
LDLIBS   := -lzstd -lz

OBJS := \
	$(BUILD)/compress_core.o \
	$(BUILD)/compress_impl.o

.PHONY: all clean selftest exports platform

all: dirs $(SO)

platform:
	@echo "CC=$(CC)"

dirs:
	@mkdir -p $(BUILD)

$(SO): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) $(LDLIBS) -o $@
	@echo "=== built $(SO) (platform: linux) ==="

$(BUILD)/%.o: $(SRC)/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(OBJCC:-c=) -c $< -o $@

selftest: all
	@echo "selftest: 真测由 tests/test_compress.c (CMocka dlopen) 承担"
	@objdump -p $(SO) | grep hw_plugin_entry | tail -1

exports:
	@echo "--- exported hw_plugin_entry ---"
	-objdump -p $(SO) | grep hw_plugin_entry | tail -1

clean:
	rm -rf $(BUILD)/*.o $(BUILD)/*.so
