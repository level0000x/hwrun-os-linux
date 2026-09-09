# console — 系统控制台协议插件（零内核依赖，无外部库）
add_library(console SHARED
  src/console_core.c
  src/console_impl.c)

target_link_libraries(console PRIVATE hwrun_includes)
target_include_directories(console PRIVATE include src)
target_compile_options(console PRIVATE -fvisibility=hidden)
set_target_properties(console PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build)