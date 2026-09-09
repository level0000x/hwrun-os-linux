# HWRun OS — 顶层构建
#
# 目标：可编译的核心 + 各插件 .so + hwrun-bus 可执行文件。
#
# 平台：POSIX C（Linux 目标）。可在 MSYS2/局域环境交叉适配。
# 用法：
#   make            # 全部构建
#   make bus        # 仅总线
#   make plugins    # 仅插件
#   make test       # 跑自测
#   make clean

CC     ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -fPIC -std=gnu11
CPPFLAGS += -Iinclude -Ibus/src -Igit/include
LDFLAGS +=
LDLIBS  += -ldl -lpthread

PREFIX   ?= /usr/lib/hwrun/plugins
BINDIR   ?= /usr/bin
STATEDIR ?= /var/lib/hwrun

# ---------- 总线 ----------
BUS_SRCS := bus/src/hwrun.c \
            bus/src/metaproto.c \
            bus/src/param.c \
            bus/src/log.c \
            bus/src/hwlock.c \
            bus/src/yml.c \
            bus/src/runtime.c \
            bus/src/loader.c \
            bus/src/kctl.c \
            bus/src/bus.c \
            bus/src/main.c
BUS_OBJS := $(BUS_SRCS:.c=.o)
BUS_BIN  := hwrun-bus

# ---------- 插件目录（各子代理出品）----------
PLUGIN_DIRS := git hap pmp fsp np sp crypto loader compress
PLUGIN_SOS  := $(foreach d,$(PLUGIN_DIRS),$(d)/build/$(lastword $(subst /, ,$(d))).so)

all: bus plugins

# ---------- 微内核（独立构建） ----------
kernel:
	$(MAKE) -C kernel

# ---------- 总线 ----------
$(BUS_BIN): $(BUS_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

bus: $(BUS_BIN)

# ---------- 插件 ----------
plugins:
	@set -e; for d in $(PLUGIN_DIRS); do $(MAKE) -C $$d; done

$(PLUGIN_DIRS):
	$(MAKE) -C $@

# ---------- 测试 ----------
test: bus
	$(MAKE) -C tests run

# ---------- 清理 ----------
clean:
	rm -f $(BUS_OBJS) $(BUS_BIN)
	$(foreach d,$(PLUGIN_DIRS),rm -rf $(d)/build;)
	rm -rf build

install: all
	install -D -m 755 $(BUS_BIN) $(BINDIR)/hwrun
	$(foreach d,$(PLUGIN_DIRS),$(MAKE) -C $(d) install;)

.PHONY: all kernel bus plugins test clean install $(PLUGIN_DIRS)