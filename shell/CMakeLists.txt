# shell — 命令行解释器协议插件（无外部库，用户态 fork/exec + pipe）
add_library(shell SHARED
  src/shell_impl.c)

target_link_libraries(shell PRIVATE hwrun_includes)
target_include_directories(shell PRIVATE include src)
target_compile_options(shell PRIVATE -fvisibility=hidden)
set_target_properties(shell PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build)