# crypto — 密码学原语插件（链接 OpenSSL）
find_package(OpenSSL REQUIRED)

add_library(crypto SHARED
  src/crypto_core.c
  src/crypto_impl.c)

target_link_libraries(crypto PRIVATE hwrun_includes OpenSSL::Crypto)
target_include_directories(crypto PRIVATE include)
target_compile_options(crypto PRIVATE -fvisibility=hidden -Wno-deprecated-declarations)
set_target_properties(crypto PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build)
