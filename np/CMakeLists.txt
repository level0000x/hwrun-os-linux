# np — 网络协议插件
set(SRCS
  src/np.c src/np_socket.c src/np_resolve.c
  src/np_interface.c src/np_conn.c)

add_library(np SHARED ${SRCS})
target_link_libraries(np PRIVATE hwrun_includes)
target_include_directories(np PRIVATE include src)
set_target_properties(np PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build)
