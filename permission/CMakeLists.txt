# permission — 权限管理协议插件（用户态 RBAC，零第三方依赖）
add_library(permission SHARED
  src/permission_core.c
  src/permission_impl.c)

target_link_libraries(permission PRIVATE
  hwrun_includes)
target_include_directories(permission PRIVATE include src)
target_compile_options(permission PRIVATE -fvisibility=hidden)
set_target_properties(permission PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build)
