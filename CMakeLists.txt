cmake_minimum_required(VERSION 3.20)
project(jxltest)

set(CMAKE_CXX_STANDARD 20)

# Build libjxl statically without tests or tools
set(JPEGXL_ENABLE_TOOLS OFF CACHE BOOL "Disable libjxl tools")
set(JPEGXL_ENABLE_BENCHMARK OFF CACHE BOOL "Disable libjxl benchmark")
set(JPEGXL_ENABLE_EXAMPLES OFF CACHE BOOL "Disable libjxl examples")
set(JPEGXL_ENABLE_SJPEG OFF CACHE BOOL "Disable sjpeg")
set(JPEGXL_ENABLE_PLUGINS OFF CACHE BOOL "Disable plugins")
set(BUILD_TESTING OFF CACHE BOOL "Disable tests")

add_subdirectory(third_party/libjxl EXCLUDE_FROM_ALL)

add_executable(jxltest main.cpp)
target_link_libraries(jxltest PRIVATE jxl jxl_threads)
