cmake_minimum_required(VERSION 2.8.10 FATAL_ERROR)
project(at-scale-debug C)

find_package (PkgConfig REQUIRED)
set(ASD_DIR ${PROJECT_SOURCE_DIR})

# Enable debug symbols and disable optimization for Debug builds
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O0 -DDEBUG")
    message(STATUS "Debug build configuration enabled with symbols")
endif()

# Enable testing at root level when BUILD_UT is ON
# This allows CTest to discover tests from subdirectories
if(${BUILD_UT})
    enable_testing()
endif()

add_subdirectory(jtag_test)
add_subdirectory(server)
add_subdirectory(target)
add_subdirectory(i3c_dbg_test)
