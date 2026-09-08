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
# 导出符号给 dlopen 的插件 .so 解析（与 hwrun-bus 一致）
set_target_properties(test_proto PROPERTIES ENABLE_EXPORTS ON)
# 插件 .so 根目录宏：隔离构建(sanitizer/coverage)时指向构建树 plugins/，
# 否则用源码内默认 "../"（不定义此宏）
if(HWRUN_PLUGIN_ROOT)
  target_compile_definitions(test_proto PRIVATE
    HWRUN_PLUGIN_ROOT="${HWRUN_PLUGIN_ROOT}")
endif()
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

# ----------------------------------------------------------------
# dlopen 型协议集成单测：加载插件 .so 后经 METAPROTO 真调 ops。
# 与 test_proto 同构（需 ENABLE_EXPORTS 供插件解析宿主符号；
# add_dependencies 保证 .so 先构建；dlopen 用相对路径故 WORKING_DIRECTORY=tests）。
# ----------------------------------------------------------------
set(_dlopen_tests test_hap test_pmp test_git test_crypto)
foreach(_t IN LISTS _dlopen_tests)
  add_executable(${_t} ${_t}.c)
  target_link_libraries(${_t} PRIVATE
    hwrun-core
    hwrun_includes
    PkgConfig::CMOCKA)
  set_target_properties(${_t} PROPERTIES ENABLE_EXPORTS ON)
  add_dependencies(${_t} hap pmp git crypto)
  if(HWRUN_PLUGIN_ROOT)
    target_compile_definitions(${_t} PRIVATE
      HWRUN_PLUGIN_ROOT="${HWRUN_PLUGIN_ROOT}")
  endif()
  add_test(NAME ${_t} COMMAND ${_t})
  set_tests_properties(${_t} PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endforeach()

# ----------------------------------------------------------------
# gcov 覆盖率：test_proto、CMocka 单测与 dlopen 集成测试统一加 --coverage
# （hwrun-core 的 --coverage 已在 bus/CMakeLists.txt 由同一 option 控制；
#   插件 .so 插桩在顶层 CMakeLists.txt 完成，dlopen 测试需 --coverage 拉 libgcov）
# ----------------------------------------------------------------
if(HWRUN_ENABLE_COVERAGE)
  target_compile_options(test_proto PRIVATE --coverage)
  target_link_options(test_proto PRIVATE --coverage)
  foreach(_t IN LISTS _cmocka_unit_tests _dlopen_tests)
    target_compile_options(${_t} PRIVATE --coverage)
    target_link_options(${_t} PRIVATE --coverage)
  endforeach()

  # --------------------------------------------------------------
  # coverage-report：清计数 -> 全量 ctest -> lcov 捕获 -> genhtml
  # 产物：build/coverage.info 与 build/coverage/index.html（需 lcov）。
  # --------------------------------------------------------------
  find_program(LCOV lcov)
  find_program(GENHTML genhtml)
  if(LCOV AND GENHTML)
    # 依赖全部被测目标，保证单命令自洽：
    #   cmake --build <build> --target coverage-report
    add_custom_target(coverage-report
      COMMAND find ${CMAKE_BINARY_DIR} -name '*.gcda' -delete
      COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR}
      COMMAND ${LCOV} --capture --directory ${CMAKE_BINARY_DIR}
              --exclude '*/usr/include/*'
              --exclude '${CMAKE_CURRENT_SOURCE_DIR}/*'
              --output-file ${CMAKE_BINARY_DIR}/coverage.info
      COMMAND ${GENHTML} ${CMAKE_BINARY_DIR}/coverage.info
              --output-directory ${CMAKE_BINARY_DIR}/coverage
      COMMAND ${CMAKE_COMMAND} -E echo
              "Coverage report: ${CMAKE_BINARY_DIR}/coverage/index.html"
      COMMENT "Collect coverage (lcov + genhtml)")
    add_dependencies(coverage-report
      test_proto ${_cmocka_unit_tests} ${_dlopen_tests}
      git hap pmp fsp np sp crypto loader hwrun-bus)
  else()
    message(WARNING "lcov/genhtml not found; coverage-report target skipped")
  endif()
endif()
