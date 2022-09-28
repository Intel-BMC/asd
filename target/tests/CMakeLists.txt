project(at-scale-debug-target-tests C)

#
# CMake options
cmake_minimum_required(VERSION 2.8)

#
# import cmocka
find_package(cmocka REQUIRED)

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
# jtag_handler tests
add_executable(jtag_handler_tests
               ../jtag_handler.c
               jtag_handler_tests.c
               ../mem_helper.c)
set_property(TARGET jtag_handler_tests PROPERTY C_STANDARD 99)
add_test(jtag_handler_tests jtag_handler_tests)
target_link_libraries(
  jtag_handler_tests cmocka.a -fprofile-arcs -ftest-coverage -lm ${SAFEC_LIBRARIES})
set_target_properties(
  jtag_handler_tests
  PROPERTIES
    LINK_FLAGS
    " -Wl,--wrap=malloc -Wl,--wrap=ASD_log -Wl,--wrap=ASD_log_buffer \
        -Wl,--wrap=memcpy_safe \
        -Wl,--wrap=ASD_log_shift -Wl,--wrap=open -Wl,--wrap=ioctl"
  )

#
# asd_msg tests
add_executable(asd_msg_tests
               ../asd_msg.c
               ../i2c_msg_builder.c
               ../vprobe_handler.c
               ../dbus_helper.c
               asd_msg_tests.c
               ../mem_helper.c)
set_property(TARGET asd_msg_tests PROPERTY C_STANDARD 99)
add_test(asd_msg_tests asd_msg_tests)
target_link_libraries(asd_msg_tests cmocka.a -fprofile-arcs -ftest-coverage -lsystemd -lm ${SAFEC_LIBRARIES})
set_target_properties(
  asd_msg_tests
  PROPERTIES
    LINK_FLAGS
    " -Wl,--wrap=malloc -Wl,--wrap=ASD_log -Wl,--wrap=ASD_log_buffer -Wl,--wrap=JTAGHandler \
        -Wl,--wrap=fopen \
        -Wl,--wrap=JTAG_initialize -Wl,--wrap=JTAG_deinitialize -Wl,--wrap=JTAG_set_tap_state \
        -Wl,--wrap=JTAG_shift -Wl,--wrap=JTAG_set_jtag_tck -Wl,--wrap=JTAG_wait_cycles \
        -Wl,--wrap=JTAG_tap_reset -Wl,--wrap=JTAG_set_padding \
        -Wl,--wrap=JTAG_get_tap_state -Wl,--wrap=JTAG_set_active_chain \
        -Wl,--wrap=TargetHandler -Wl,--wrap=target_initialize \
        -Wl,--wrap=target_deinitialize -Wl,--wrap=target_wait_sync \
        -Wl,--wrap=memcpy_safe \
        -Wl,--wrap=target_write_event_config -Wl,--wrap=target_write \
        -Wl,--wrap=target_read -Wl,--wrap=target_wait_PRDY \
        -Wl,--wrap=target_get_fds -Wl,--wrap=target_event \
        -Wl,--wrap=I2CHandler -Wl,--wrap=i2c_initialize \
        -Wl,--wrap=i2c_deinitialize -Wl,--wrap=i2c_msg_reset \
        -Wl,--wrap=i2c_msg_get_count -Wl,--wrap=i2c_msg_get_asd_i2c_msg \
        -Wl,--wrap=i2c_bus_select -Wl,--wrap=i2c_set_sclk -Wl,--wrap=i2c_read_write -Wl,--wrap=flock"
  )

#
# gpio tests
add_executable(gpio_tests ../gpio.c gpio_tests.c ../mem_helper.c)
set_property(TARGET gpio_tests PROPERTY C_STANDARD 99)
add_test(gpio_test gpio_tests)
target_link_libraries(gpio_tests cmocka.a -fprofile-arcs -ftest-coverage -lm ${SAFEC_LIBRARIES})
set_target_properties(
  gpio_tests
  PROPERTIES
    LINK_FLAGS
    "-Wl,--wrap=open -Wl,--wrap=close -Wl,--wrap=write \
                     -Wl,--wrap=ASD_log \
                     -Wl,--wrap=read -Wl,--wrap=lseek"
  )

#
# target handler tests
add_executable(target_handler_tests
               ../target_handler.c
               ../logging.c
               ../dbus_helper.c
               target_handler_tests.c
               ../mem_helper.c)
set_property(TARGET target_handler_tests PROPERTY C_STANDARD 99)
add_test(target_handler_test target_handler_tests)
target_link_libraries(
  target_handler_tests cmocka.a -fprofile-arcs -ftest-coverage -lm -lsystemd -lgpiod ${SAFEC_LIBRARIES})
