# Minimal hwinfo CMakeLists.txt stub
add_library(hwinfo INTERFACE)
target_include_directories(hwinfo INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
add_library(lfreist-hwinfo::hwinfo ALIAS hwinfo)
