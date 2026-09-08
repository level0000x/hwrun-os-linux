# HWRun OS — 真协议调用自测
#
# 覆盖依赖链各协议的真实 ops 调用。依赖根 Makefile 已构建的插件 .so。

CC       ?= gcc
CFLAGS   ?= -O2 -Wall -Wextra -std=gnu11
CPPFLAGS += -I../include -I../bus/src \
            -I../hap/include -I../pmp/include -I../fsp/include \
            -I../np/include -I../loader/include -I../git/include
LDFLAGS  +=
LDLIBS   += -ldl -lpthread

BUS_OBJS := ../bus/src/hwrun.o \
            ../bus/src/metaproto.o \
            ../bus/src/param.o \
            ../bus/src/log.o \
            ../bus/src/hwlock.o \
            ../bus/src/yml.o \
            ../bus/src/runtime.o \
            ../bus/src/loader.o \
            ../bus/src/kctl.o \
            ../bus/src/bus.o

all: test_proto

test_proto: test_proto.o $(BUS_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

run: all
	@echo "=== 真协议调用自测 ==="
	@./test_proto

clean:
	rm -f test_proto *.o

.PHONY: all run clean