project(at-scale-debug-server-tests C)

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
# Logging tests
add_executable(logging_tests ../logging.c logging_tests.c ../mem_helper.c)
set_property(TARGET logging_tests PROPERTY C_STANDARD 99)
add_test(logging_test logging_tests)
target_link_libraries(logging_tests cmocka.a -fprofile-arcs -ftest-coverage -lm)
set_target_properties(
  logging_tests
  PROPERTIES
    LINK_FLAGS
    " -Wl,--wrap=malloc -Wl,--wrap=vfprintf -Wl,--wrap=vsyslog -Wl,--wrap=syslog"
  )

#
# asd_main tests
add_executable(asd_main_tests ../asd_main.c asd_main_tests.c ../mem_helper.c ../target_handler.c
               ../dbus_helper.c ../gpio.c)
set_property(TARGET asd_main_tests PROPERTY C_STANDARD 99)
add_test(asd_main_tests asd_main_tests)
target_link_libraries(
  asd_main_tests cmocka.a -fprofile-arcs -ftest-coverage -lsystemd -lgpiod -lm ${SAFEC_LIBRARIES})
set_target_properties(
  asd_main_tests
  PROPERTIES
    LINK_FLAGS
    " -Wl,--wrap=malloc  -Wl,--wrap=free -Wl,--wrap=ASD_log -Wl,--wrap=ASD_log_buffer \
        -Wl,--wrap=strtolevel -Wl,--wrap=strtostreams -Wl,--wrap=getpeername \
        -Wl,--wrap=ASD_initialize_log_settings -Wl,--wrap=session_close_all -Wl,--wrap=session_get_authenticated_conn \
        -Wl,--wrap=session_init -Wl,--wrap=session_getfds -Wl,--wrap=session_close -Wl,--wrap=session_open \
        -Wl,--wrap=session_close_expired_unauth -Wl,--wrap=session_lookup_conn -Wl,--wrap=session_get_data_pending \
        -Wl,--wrap=session_already_authenticated -Wl,--wrap=session_set_data_pending \
        -Wl,--wrap=session_auth_complete -Wl,--wrap=set_config_defaults \
        -Wl,--wrap=asd_msg_init \
        -Wl,--wrap=memcpy_safe \
        -Wl,--wrap=asd_msg_free -Wl,--wrap=auth_init -Wl,--wrap=extnet_init -Wl,--wrap=extnet_open_external_socket \
        -Wl,--wrap=extnet_send -Wl,--wrap=extnet_accept_connection -Wl,--wrap=extnet_close_client \
        -Wl,--wrap=auth_client_handshake -Wl,--wrap=extnet_recv -Wl,--wrap=close -Wl,--wrap=read \
        -Wl,--wrap=asd_msg_read -Wl,--wrap=asd_msg_get_fds -Wl,--wrap=asd_msg_event \
        -Wl,--wrap=eventfd -Wl,--wrap=poll"
  )

#
# Session tests
add_executable(session_tests ../session.c session_tests.c ../mem_helper.c)
set_property(TARGET session_tests PROPERTY C_STANDARD 99)
add_test(session_test session_tests)
target_link_libraries(session_tests cmocka.a -fprofile-arcs -ftest-coverage -lm ${SAFEC_LIBRARIES})
set_target_properties(
  session_tests
  PROPERTIES
    LINK_FLAGS
    "-Wl,--wrap=ASD_log -Wl,--wrap=ASD_log_buffer \
        -Wl,--wrap=extnet_open_external_socket -Wl,--wrap=extnet_accept_connection \
        -Wl,--wrap=extnet_close_client -Wl,--wrap=extnet_init_client \
        -Wl,--wrap=memcpy_safe \
        -Wl,--wrap=extnet_is_client_closed -Wl,--wrap=malloc"
  )

#
# Auth None tests
add_executable(auth_none_tests ../auth_none.c auth_none_tests.c)
set_property(TARGET auth_none_tests PROPERTY C_STANDARD 99)
add_test(auth_none_test auth_none_tests)
target_link_libraries(
  auth_none_tests cmocka.a -fprofile-arcs -ftest-coverage -lm)

#
# Auth Pam tests
add_executable(auth_pam_tests ../auth_pam.c auth_pam_tests.c ../mem_helper.c)
set_property(TARGET auth_pam_tests PROPERTY C_STANDARD 99)
target_compile_definitions(auth_pam_tests PRIVATE UNIT_TESTING_ONLY=1)
add_test(auth_pam_test auth_pam_tests)
target_link_libraries(
  auth_pam_tests cmocka.a -fprofile-arcs -ftest-coverage -lm ${SAFEC_LIBRARIES})
