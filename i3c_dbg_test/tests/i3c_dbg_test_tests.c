/*
Copyright (c) 2025, Intel Corporation

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of Intel Corporation nor the names of its contributors
      may be used to endorse or promote products derived from this software
      without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// Standard headers must come before cmocka.h to define size_t, va_list, etc.
// DO NOT reorder these includes - cmocka.h depends on these types being defined first
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <cmocka.h>

#include "../i3c_dbg_test.h"
#include "i3c_dbg_mock.h"

// Mock functions if needed
// You can define mock functions here if your code interacts with external
// systems

// Test function for parse_arguments

static const ASD_LogStream stream = ASD_LogStream_SPP;
static const ASD_LogOption option = ASD_LogOption_None;
static void test_parse_arguments(void** unused)
{
    i3c_dbg_test_args args;
    char* argv[] = {"i3c_dbg_test", "--ir-size=0x10", "--log-level=info"};
    int argc = sizeof(argv) / sizeof(argv[0]);

    STATUS result = parse_arguments(argc, argv, &args);
    assert_int_equal(result, ST_OK);
    assert_int_equal(args.ir_shift_size, 0x10);
    assert_int_equal(args.log_level, ASD_LogLevel_Info);
}

// Test function for initialize_bpk
static void test_initialize_bpk(void** unused)
{
    SPP_Handler state;
    i3c_dbg_test_args args;

    // Initialize the args structure
    memset(&args, 0, sizeof(args));
    memset(&state, 0, sizeof(state));

    uint8_t response1[1] = {0x00};
    uint8_t response2[4] = {0x10, 0x10, 0x31, 0x42};
    uint8_t response3[4] = {0x2b, 0x00, 0x00, 0x00};
    reset_mock_data();
    prepare_buffer_read(response1, 1, 0); // Clean previous read.
    prepare_buffer_read(response2, 4, 1);
    prepare_buffer_read(response3, 4, 2);
    STATUS result = initialize_bpk(&state, &args);
    assert_int_equal(result, ST_OK);
}

// Test function for configure_bpk
static void test_configure_bpk(void** unused)
{
    SPP_Handler state;
    i3c_dbg_test_args args;
    args.bpk_values = true;
    // clang-format off
    uint8_t response1[12] = {0x22, 0x00, 0x00, 0x00, 0x11, 0xee,0x77,0x44,0xa5,0xc3,0xc3,0xa5};
    uint8_t response2[8] = {0x42, 0x00, 0x04, 0x00, 0x00, 0x01, 0x00, 0x00}; //SP_VERSIONS
    uint8_t response3[8] = {0x42, 0x00, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00}; //SP_SESSION_MGMT_0
    uint8_t response4[8] = {0x42, 0x00, 0x04, 0x00, 0x00, 0x00, 0x13, 0x00}; //SP_SESSION_MGMT_1
    uint8_t response5[8] = {0x42, 0x00, 0x04, 0x00, 0x0B, 0x12, 0x00, 0x00}; //SP_IDCODE
    uint8_t response6[8] = {0x42, 0x00, 0x04, 0x00, 0x13, 0x81, 0x12, 0x00}; //SP_PROD_ID
    uint8_t response7[8] = {0x42, 0x00, 0x04, 0x00, 0x03, 0x00, 0x00, 0x00}; //SP_CAP_AS_PRESENT
    uint8_t response8[4] = {0x52, 0x00, 0x04, 0x00}; // SP_AS_AVAIL_REQ_SET
    uint8_t response9[8] = {0x42, 0x00, 0x04, 0x00, 0xd0, 0x00, 0x00, 0x00}; //SP_AS_AVAIL_STAT
    uint8_t response10[4] = {0x52, 0x00, 0x04, 0x00}; // SP_AS_AVAIL_REQ_SET
    uint8_t response11[8] = {0x42, 0x00, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00}; //SP_AS_EN_STAT
    // clang-format on
    reset_mock_data();
    prepare_buffer_read(response1, sizeof(response1), 0);
    prepare_buffer_read(response2, sizeof(response2), 1);
    prepare_buffer_read(response3, sizeof(response3), 2);
    prepare_buffer_read(response4, sizeof(response4), 3);
    prepare_buffer_read(response5, sizeof(response5), 4);
    prepare_buffer_read(response6, sizeof(response6), 5);
    prepare_buffer_read(response7, sizeof(response7), 6);
    prepare_buffer_read(response8, sizeof(response8), 7);
    prepare_buffer_read(response9, sizeof(response9), 8);
    prepare_buffer_read(response10, sizeof(response10), 9);
    prepare_buffer_read(response11, sizeof(response11), 10);
    STATUS result = configure_bpk(&state, &args);
    assert_int_equal(result, ST_OK);
}

// Test function for disconnect_bpk
static void test_disconnect_bpk(void** unused)
{
    SPP_Handler state;
    i3c_dbg_test_args args;
    // clang-format off
    uint8_t response1[4] = {0x52, 0x00, 0x04, 0x00}; // SP_AS_AVAIL_REQ_SET
    uint8_t response2[8] = {0x42, 0x00, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00}; //SP_AS_EN_STAT
    // clang-format on
    reset_mock_data();
    prepare_buffer_read(response1, sizeof(response1), 0);
    prepare_buffer_read(response2, sizeof(response2), 1);
    STATUS result = disconnect_bpk(&state, &args);
    assert_int_equal(result, ST_OK);
}

// Main function to run tests
int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_parse_arguments),
        cmocka_unit_test(test_initialize_bpk),
        cmocka_unit_test(test_configure_bpk),
        cmocka_unit_test(test_disconnect_bpk),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
