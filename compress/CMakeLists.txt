# compress — 压缩协议插件（zlib + zstd）
find_library(HWRUN_ZSTD_LIB zstd REQUIRED)
find_library(HWRUN_Z_LIB z REQUIRED)

add_library(compress SHARED
  src/compress_core.c
  src/compress_impl.c)

target_link_libraries(compress PRIVATE
  hwrun_includes
  ${HWRUN_ZSTD_LIB}
  ${HWRUN_Z_LIB})
target_include_directories(compress PRIVATE include src)
target_compile_options(compress PRIVATE -fvisibility=hidden)
set_target_properties(compress PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build)
