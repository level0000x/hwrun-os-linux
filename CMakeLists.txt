# HWRun OS — 顶层 CMake 构建
#
# 迁移自手写 Makefile（各插件目录自维护）的统一构建树。
# 平台：POSIX C（Linux 目标），在 WSL/MSYS2 下构建。
#
# 用法：
#   cmake -S . -B build                    # 配置
#   cmake --build build -j                  # 构建
#   ctest --test-dir build --output-on-failure   # 跑测试
#
# 常用开关：
#   -DHWRUN_ENABLE_SANITIZER=address|undefined|thread|all
#   -DHWRUN_WERROR=ON                       # -Werror 硬门禁
#   -DHWRUN_BUILD_TESTS=OFF                 # 关测试
#   -DHWRUN_ENABLE_COVERAGE=ON              # gcov 覆盖率

cmake_minimum_required(VERSION 3.16)
project(hwrun-os
        VERSION 7.0.0
        DESCRIPTION "HWRun OS — plugin/protocol/param/git OS container"
        LANGUAGES C)

option(HWRUN_BUILD_TESTS "Build tests and enable ctest" ON)
option(HWRUN_WERROR "Treat warnings as errors" OFF)
option(HWRUN_ENABLE_COVERAGE "Enable gcov coverage" OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)   # gnu11：保留既有 __attribute__ / 平台扩展用法

find_package(Threads REQUIRED)

# ---------------------------------------------------------------
# 编译告警统一收紧
# ---------------------------------------------------------------
add_library(hwrun_warnings INTERFACE)
target_compile_options(hwrun_warnings INTERFACE
  -Wall -Wextra -Wpedantic
  -Wshadow -Wformat=2 -Wno-format-nonliteral
  -Wundef -Wwrite-strings -Wstrict-prototypes
  $<$<COMPILE_LANGUAGE:C>:-Wmissing-prototypes>)
if(HWRUN_WERROR)
  target_compile_options(hwrun_warnings INTERFACE -Werror)
endif()

# ---------------------------------------------------------------
# 公共 include 目录（根 include + bus 内部头 + git 协议头）
# ---------------------------------------------------------------
add_library(hwrun_includes INTERFACE)
target_include_directories(hwrun_includes INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/bus/src
  ${CMAKE_CURRENT_SOURCE_DIR}/git/include)
target_link_libraries(hwrun_includes INTERFACE hwrun_warnings Threads::Threads)

# ---------------------------------------------------------------
# 公共标志
# ---------------------------------------------------------------
add_compile_options(-fPIC)

# 插件 .so 产物不带 lib 前缀（与 plugin.yml/files 的 build/<name>.so 约定一致）
set(CMAKE_SHARED_LIBRARY_PREFIX "")

# ---------------------------------------------------------------
# sanitizer
# ---------------------------------------------------------------
set(HWRUN_ENABLE_SANITIZER "" CACHE STRING
    "address|undefined|thread|all （多个用分号）")
if(HWRUN_ENABLE_SANITIZER)
  set(_san_flags "")
  foreach(_s IN LISTS HWRUN_ENABLE_SANITIZER)
    if(_s STREQUAL "all")
      set(_san_flags "${_san_flags} -fsanitize=address -fsanitize=undefined -fsanitize=thread")
    else()
      set(_san_flags "${_san_flags} -fsanitize=${_s}")
    endif()
  endforeach()
  add_compile_options(${_san_flags})
  add_link_options(${_san_flags})
  message(STATUS "HWRun sanitizer: ${_san_flags}")
endif()

# ---------------------------------------------------------------
# 目标组织：bus 静态库 + hwrun-bus 可执行 + 插件 + 测试
# ---------------------------------------------------------------
add_subdirectory(bus)
add_subdirectory(git)
add_subdirectory(hap)
add_subdirectory(pmp)
add_subdirectory(fsp)
add_subdirectory(np)
add_subdirectory(sp)
add_subdirectory(crypto)
add_subdirectory(loader)

if(HWRUN_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
else()
  # 仍暴露 ctest（空）
  enable_testing()
endif()

# kernel 委托目标（kbuild 集成最自然，保持原 Makefile）
add_custom_target(hwrun-kernel
  COMMAND ${CMAKE_MAKE_PROGRAM} -C ${CMAKE_CURRENT_SOURCE_DIR}/kernel
  COMMENT "Build kernel modules via kernel/Makefile (needs LINUX_SRC)")
