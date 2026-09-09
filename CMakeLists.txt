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
  # 以 CMake 列表累积（add_compile_options 只按分号切分；若拼成含空格的
  # 单个字符串，多 sanitizer 会被当作一个参数传给编译器导致失败）
  unset(_san_flags)
  foreach(_s IN LISTS HWRUN_ENABLE_SANITIZER)
    if(_s STREQUAL "all")
      list(APPEND _san_flags -fsanitize=address -fsanitize=undefined -fsanitize=thread)
    else()
      list(APPEND _san_flags "-fsanitize=${_s}")
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
add_subdirectory(compress)
add_subdirectory(audit)
add_subdirectory(permission)
add_subdirectory(storage)

# ---------------------------------------------------------------
# 插件产物目录隔离
#
# 默认：插件 .so 落源树 <p>/build/（与 plugin.yml files 相对路径、
# hwrun-bus 运行时扫描、test_proto 相对 dlopen 约定一致）。
#
# sanitizer / coverage 开启时：产物改指构建树 plugins/<p>/build/，
# 避免与普通构建共用源树产物目录互相污染（ASan 版 .so 覆盖正常版）。
# 此时 test_proto 经 HWRUN_PLUGIN_ROOT 宏指到构建树 plugins/。
# ---------------------------------------------------------------
if(HWRUN_ENABLE_SANITIZER OR HWRUN_ENABLE_COVERAGE)
  set(_hw_iso_root "${CMAKE_BINARY_DIR}/plugins")
  foreach(_p git hap pmp fsp np sp crypto loader compress audit permission storage)
    set_target_properties(${_p} PROPERTIES
      LIBRARY_OUTPUT_DIRECTORY "${_hw_iso_root}/${_p}/build")
  endforeach()
  set(HWRUN_PLUGIN_ROOT "${_hw_iso_root}/")
  message(STATUS "HWRun isolated plugin output: ${_hw_iso_root}")
else()
  set(HWRUN_PLUGIN_ROOT "")   # 非隔离：test_proto 用默认 "../" 前缀
endif()

# ---------------------------------------------------------------
# 覆盖率：插件 .so 一并插桩（源码覆盖不只限 bus/hwrun-core）。
# dlopen 的插件与宿主同进程，--coverage 链接已含 libgcov；捕获聚合见
# tests/CMakeLists.txt 的 coverage-report 目标。
# ---------------------------------------------------------------
if(HWRUN_ENABLE_COVERAGE)
  foreach(_p git hap pmp fsp np sp crypto loader compress audit permission storage)
    target_compile_options(${_p} PRIVATE --coverage)
    target_link_options(${_p} PRIVATE --coverage)
  endforeach()
endif()

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
