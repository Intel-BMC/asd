cmake_minimum_required(VERSION 2.8.10 FATAL_ERROR)
project(at-scale-debug-i3c-dbg-test C)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/sysroot/include)
pkg_check_modules (SAFEC REQUIRED libsafec)
# Define HAVE_C99 to include sprintf_s macro in safec library
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DHAVE_C99")
include_directories (${SAFEC_INCLUDE_DIRS})
include_directories (${ASD_DIR}/include)
include_directories (${ASD_DIR}/target)
link_directories (${SAFEC_LIBRARY_DIRS})
set(SPP_HANDLER "${ASD_DIR}/target/spp_handler.c" "${ASD_DIR}/target/i3c_debug_handler.c")

if(NOT ${BUILD_UT})
    add_executable(i3c_dbg_test i3c_dbg_test.c
            ${ASD_DIR}/server/logging.c
            ${ASD_DIR}/target/jtag_handler.c
            ${SPP_HANDLER})
    target_link_libraries(i3c_dbg_test -lm ${SAFEC_LIBRARIES})
    install (TARGETS i3c_dbg_test DESTINATION bin)
endif(NOT ${BUILD_UT})

add_subdirectory(debug_over_i3c)

if(${BUILD_UT})
    add_subdirectory(tests)
endif(${BUILD_UT})

