# hap — 硬件抽象协议插件
set(SRCS
  src/hap.c src/hap_cpu.c src/hap_memory.c src/hap_disk.c
  src/hap_net.c src/hap_system.c)

add_library(hap SHARED ${SRCS})
target_link_libraries(hap PRIVATE hwrun_includes)
target_include_directories(hap PRIVATE include src)
set_target_properties(hap PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build)
