# tests — 测试
# 说明：
#   1) test_proto：真协议集成测试（链接 hwrun-core 静态库 + dlopen 各插件 .so）。
#      插件 .so 由各插件 CMake target 产出，此处用完整路径引用它们（生成目录）。
#      该文件保持原样，属合格集成测试。
#   2) test_<subsystem>：CMocka 单元测试（错误码/参数树/协议注册表/yml 解析/锁并发）。
#      每个文件一个 main(run_tests)，链接 hwrun-core 静态库 + PkgConfig::CMOCKA。

# CMocka（pkg-config 探测；Linux 下头在 /usr/include/cmocka.h）
include(FindPkgConfig)
pkg_check_modules(CMOCKA REQUIRED IMPORTED_TARGET cmocka)

# 每个插件 .so 的 CMake 产物绝对路径（LIBRARY_OUTPUT_DIRECTORY 已设到源树 build/）
set(_plugin_dirs
  ${CMAKE_CURRENT_SOURCE_DIR}/../git
  ${CMAKE_CURRENT_SOURCE_DIR}/../hap
  ${CMAKE_CURRENT_SOURCE_DIR}/../pmp
  ${CMAKE_CURRENT_SOURCE_DIR}/../fsp
  ${CMAKE_CURRENT_SOURCE_DIR}/../np
  ${CMAKE_CURRENT_SOURCE_DIR}/../sp
  ${CMAKE_CURRENT_SOURCE_DIR}/../crypto
  ${CMAKE_CURRENT_SOURCE_DIR}/../loader)

add_executable(test_proto test_proto.c)
target_link_libraries(test_proto PRIVATE hwrun-core hwrun_includes)
target_include_directories(test_proto PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../hap/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../pmp/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../fsp/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../np/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../loader/include)

# 运行期依赖：8 个 .so 需先构建（dlopen 路径指向插件 build/）
add_dependencies(test_proto git hap pmp fsp np sp crypto loader)

# 供 test_proto 在源码里展开：通过宏把目录传给运行环境
# 注意：dlopen 路径在 test_proto.c 中为相对路径(../x/build/x.so)，
# 故 WORKING_DIRECTORY 必须设为 tests 源码目录（与旧 make test 一致）。
add_test(NAME test_proto COMMAND test_proto)
set_tests_properties(test_proto PROPERTIES
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

# ----------------------------------------------------------------
# CMocka 单元测试可执行：每子系统一个，均在 tests 源目录下工作
# ----------------------------------------------------------------
set(_cmocka_unit_tests
  test_errno        # 错误码宏语义 + hw_str_eq/hw_fmt_path 基础工具
  test_param        # 参数树全套：init/get/set/watch/save/load/shutdown
  test_metaproto    # 协议注册表：register/resolve/unregister/subscribe/check_deps
  test_yml          # plugin.yml 解析（plugin 嵌套根 + 双形态列表）
  test_lock)        # hwlock 并发正确性（注册表 + 参数树多线程锤测）

foreach(_t IN LISTS _cmocka_unit_tests)
  add_executable(${_t} ${_t}.c)
  target_link_libraries(${_t} PRIVATE
    hwrun-core
    hwrun_includes
    PkgConfig::CMOCKA)
  add_test(NAME ${_t} COMMAND ${_t})
  set_tests_properties(${_t} PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endforeach()
