# pmp — 进程管理协议插件
set(SRCS
  src/pmp.c src/pmp_list.c src/pmp_proc.c src/pmp_task.c)

add_library(pmp SHARED ${SRCS})
target_link_libraries(pmp PRIVATE hwrun_includes)
target_include_directories(pmp PRIVATE include)
set_target_properties(pmp PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build)