set_target_properties(
  auth_pam_tests
  PROPERTIES
    LINK_FLAGS
    "-Wl,--wrap=ASD_log -Wl,--wrap=pam_start \
                     -Wl,--wrap=pam_set_item -Wl,--wrap=pam_authenticate \
                     -Wl,--wrap=pam_end -Wl,--wrap=extnet_send \
                     -Wl,--wrap=strcpy_safe \
                     -Wl,--wrap=RAND_bytes -Wl,--wrap=extnet_recv \
                     -Wl,--wrap=session_get_authenticated_conn \
                     -Wl,--wrap=malloc -Wl,--wrap=calloc"
  )

#
# Authenticate tests
add_executable(authenticate_tests ../authenticate.c authenticate_tests.c)
set_property(TARGET authenticate_tests PROPERTY C_STANDARD 99)
add_test(authenticate_test authenticate_tests)
target_link_libraries(authenticate_tests
                      cmocka.a
                      -fprofile-arcs
                      -ftest-coverage)
set_target_properties(authenticate_tests
                      PROPERTIES LINK_FLAGS "-Wl,--wrap=ASD_log")

#
# Ext Network tests
add_executable(ext_network_tests ../ext_network.c ext_network_tests.c)
set_property(TARGET ext_network_tests PROPERTY C_STANDARD 99)
add_test(ext_network_test ext_network_tests)
target_link_libraries(ext_network_tests cmocka.a -fprofile-arcs -ftest-coverage)
set_target_properties(
  ext_network_tests
  PROPERTIES
    LINK_FLAGS
    "-Wl,--wrap=ASD_log -Wl,--wrap=malloc -Wl,--wrap=signal \
        -Wl,--wrap=socket -Wl,--wrap=setsockopt -Wl,--wrap=bind \
        -Wl,--wrap=listen -Wl,--wrap=accept -Wl,--wrap=close"
  )

#
# Ext Tcp tests
add_executable(ext_tcp_tests ../ext_tcp.c ext_tcp_tests.c)
set_property(TARGET ext_tcp_tests PROPERTY C_STANDARD 99)
add_test(ext_tcp_test ext_tcp_tests)
target_link_libraries(ext_tcp_tests cmocka.a -fprofile-arcs -ftest-coverage)
set_target_properties(ext_tcp_tests
                      PROPERTIES
                        LINK_FLAGS
                        "-Wl,--wrap=ASD_log -Wl,--wrap=recv -Wl,--wrap=send")

#
# Ext TLS tests
add_executable(ext_tls_tests ../ext_tls.c ext_tls_tests.c)
set_property(TARGET ext_tls_tests PROPERTY C_STANDARD 99)
add_test(ext_tls_test ext_tls_tests)
target_link_libraries(ext_tls_tests cmocka.a -fprofile-arcs -ftest-coverage)
set_target_properties(
  ext_tls_tests
  PROPERTIES
    LINK_FLAGS
    "-Wl,--wrap=ASD_log -Wl,--wrap=OPENSSL_init_ssl \
                     -Wl,--wrap=SSL_CTX_free -Wl,--wrap=TLS_server_method \
                     -Wl,--wrap=SSL_CTX_new -Wl,--wrap=ERR_get_error \
                     -Wl,--wrap=ERR_error_string_n \
                     -Wl,--wrap=SSL_CTX_set_options \
                     -Wl,--wrap=SSL_CTX_set_cipher_list \
                     -Wl,--wrap=SSL_CTX_use_certificate_file \
                     -Wl,--wrap=SSL_CTX_use_PrivateKey_file \
                     -Wl,--wrap=SSL_CTX_check_private_key \
                     -Wl,--wrap=SSL_new -Wl,--wrap=SSL_set_fd \
                     -Wl,--wrap=SSL_accept -Wl,--wrap=SSL_free \
                     -Wl,--wrap=extnet_close_client \
                     -Wl,--wrap=SSL_get_peer_certificate \
                     -Wl,--wrap=X509_get_subject_name \
                     -Wl,--wrap=X509_NAME_oneline \
                     -Wl,--wrap=X509_get_issuer_name \
                     -Wl,--wrap=X509_free -Wl,--wrap=SSL_get_error \
                     -Wl,--wrap=ERR_print_errors_fp -Wl,--wrap=SSL_read \
                     -Wl,--wrap=SSL_pending -Wl,--wrap=SSL_write \
                     -Wl,--wrap=setsockopt"
  )

#
# Config tests
add_executable(config_tests ../config.c config_tests.c)
set_property(TARGET config_tests PROPERTY C_STANDARD 99)
add_test(config_test config_tests)
target_link_libraries(config_tests cmocka.a -fprofile-arcs -ftest-coverage)
set_target_properties(config_tests PROPERTIES LINK_FLAGS "-Wl,--wrap=ASD_log")

#
# Coverage settings
set(COVERAGE_EXCLUDES '*/tests/*')

setup_target_for_coverage(NAME test_coverage EXECUTABLE ctest)
