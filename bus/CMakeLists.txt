# bus — 总线核心：静态库 hwrun-core + 可执行 hwrun-bus

set(HWRUN_CORE_SRCS
  src/hwrun.c
  src/metaproto.c
  src/param.c
  src/log.c
  src/yml.c
  src/runtime.c
  src/loader.c
  src/kctl.c
  src/bus.c
  src/hwlock.c)

# 静态库：全部可复用逻辑（不含 main）
add_library(hwrun-core STATIC ${HWRUN_CORE_SRCS})
target_link_libraries(hwrun-core PUBLIC hwrun_includes)
target_link_libraries(hwrun-core PRIVATE ${CMAKE_DL_LIBS})

# 可执行入口
add_executable(hwrun-bus src/main.c)
target_link_libraries(hwrun-bus PRIVATE hwrun-core)
# 导出主程序符号：loader 用 RTLD_GLOBAL 加载插件 .so，插件的
# hw_str_*/runtime 回调等需从宿主动态符号表解析
set_target_properties(hwrun-bus PROPERTIES ENABLE_EXPORTS ON)

if(HWRUN_ENABLE_COVERAGE)
  # gcov 覆盖率：hwrun-core 静态库编译插桩；链接方需链 --coverage 拉 libgcov
  # （hwrun-bus 及测试可执行，后者在 tests/CMakeLists.txt 处理）
  target_compile_options(hwrun-core PRIVATE --coverage)
  target_link_options(hwrun-core PRIVATE --coverage)
  target_link_options(hwrun-bus PRIVATE --coverage)
endif()

# 安装
install(TARGETS hwrun-bus RUNTIME DESTINATION bin)
install(TARGETS hwrun-core ARCHIVE DESTINATION lib/hwrun)
