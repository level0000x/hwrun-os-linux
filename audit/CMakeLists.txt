# audit — 审计协议插件（零第三方依赖：仅 C 标准库 + pthread）
add_library(audit SHARED
  src/audit_core.c
  src/audit_impl.c)

target_link_libraries(audit PRIVATE
  hwrun_includes)
target_include_directories(audit PRIVATE include src)
target_compile_options(audit PRIVATE -fvisibility=hidden)
set_target_properties(audit PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build)
