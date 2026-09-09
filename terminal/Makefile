# terminal/Makefile — TERMINAL 协议插件构建与本地自测
#
# 构建环境：Linux / WSL（主跑测环境）；纯 POSIX + libc，无外部库依赖。
#
#   make            # 产出 build/terminal.so
#   make selftest   # 编译 build/terminal.so + 用 cmocka 编译 tests/test_terminal.c
#                   # 并 dlopen 该 .so 真跑测试（在 tests 源码目录执行，以解析相对路径）
#   make clean
#
# 注意：本文件为 Makefile，勿用 clang-format 格式化（会破坏make语法）。
#
# 说明：tests/test_terminal.c 走 CMocka dlopen 链路，需链接 BUS 核心（hw_bus_init/
# hw_metaproto_*）。CMake 中该核心是静态库 hwrun-core；此处直接编译 bus 源码
# 进入测试可执行，避免依赖预先的 CMake 产物，保证独立可复现（等价 hwrun-core，
# 不含 main.c）。

BUILD := build
SO    := $(BUILD)/terminal.so
ROOT  := ..

INC   := include ../include

# --- 平台与工具链 ---
UNAME_S := $(shell uname -s 2>/dev/null || echo MSYS)
CC      ?= gcc
CFLAGS  := -O2 -Wall -Wextra -fPIC -fvisibility=hidden -std=gnu11
CPPFLAGS := $(addprefix -I,$(INC))
LDFLAGS := -shared -fPIC

# --- cmocka（pkg-config 探测，缺省回退 -lcmocka） ---
CMOCKA_CFLAGS := $(shell pkg-config --cflags cmocka 2>/dev/null)
CMOCKA_LIBS   := $(shell pkg-config --libs cmocka 2>/dev/null || echo -lcmocka)

# 测试对象与 BUS 核心源码（等价 tests/CMakeLists 链接的 hwrun-core）
TEST_SRC  := ../tests/test_terminal.c
BUS_SRCS  := ../bus/src/hwrun.c ../bus/src/metaproto.c ../bus/src/param.c \
             ../bus/src/log.c ../bus/src/yml.c ../bus/src/runtime.c \
             ../bus/src/loader.c ../bus/src/kctl.c ../bus/src/bus.c ../bus/src/hwlock.c
TEST_BIN  := $(BUILD)/test_terminal
TEST_INC  := $(CPPFLAGS) -I$(ROOT)/bus/src -I$(ROOT)/git/include

.PHONY: all clean selftest platform exports

all: dirs $(SO)

platform:
	@echo "platform=$(UNAME_S) CC=$(CC)"

dirs:
	@mkdir -p $(BUILD)

$(SO): src/terminal_impl.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $< -o $@
	@echo "=== built $(SO) ==="

# dlopen 自测：cmocka 编译 test_terminal.c（含 bus 源码），真跑 TERMINAL ops
selftest: all
	$(CC) $(TEST_INC) -std=gnu11 -O2 -g -Wall -D_GNU_SOURCE \
	  $(TEST_SRC) $(BUS_SRCS) \
	  $(CMOCKA_CFLAGS) $(CMOCKA_LIBS) -lpthread -ldl -o $(TEST_BIN)
	@echo "=== running $(TEST_BIN) (dlopen $(SO)) ==="
	cd $(ROOT)/tests && ../terminal/build/test_terminal

# 导出符号检查
exports:
	@echo "--- exported hw_plugin_entry ---"
	-objdump -p $(SO) | grep hw_plugin_entry | tail -1

clean:
	rm -rf $(BUILD)/*.o $(BUILD)/*.so $(BUILD)/test_terminal