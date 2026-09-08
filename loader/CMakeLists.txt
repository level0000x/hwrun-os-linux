# loader — 格式加载器插件
set(SRCS
  src/loader.c src/loader_detect.c src/loader_elf.c src/loader_run.c)

add_library(loader SHARED ${SRCS})
target_link_libraries(loader PRIVATE hwrun_includes)
target_include_directories(loader PRIVATE include)
set_target_properties(loader PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build)
