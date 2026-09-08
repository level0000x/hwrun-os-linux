# sp — 安全协议插件
set(SRCS
  src/sp.c src/sp_util.c src/sp_hash.c
  src/sp_crypto.c src/sp_key.c src/sp_auth.c)

add_library(sp SHARED ${SRCS})
target_link_libraries(sp PRIVATE hwrun_includes)
target_include_directories(sp PRIVATE include src)
set_target_properties(sp PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build)
