# crypto/Makefile — CRYPTO 协议插件构建
#
# 构建环境：双平台支持
#   - MSYS2 / Windows : mingw64 工具链 + mingw64 libcrypto (OpenSSL EVP)
#   - Linux / WSL     : 系统 gcc + 系统 libcrypto (OpenSSL)
# 通过 PLATFORM 覆盖强制指定平台。产出 build/crypto.so（导出 hw_plugin_entry()）
#
# 用法：
#   make                    # 自动检测平台
#   make PLATFORM=linux     # 强制 Linux
#   make PLATFORM=mingw     # 强制 MSYS2/mingw64
#   make selftest           # dlopen 自测

BUILD := build
SRC   := src
INC   := include ../include
SO    := $(BUILD)/crypto.so

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
  # --- mingw64 工具链与 libcrypto (MSYS2) ---
  MINGW   := /mingw64
  CC      := $(MINGW)/bin/gcc
  OBJCC   := -c
  export PATH := $(MINGW)/bin:$(PATH)   # 子进程需 /mingw64/bin 的 DLL
  LIBCRYPTO_STATIC := $(MINGW)/lib/libcrypto.a
  CPPFLAGS := $(addprefix -I,$(INC)) -I$(MINGW)/include -DWIN32_LEAN_AND_MEAN
  CFLAGS   := -O2 -Wall -Wextra -fPIC -fvisibility=hidden -std=gnu11 -Wno-deprecated-declarations
  LDFLAGS  := -shared -fPIC -Wl,--enable-stdcall-fixup -Wl,--out-implib,$(BUILD)/libcrypto-plugin.a
  LDLIBS   := $(LIBCRYPTO_STATIC) -lws2_32 -lcrypt32 -luser32 -lgdi32 -static-libgcc
else
  # --- Linux / WSL：系统 gcc + 系统 OpenSSL ---
  CC       ?= gcc
  OBJCC    := -c
  CPPFLAGS := $(addprefix -I,$(INC))
  CFLAGS   := -O2 -Wall -Wextra -fPIC -fvisibility=hidden -std=gnu11 -Wno-deprecated-declarations
  LDFLAGS  := -shared -fPIC
  LDLIBS   := -lcrypto
endif

OBJS := \
	$(BUILD)/crypto_core.o \
	$(BUILD)/crypto_impl.o

.PHONY: all clean selftest exports platform

all: dirs $(SO)

platform:
	@echo "PLATFORM=$(PLATFORM) CC=$(CC)"

dirs:
	@mkdir -p $(BUILD)

# hw_plugin_entry 已以 __declspec(dllexport) / visibility(default) 标记，故可导出。
$(SO): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) $(LDLIBS) -o $@
	@echo "=== built $(SO) (platform: $(PLATFORM)) ==="

$(BUILD)/%.o: $(SRC)/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(OBJCC) $< -o $@

# dlopen 自测：验证加密往返 + 哈希参考向量
selftest: all
	$(CC) $(CFLAGS:-fPIC=) $(addprefix -I,$(INC)) -o $(BUILD)/selftest $(BUILD)/selftest.c
	@cd $(BUILD) && ./selftest

# 导出符号检查
exports:
	@echo "--- exported hw_plugin_entry ---"
	-objdump -p $(SO) | grep hw_plugin_entry | tail -1

clean:
	rm -rf $(BUILD)/*.o $(BUILD)/*.a $(BUILD)/*.so $(BUILD)/*.exe $(BUILD)/*.log $(BUILD)/selftest