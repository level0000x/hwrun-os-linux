# crypto/Makefile — CRYPTO 协议插件构建
#
# 构建环境：MSYS2 mingw64 工具链 + mingw64 libcrypto (OpenSSL EVP)。
# 在 msys2 bash 中执行：  make
# 产出： build/crypto.so （导出 hw_plugin_entry()）
#
# 说明：msys2 默认 gcc 的 /usr/include 无 openssl 头文件，故显式使用
# mingw64 工具链及其自带的 libcrypto，保证真实加密功能且便于 dlopen 自测。

BUILD    := build
SRC      := src
INC      := include ../include
SO       := $(BUILD)/crypto.so

# --- mingw64 工具链与 libcrypto ---
MINGW      := /mingw64
CC         := $(MINGW)/bin/gcc
OBJCC      := -c

# gcc 会再派生 cc1/collect2 等子进程，这些子进程依赖 /mingw64/bin 下的
# DLL (libwinpthread-1.dll, libgcc_s_seh-1.dll)。msys bash 默认 PATH 不含
# /mingw64/bin，故此处导出以让子进程能加载运行时 DLL。
export PATH := $(MINGW)/bin:$(PATH)

# 优先链接静态 libcrypto（build/crypto.so 自动携带符号）；同时保留 ws2_32/crypt32 依赖
LIBCRYPTO_STATIC := $(MINGW)/lib/libcrypto.a

CPPFLAGS  := $(addprefix -I,$(INC)) -I$(MINGW)/include -DWIN32_LEAN_AND_MEAN
CFLAGS    := -O2 -Wall -Wextra -fPIC -fvisibility=hidden -std=gnu11 -Wno-deprecated-declarations
LDFLAGS   := -shared -fPIC -Wl,--enable-stdcall-fixup -Wl,--out-implib,$(BUILD)/libcrypto-plugin.a
LDLIBS    := $(LIBCRYPTO_STATIC) -lws2_32 -lcrypt32 -luser32 -lgdi32 -static-libgcc

OBJS := \
	$(BUILD)/crypto_core.o \
	$(BUILD)/crypto_impl.o

.PHONY: all clean selftest exports

all: dirs $(SO)

dirs:
	@mkdir -p $(BUILD)

# -fvisibility=hidden + 显式导出 hw_plugin_entry；
# hw_plugin_entry 已以 __declspec(dllexport) / visibility(default) 标记，故可导出。
$(SO): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) $(LDLIBS) -o $@
	@echo "=== built $(SO) ==="

$(BUILD)/%.o: $(SRC)/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(OBJCC) $< -o $@

# dlopen 自测：验证加密往返 + 哈希参考向量（不经 libcrypto 直接链接，走插件接口）
selftest: all
	$(CC) $(CFLAGS:-fPIC=) $(addprefix -I,$(INC)) -o $(BUILD)/selftest.exe $(BUILD)/selftest.c
	@cd $(BUILD) && ./selftest.exe

# 导出符号检查（objdump 导出表）
exports:
	@echo "--- exported hw_plugin_entry in export table ---"
	-objdump -p $(SO) | grep hw_plugin_entry | tail -1

clean:
	rm -rf $(BUILD)/*.o $(BUILD)/*.a $(BUILD)/*.so $(BUILD)/*.exe $(BUILD)/*.log