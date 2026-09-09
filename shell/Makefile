# shell/Makefile — SHELL 协议插件构建（双平台自测）
#
# 构建环境：
#   - Linux / WSL : 系统 gcc（默认）
#   - MSYS2 (选)  : 本插件仅使用 libc + POSIX（pipe/fork/exec），mingw 亦需 POSIX fork
#                   故主要面向 Linux 构建 shell.so（导出 hw_plugin_entry()）。
#
# 产出 build/shell.so（导出 hw_plugin_entry()），零外部库依赖。
#
# 用法：
#   make                    # 构建 build/shell.so
#   make test               # 编译 tests/test_shell.c（链接 hwrun-core 总线对象 + cmocka），
#                           # dfopen build/shell.so 后跑全部断言（要求 cmocka 已安装）
#   make exports            # 查看导出符号
#   make clean

BUILD := build
SRC   := src
INC   := include ../include
SO    := $(BUILD)/shell.so

CC       ?= gcc
CPPFLAGS := $(addprefix -I,$(INC))
CFLAGS   := -O2 -Wall -Wextra -fPIC -fvisibility=hidden -std=gnu11
LDFLAGS  := -shared -fPIC

OBJS := $(BUILD)/shell_impl.o

# ---- 本地自测（test_shell 链路：dlopen .so + metaproto 真调 ops） ----
# 复用 bus/src 总线对象提供 hw_bus_init/metaproto_* 符号；需要 cmocka。
CMOCKA_CFLAGS := $(shell pkg-config --cflags cmocka 2>/dev/null)
CMOCKA_LIBS   := $(shell pkg-config --libs cmocka 2>/dev/null)
BUS_SRCS := ../bus/src/hwrun.c ../bus/src/metaproto.c ../bus/src/param.c \
            ../bus/src/log.c ../bus/src/hwlock.c ../bus/src/yml.c \
            ../bus/src/runtime.c ../bus/src/loader.c ../bus/src/kctl.c ../bus/src/bus.c
BUS_OBJS := $(patsubst %.c,$(BUILD)/bus/%.o,$(notdir $(BUS_SRCS)))
TEST_BIN := $(BUILD)/test_shell
CPPFLAGS_TEST := -I../include -I../bus/src -I../git/include $(CMOCKA_CFLAGS)
# 自测在这种 cwd=项目根 下运行，so 相对路径用 "./shell/..."；宏置空即相对项目根。
CPPFLAGS_TEST += -DHWRUN_PLUGIN_ROOT=\"\"

.PHONY: all clean test exports platform

all: dirs $(SO)

platform:
	@echo "CC=$(CC)"

dirs:
	@mkdir -p $(BUILD)/bus

$(SO): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -o $@
	@echo "=== built $(SO) (linux) ==="

$(BUILD)/%.o: $(SRC)/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# ---- bus 对象（供 test_shell 链接，独立目录避免与插件对象命名冲突） ----
# 测试侧对象用普通编译标志（无需 -fPIC / visibility）
CTEST_CFLAGS := -std=gnu11 -O2 -Wall -Wextra
$(BUILD)/bus/%.o: ../bus/src/%.c
	$(CC) $(CPPFLAGS_TEST) $(CTEST_CFLAGS) -c $< -o $@

$(BUILD)/test_shell.o: ../tests/test_shell.c
	$(CC) $(CPPFLAGS_TEST) $(CTEST_CFLAGS) -c $< -o $@

$(TEST_BIN): $(BUILD)/test_shell.o $(BUS_OBJS)
	$(CC) -rdynamic $^ $(CMOCKA_LIBS) -ldl -lpthread -o $@

# 本地自测：dlopen build/shell.so + metaproto 解析 SHELL -> 真跑全部断言
test: all $(TEST_BIN)
	@echo "=== shell selftest (dlopen + cmocka) ==="
	@cd .. && ./shell/$(TEST_BIN)

# 导出符号检查
exports: all
	@echo "--- exported hw_plugin_entry ---"
	@nm -D $(SO) | grep hw_plugin_entry

clean:
	rm -rf $(BUILD)