set_target_properties(
  target_handler_tests
  PROPERTIES
    LINK_FLAGS
    "-Wl,--wrap=malloc -Wl,--wrap=poll -Wl,--wrap=ASD_log \
                    -Wl,--wrap=gpio_export -Wl,--wrap=gpio_unexport \
                    -Wl,--wrap=gpio_set_value -Wl,--wrap=gpio_get_value \
                    -Wl,--wrap=gpio_set_direction \
                    -Wl,--wrap=gpio_set_edge \
                    -Wl,--wrap=open \
                    -Wl,--wrap=lseek \
                    -Wl,--wrap=read \
                    -Wl,--wrap=gpiod_ctxless_find_line \
                    -Wl,--wrap=gpiod_chip_open \
                    -Wl,--wrap=gpiod_chip_get_line \
                    -Wl,--wrap=gpiod_chip_close \
                    -Wl,--wrap=gpiod_line_request \
                    -Wl,--wrap=gpiod_line_event_get_fd \
                    -Wl,--wrap=gpiod_line_get_value \
                    -Wl,--wrap=gpiod_line_set_value \
                    -Wl,--wrap=gpiod_line_event_read\
                    -Wl,--wrap=dbus_power_reboot \
                    -Wl,--wrap=dbus_power_on \
                    -Wl,--wrap=dbus_power_off \
                    -Wl,--wrap=dbus_get_powerstate \
                    -Wl,--wrap=gpio_set_active_low -Wl,--wrap=usleep \
                    -Wl,--wrap=dbus_helper -Wl,--wrap=dbus_initialize \
                    -Wl,--wrap=dbus_deinitialize -Wl,--wrap=dbus_power_reset \
                    -Wl,--wrap=dbus_power_toggle -Wl,--wrap=dbus_process_event"
  )

#
# dbus helper tests
add_executable(dbus_helper_tests
               ../dbus_helper.c
               ../mem_helper.c
               dbus_helper_tests.c)
set_property(TARGET dbus_helper_tests PROPERTY C_STANDARD 99)
add_test(dbus_helper_tests dbus_helper_tests)
target_link_libraries(
  dbus_helper_tests cmocka.a -fprofile-arcs -ftest-coverage -lm -lsystemd ${SAFEC_LIBRARIES})
set_target_properties(
  dbus_helper_tests
  PROPERTIES
    LINK_FLAGS
    "-Wl,--wrap=ASD_log \
                     -Wl,--wrap=malloc -Wl,--wrap=sd_bus_open_system \
                     -Wl,--wrap=sd_bus_unref -Wl,--wrap=sd_bus_get_property \
                     -Wl,--wrap=sd_bus_process \
                     -Wl,--wrap=sd_bus_call_async \
                     -Wl,--wrap=sd_bus_message_new_method_call \
                     -Wl,--wrap=sd_bus_message_append \
                     -Wl,--wrap=sd_bus_message_open_container \
                     -Wl,--wrap=sd_bus_message_close_container \
                     -Wl,--wrap=sd_bus_error_set_errno \
                     -Wl,--wrap=sd_bus_get_fd \
                     -Wl,--wrap=sd_bus_message_read \
                     -Wl,--wrap=sd_bus_error_free \
                     -Wl,--wrap=sd_bus_message_unref \
                     -Wl,--wrap=sd_bus_add_match \
                     -Wl,--wrap=sd_bus_message_skip \
                     -Wl,--wrap=sd_bus_message_enter_container \
                     -Wl,--wrap=sd_bus_message_exit_container"
  )

#
# I2C Handler tests
add_executable(i2c_handler_tests ../i2c_handler.c i2c_handler_tests.c)
set_property(TARGET i2c_handler_tests PROPERTY C_STANDARD 99)
add_test(i2c_handler_test i2c_handler_tests)
target_link_libraries(i2c_handler_tests cmocka.a -fprofile-arcs -ftest-coverage)
set_target_properties(
  i2c_handler_tests
  PROPERTIES
    LINK_FLAGS
    "-Wl,--wrap=ASD_log -Wl,--wrap=ASD_log_buffer -Wl,--wrap=open \
        -Wl,--wrap=close -Wl,--wrap=ioctl,--wrap=malloc"
  )

#
# I2C Msg Builder tests
add_executable(i2c_msg_builder_tests ../i2c_msg_builder.c
               i2c_msg_builder_tests.c)
set_property(TARGET i2c_msg_builder_tests PROPERTY C_STANDARD 99)
add_test(i2c_msg_builder_tests i2c_msg_builder_tests)
target_link_libraries(i2c_msg_builder_tests
                      cmocka.a
                      -fprofile-arcs
                      -ftest-coverage)
set_target_properties(i2c_msg_builder_tests
                      PROPERTIES LINK_FLAGS "-Wl,--wrap=ASD_log,--wrap=malloc")
#
# Mem_Helper tests
add_executable(mem_helper_tests mem_helper_test.c ../mem_helper.c)
set_property(TARGET mem_helper_tests PROPERTY C_STANDARD 99)
add_test(mem_helper_test mem_helper_tests)
target_link_libraries(
  mem_helper_tests cmocka.a -fprofile-arcs -ftest-coverage -lm)
set_target_properties(mem_helper_tests
                      PROPERTIES LINK_FLAGS "-Wl,--wrap=ASD_log")
#
# Coverage settings
set(COVERAGE_EXCLUDES '*/tests/*')

setup_target_for_coverage(NAME test_coverage EXECUTABLE ctest)
