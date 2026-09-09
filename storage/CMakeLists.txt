# storage — 存储后端协议插件（用户态目录卷管理，零第三方依赖）
# 实现：真实文件系统操作（mkdir/lstat/nftw/opendir/statvfs/rename），无占位数据。
add_library(storage SHARED
  src/storage_core.c
  src/storage_impl.c)

target_link_libraries(storage PRIVATE hwrun_includes)
target_include_directories(storage PRIVATE include src)
target_compile_options(storage PRIVATE -fvisibility=hidden)
set_target_properties(storage PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build)
