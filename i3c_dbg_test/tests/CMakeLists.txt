project(at-scale-debug-i3c-debug-test-tests C)

#
# CMake options
cmake_minimum_required(VERSION 3.0)
include(FindPkgConfig)
#
# import cmocka
find_package(cmocka 1.1.0 REQUIRED)
pkg_check_modules(CMOCKA REQUIRED cmocka)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmocka)
include_directories (${ASD_DIR}/include)
include_directories (${ASD_DIR}/target)
pkg_check_modules (SAFEC REQUIRED libsafec)
include_directories (${SAFEC_INCLUDE_DIRS})
link_directories (${SAFEC_LIBRARY_DIRS})

#
# Enable Cmake Tests
enable_testing()

#
# Include code coverage
set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(CodeCoverage)

#
# Set options required for code coverage
set(CMAKE_C_FLAGS
        "${CMAKE_C_FLAGS} -g -O0 --coverage -fprofile-arcs -ftest-coverage")
set(CMAKE_EXE_LINKER "${CMAKE_EXE_LINKER}")
set(CMAKE_SHARED_LINKER "${CMAKE_SHARED_LINKER}")

#
# Treat warnings as errors
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror")

#
# Include local header files in project
include_directories(".")

#
# For Unit Test specific code
add_definitions(-DUNIT_TEST_MAIN)
#
# jtag_test tests
add_executable(i3c_dbg_test_tests
        "i3c_dbg_test_tests.c"
        ${ASD_DIR}/server/logging.c
        ../i3c_dbg_test.c
        i3c_dbg_mock.c
        i3c_dbg_mock.h)
set_property(TARGET i3c_dbg_test_tests PROPERTY C_STANDARD 99)
target_link_libraries(
        i3c_dbg_test_tests ${CMOCKA_LIBRARIES} pthread -fprofile-arcs -ftest-coverage -lm ${SAFEC_LIBRARIES})
add_test(i3c_dbg_test_tests i3c_dbg_test_tests)

#
# Coverage settings
set(COVERAGE_EXCLUDES '*/tests/*')

setup_target_for_coverage(NAME test_coverage_spp EXECUTABLE ctest)
