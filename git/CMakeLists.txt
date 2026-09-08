# git — GIT 协议插件
set(SRCS
  src/git.c src/executor.c src/storage.c src/version.c
  src/history.c src/branch.c src/compact.c src/audit.c)

add_library(git SHARED ${SRCS})
target_link_libraries(git PRIVATE hwrun_includes)
target_include_directories(git PRIVATE include src)
set_target_properties(git PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build)
