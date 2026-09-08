# fsp — 文件系统协议插件（排除测试 fsp_test.c）
set(SRCS
  src/fsp.c src/fsp_file.c src/fsp_dir.c
  src/fsp_perm.c src/fsp_path.c src/fsp_mount.c)

add_library(fsp SHARED ${SRCS})
target_link_libraries(fsp PRIVATE hwrun_includes)
target_include_directories(fsp PRIVATE include)
set_target_properties(fsp PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build)
