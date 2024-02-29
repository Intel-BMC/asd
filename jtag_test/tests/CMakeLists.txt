project(at-scale-debug-jtag-test-tests C)

#
# CMake options
cmake_minimum_required(VERSION 3.0)
include(FindPkgConfig)
#
# import cmocka
find_package(cmocka 1.1.0 REQUIRED)
pkg_check_modules(CMOCKA REQUIRED cmocka)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmocka)

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
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror")

#
# Include local header files in project
include_directories(".")

#
# For Unit Test specific code
add_definitions(-DUNIT_TEST_MAIN)

#
# jtag_test tests
add_executable(jtag_test_tests
               "jtag_test_tests.c"
               ../jtag_test.c)
set_property(TARGET jtag_test_tests PROPERTY C_STANDARD 99)
target_link_libraries(
  jtag_test_tests ${CMOCKA_LIBRARIES} pthread -fprofile-arcs -ftest-coverage -lm ${SAFEC_LIBRARIES})
add_test(jtag_test_tests jtag_test_tests)
set_target_properties(
  jtag_test_tests
  PROPERTIES
    LINK_FLAGS
    " -Wl,--wrap=ASD_log -Wl,--wrap=ASD_log_buffer -Wl,--wrap=ASD_log_shift \
        -Wl,--wrap=strtolevel -Wl,--wrap=strtostreams -Wl,--wrap=JTAGHandler \
        -Wl,--wrap=JTAG_initialize -Wl,--wrap=JTAG_deinitialize -Wl,--wrap=JTAG_set_tap_state \
        -Wl,--wrap=JTAG_shift -Wl,--wrap=JTAG_set_jtag_tck \
        -Wl,--wrap=_memcpy_s_chk \
        -Wl,--wrap=ASD_initialize_log_settings"
  )

#
# Coverage settings
set(COVERAGE_EXCLUDES '*/tests/*')

setup_target_for_coverage(NAME test_coverage EXECUTABLE ctest)
