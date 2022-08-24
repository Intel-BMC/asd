cmake_minimum_required(VERSION 2.8.10 FATAL_ERROR)
project(at-scale-debug C)

find_package (PkgConfig REQUIRED)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/sysroot/include)
pkg_check_modules (SAFEC REQUIRED libsafec)
# Define HAVE_C99 to include sprintf_s macro in safec library
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DHAVE_C99")
include_directories (${SAFEC_INCLUDE_DIRS})
link_directories (${SAFEC_LIBRARY_DIRS})

# Set APB_FREQ to EXTRA_OECMAKE on bb recipe to modify JTAG frequency
if(${APB_FREQ})
    add_definitions( -DAPB_FREQ=${APB_FREQ} )
endif(${APB_FREQ})

# Set I2C_STUB and/or I3C_STUB to EXTRA_OECMAKE on bb recipe to remove
# i2c/i3c features and dependencies respectively.
set(I2C_MSG_BUILDER "i2c_msg_builder.c")
set(I2C_HANDLER "i2c_handler.c")
set(I3C_HANDLER "i3c_handler.c")

if(${I2C_STUB})
    if(${I3C_STUB})
        set(I2C_MSG_BUILDER "i2c_msg_builder_stub.c")
    endif(${I3C_STUB})
endif(${I2C_STUB})

if(${I2C_STUB})
    set(I2C_HANDLER "i2c_handler_stub.c")
endif(${I2C_STUB})

if(${I3C_STUB})
    set(I3C_HANDLER "i3c_handler_stub.c")
endif(${I3C_STUB})

if(${BUILD_UT})
    enable_testing()
    add_subdirectory(tests)
else()
    add_executable(jtag_test jtag_test.c logging.c jtag_handler.c)
    target_link_libraries(jtag_test -lm ${SAFEC_LIBRARIES})
    install (TARGETS jtag_test DESTINATION bin)

    add_executable(asd asd_main.c logging.c ext_network.c authenticate.c
            session.c logging.c config.c jtag_handler.c asd_msg.c ext_tcp.c
            auth_none.c target_handler.c gpio.c dbus_helper.c ext_tls.c
            auth_pam.c ${I2C_MSG_BUILDER} ${I2C_HANDLER} ${I3C_HANDLER}
            vprobe_handler.c)
    target_link_libraries(asd -lm -lsystemd -lssl -lcrypto -lpam -lgpiod ${SAFEC_LIBRARIES})
    install (TARGETS asd DESTINATION bin)

    # Copy pam-asd to etc/pam.d
    install (
        FILES ${CMAKE_CURRENT_SOURCE_DIR}/pam-asd
        DESTINATION /etc/pam.d/
        RENAME asd
    )
endif(${BUILD_UT})
