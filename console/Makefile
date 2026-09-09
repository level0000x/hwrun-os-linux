# console/Makefile — CONSOLE 协议插件构建（双平台自测）
#
# 构建环境（仿 crypto/Makefile）：
#   - MSYS2 / Windows : mingw64 工具链
#   - Linux / WSL     : 系统 gcc + 系统 cmocka（selftest 真跑）
# 产出 build/console.so（导出 hw_plugin_entry()），无外部库依赖。
# 用法：
#   make                        # 自动检测平台
#   make PLATFORM=linux         # 强制 Linux
#   make selftest               # 编出 .so 后用 cmocka 编 tests/test_console.c 并 dlopen 真跑（linux）

BUILD := build
SRC   := src
INC   := include ../include
SO    := $(BUILD)/console.so

# --- 平台检测 ---
UNAME_S := $(shell uname -s 2>/dev/null || echo MSYS)
ifeq ($(PLATFORM),)
  ifneq ($(findstring MINGW,$(UNAME_S)),)
    PLATFORM := mingw
  else ifneq ($(findstring MSYS,$(UNAME_S)),)
    PLATFORM := mingw
  else
    PLATFORM := linux
  endif
endif

ifeq ($(PLATFORM),mingw)
  # --- mingw64 工具链 ---
  CC      := /mingw64/bin/gcc
  CFLAGS  := -O2 -Wall -Wextra -fPIC -fvisibility=hidden -std=gnu11
  LDFLAGS := -shared -fPIC -Wl,--out-implib,$(BUILD)/libconsole-plugin.a
else
  # --- Linux / WSL：系统 gcc ---
  CC      ?= gcc
  CFLAGS  := -O2 -Wall -Wextra -fPIC -fvisibility=hidden -std=gnu11
  LDFLAGS := -shared -fPIC
endif

CPPFLAGS := $(addprefix -I,$(INC))

OBJS := \
	$(BUILD)/console_core.o \
	$(BUILD)/console_impl.o

# --- 本地自测目标（仅 linux：依赖系统 cmocka）---
TEST_SRC      := ../tests/test_console.c
TEST_BUS_SRCS := \
	../bus/src/hwrun.c \
	../bus/src/metaproto.c \
	../bus/src/param.c \
	../bus/src/log.c \
	../bus/src/hwlock.c \
	../bus/src/yml.c \
	../bus/src/runtime.c \
	../bus/src/loader.c \
	../bus/src/kctl.c \
	../bus/src/bus.c
TEST_CPPFLAGS := -I../tests -I../include -I../bus/src -I../git/include -I./include
TEST_LDLIBS   := -ldl -lpthread -lcmocka
TESTBIN       := $(BUILD)/test_console

.PHONY: all clean selftest exports platform

all: dirs $(SO)

platform:
	@echo "PLATFORM=$(PLATFORM) CC=$(CC)"

dirs:
	@mkdir -p $(BUILD)

$(SO): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -o $@
	@echo "=== built $(SO) (platform: $(PLATFORM)) ==="

$(BUILD)/%.o: $(SRC)/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# dlopen 自测：编出 .so 后，用 cmocka 编 tests/test_console.c 并 dlopen 真跑。
# 测试在 tests/ 目录下运行（dlopen 用相对路径 ../console/build/console.so）。
$(TESTBIN): $(TEST_SRC) $(TEST_BUS_SRCS) Makefile
	$(CC) -O2 -Wall -Wextra -std=gnu11 $(TEST_CPPFLAGS) -o $@ $(TEST_SRC) $(TEST_BUS_SRCS) $(TEST_LDLIBS)

selftest: all $(TESTBIN)
ifeq ($(PLATFORM),mingw)
	@echo "selftest: 仅支持 Linux/WSL（依赖系统 cmocka）"
else
	@echo "=== dlopen 自测：tests/test_console.c (CMocka) ==="
	@cd ../tests && $(abspath $(TESTBIN))
endif

exports:
	@echo "--- exported hw_plugin_entry ---"
	-objdump -p $(SO) | grep hw_plugin_entry | tail -1

clean:
	rm -rf $(BUILD)/*.o $(BUILD)/*.so $(BUILD)/*.a $(BUILD)/test_console