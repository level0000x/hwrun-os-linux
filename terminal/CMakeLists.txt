# terminal — TERMINAL 终端协议插件（PTY 会话管理，纯 POSIX，无外部库）
add_library(terminal SHARED
  src/terminal_impl.c)

target_link_libraries(terminal PRIVATE hwrun_includes)
target_include_directories(terminal PRIVATE include)
target_compile_options(terminal PRIVATE -fvisibility=hidden)
set_target_properties(terminal PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build)