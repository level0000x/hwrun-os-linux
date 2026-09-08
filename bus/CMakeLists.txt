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

# 安装
install(TARGETS hwrun-bus RUNTIME DESTINATION bin)
install(TARGETS hwrun-core ARCHIVE DESTINATION lib/hwrun)
