/*
Copyright (c) 2019, Intel Corporation

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

#include <fcntl.h>
#include <getopt.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "jtag_handler.h"
#include "../jtag_test.h"
#include "logging.h"
#include "cmocka.h"

// static char temporary_log_buffer[512];
void __wrap_ASD_log(ASD_LogLevel level, ASD_LogStream stream,
                    ASD_LogOption options, const char* format, ...)
{
    (void)level;
    (void)stream;
    (void)options;
    (void)format;
    // va_list args;
    // va_start(args, format);
    // vsnprintf(temporary_log_buffer, sizeof(temporary_log_buffer), format,
    // args);
    // fprintf(stderr, "%s\n", temporary_log_buffer);
    // va_end(args);
}

void __wrap_ASD_log_buffer(ASD_LogLevel level, ASD_LogStream stream,
                           ASD_LogOption options, const unsigned char* ptr,
                           size_t len, const char* prefixPtr)
{
    (void)level;
    (void)stream;
    (void)options;
    (void)ptr;
    (void)len;
    (void)prefixPtr;
}

void __wrap_ASD_log_shift(ASD_LogLevel level, ASD_LogStream stream,
                          ASD_LogOption options,
                          const unsigned int number_of_bits,
                          unsigned int size_bytes, const unsigned char* buffer,
                          const char* prefixPtr)
{
    (void)level;
    (void)stream;
    (void)options;
    (void)number_of_bits;
    (void)size_bytes;
    (void)buffer;
    (void)prefixPtr;
}

void __wrap_ASD_initialize_log_settings(ASD_LogLevel level,
                                        ASD_LogStream stream,
                                        bool write_to_syslog,
                                        ShouldLogFunctionPtr should_log_ptr,
                                        LogFunctionPtr log_ptr)
{
    (void)level;
    (void)stream;
    (void)write_to_syslog;
    (void)should_log_ptr;
    (void)log_ptr;
}

bool strtolevel_result = true;
ASD_LogLevel strtolevel_output = ASD_LogLevel_Off;
bool __wrap_strtolevel(char* input, ASD_LogLevel* output)
{
    check_expected_ptr(input);
    check_expected_ptr(output);
    *output = strtolevel_output;
    return strtolevel_result;
}

bool strtostreams_result = true;
ASD_LogStream strtostreams_output = ASD_LogLevel_Off;
bool __wrap_strtostreams(char* input, ASD_LogStream* output)
{
    check_expected_ptr(input);
    check_expected_ptr(output);
    *output = strtostreams_output;
    return strtostreams_result;
}

#define MAX_COMMANDS 20
int command_index = 0;
STATUS command_result[MAX_COMMANDS];

JTAG_Handler* FAKE_JTAG_HANDLER;
JTAG_Handler* __wrap_JTAGHandler()
{
    return FAKE_JTAG_HANDLER;
}

STATUS JTAG_INITIALIZE_RESULT;
STATUS __wrap_JTAG_initialize(JTAG_Handler* state, bool sw_mode)
{
    (void)state;
    (void)sw_mode;
    return JTAG_INITIALIZE_RESULT;
}

STATUS __wrap_JTAG_deinitialize(JTAG_Handler* state)
{
    check_expected_ptr(state);
    return command_result[command_index++];
}

STATUS __wrap_JTAG_set_tap_state(JTAG_Handler* state,
                                 enum jtag_states tap_state)
{
    check_expected_ptr(state);
    check_expected(tap_state);
    return command_result[command_index++];
}

int MEMCPY_SAFE_RESULT = 0;
int __wrap__memcpy_s_chk(void* dest, size_t destsize, const void* src,
                       size_t count)
{
    if (MEMCPY_SAFE_RESULT == 1)
        return MEMCPY_SAFE_RESULT;
    else
    {
        while (count--)
            *(char*)dest++ = *(const char*)src++;
        return MEMCPY_SAFE_RESULT;
    }
}

size_t tdi_bytes = 0;
unsigned char* tdi = NULL;
size_t tdo_bytes = 0;
size_t tdo_index = 0;
unsigned char* tdo = NULL;
STATUS __wrap_JTAG_shift(JTAG_Handler* state, unsigned int number_of_bits,
                         unsigned int input_bytes, unsigned char* input,
                         unsigned int output_bytes, unsigned char* output,
                         enum jtag_states end_tap_state)
{
    check_expected_ptr(state);
    check_expected(number_of_bits);
    check_expected(input_bytes);
    check_expected_ptr(input);
    if (input_bytes > 0 && tdi != NULL && input != NULL)
    {
        memcpy(tdi, input, input_bytes);
        tdi_bytes = input_bytes;
    }
    check_expected(output_bytes);
    check_expected_ptr(output);
    check_expected(end_tap_state);
    if (tdo_bytes > 0 && tdo != NULL && output != NULL)
    {
        memcpy(output, tdo + tdo_index, tdo_bytes);
        tdo_index += tdo_bytes;
    }
    return command_result[command_index++];
}

STATUS JTAG_SET_JTAG_TCK_RESULT;
STATUS __wrap_JTAG_set_jtag_tck(JTAG_Handler* state, unsigned int tck)
{
    (void)state;
    (void)tck;
    return JTAG_SET_JTAG_TCK_RESULT;
}

unsigned long long human_readable = 0xdeadbeefbad4f00d;

jtag_test_args get_default_args()
{
    jtag_test_args args;
    args.human_readable = DEFAULT_TAP_DATA_PATTERN;
    args.ir_shift_size = DEFAULT_IR_SHIFT_SIZE;
    args.loop_forever = false;
    args.numIterations = DEFAULT_NUMBER_TEST_ITERATIONS;
    args.ir_value = DEFAULT_IR_VALUE;           // overridden in manual mode
    args.dr_shift_size = DEFAULT_DR_SHIFT_SIZE; // overridden in manual mode
    args.manual_mode = DEFAULT_TO_MANUAL_MODE;
    args.mode = DEFAULT_JTAG_CONTROLLER_MODE;
    args.tck = DEFAULT_JTAG_TCK;

    memcpy(args.tap_data_pattern, &args.human_readable,
           sizeof(args.human_readable));
    return args;
}

static int setup(void** state)
{
    int i = 0;
    for (; i < MAX_COMMANDS; i++)
    {
        command_result[i] = ST_OK;
    }
    command_index = 0;
    *state = malloc(sizeof(JTAG_Handler));
    assert_true(*state != NULL);
    tdo_bytes = 0;
    tdo_index = 0;
    tdo = (unsigned char*)malloc(MAX_TDO_SIZE);
    memset(tdo, '\0', MAX_TDO_SIZE);

    tdi_bytes = 0;
    tdi = (unsigned char*)malloc(MAX_TDO_SIZE);
    return 0;
}

static int teardown(void** state)
{
    if (tdo)
        free(tdo);
    if (tdi)
        free(tdi);
    free(*state);
    return 0;
}

static void find_pattern_not_found_returns_0(void** state)
{
    (void)state;
    unsigned char shiftDataOut[2048];
    int shiftSize = 192;
    unsigned char dead_beef[8];
    memcpy(dead_beef, &human_readable, sizeof(human_readable));
    memset(shiftDataOut, 0xff, sizeof(shiftDataOut));
    assert_int_equal(find_pattern(shiftDataOut, shiftSize,
                     dead_beef, sizeof(dead_beef)), 0);
}

static void find_pattern_found_returns_value(void** state)
{
    (void)state;
    unsigned char shiftDataOut[2048];
    int shiftSize = 192;
    unsigned char dead_beef[8];
    int expected = 20;
    memcpy(dead_beef, &human_readable, sizeof(human_readable));
    memset(shiftDataOut, 0xff, sizeof(shiftDataOut));
    memcpy(shiftDataOut + expected, &human_readable, sizeof(human_readable));
    assert_int_equal(find_pattern(shiftDataOut, shiftSize,
                     dead_beef, sizeof(dead_beef)),
                     expected);
}

static void shift_right_test(void** state)
{
    (void)state;
    unsigned char buffer[8];
    int i;
    for (i = 0; i < sizeof(buffer); i++)
        buffer[i] = 0xAA;
    shift_right((unsigned char*)&buffer, sizeof(buffer));
    for (i = 0; i < sizeof(buffer); i++)
        assert_int_equal(buffer[i], 0x55);
}

static void shift_right_with_carry_over_test(void** state)
{
    (void)state;
    unsigned char buffer[8];
    int i;
    for (i = 0; i < sizeof(buffer); i++)
        buffer[i] = 0x55;
    shift_right((unsigned char*)&buffer, sizeof(buffer));
    for (i = 0; i < sizeof(buffer); i++)
        assert_int_equal(buffer[i], (i == sizeof(buffer) - 1) ? 0x2A : 0xAA);
}

static void shift_left_test(void** state)
{
    (void)state;
    unsigned char buffer[8];
    int i;
    for (i = 0; i < sizeof(buffer); i++)
        buffer[i] = 0x55;
    shift_left((unsigned char*)&buffer, sizeof(buffer));
    for (i = 0; i < sizeof(buffer); i++)
        assert_int_equal(buffer[i], 0xAA);
}

static void shift_left_with_carry_over_test(void** state)
{
    (void)state;

    unsigned char buffer[8];
    unsigned char expected[8];
    for (int i = 0; i < sizeof(buffer); i++)
    {
        buffer[i] = 0xAA;
        expected[i] = 0x55;
    }
    expected[0] = 0x54;
    shift_left((unsigned char*)&buffer, sizeof(buffer));
    assert_memory_equal(buffer, expected, 8);
}

static void init_jtag_null_jtag_check_test(void** state)
{
    (void)state;
    FAKE_JTAG_HANDLER = NULL;
    assert_null(init_jtag(NULL));
}

static void init_jtag_JTAG_initialize_fail_returns_null_test(void** state)
{
    (void)state;
    jtag_test_args args = get_default_args();
    FAKE_JTAG_HANDLER = (JTAG_Handler*)malloc(sizeof(JTAG_Handler));
    JTAG_INITIALIZE_RESULT = ST_ERR;
    assert_null(init_jtag(&args));
}

static void init_jtag_JTAG_set_jtag_tck_fail_returns_null_test(void** state)
{
    (void)state;
    jtag_test_args args = get_default_args();
    args.mode = HW_MODE;
    args.tck = 11;
    FAKE_JTAG_HANDLER = (JTAG_Handler*)malloc(sizeof(JTAG_Handler));
    JTAG_INITIALIZE_RESULT = ST_OK;
    JTAG_SET_JTAG_TCK_RESULT = ST_ERR;
    assert_null(init_jtag(&args));
}

static void init_jtag_success_test(void** state)
{
    (void)state;
    jtag_test_args args = get_default_args();
    args.mode = HW_MODE;
    args.tck = 11;
    FAKE_JTAG_HANDLER = (JTAG_Handler*)malloc(sizeof(JTAG_Handler));
    JTAG_INITIALIZE_RESULT = ST_OK;
    JTAG_SET_JTAG_TCK_RESULT = ST_OK;
    assert_non_null(init_jtag(&args));
    free(FAKE_JTAG_HANDLER);
}

static void uncore_discovery_reset_rti_fail_test(void** state)
{
    uncore_info uncore;
    jtag_test_args args = get_default_args();
    command_result[0] = ST_ERR;
    expect_any(__wrap_JTAG_set_tap_state, state);
    expect_value(__wrap_JTAG_set_tap_state, tap_state, jtag_tlr);
    assert_false(uncore_discovery(*state, &uncore, &args));
}

static void uncore_discovery_shift_dr_fail_test(void** state)
{
    uncore_info uncore;
    jtag_test_args args = get_default_args();
    command_result[0] = ST_OK;
    expect_any(__wrap_JTAG_set_tap_state, state);
    expect_value(__wrap_JTAG_set_tap_state, tap_state, jtag_tlr);

    command_result[1] = ST_OK;
    expect_any(__wrap_JTAG_set_tap_state, state);
    expect_value(__wrap_JTAG_set_tap_state, tap_state, jtag_rti);

    command_result[2] = ST_ERR;
    expect_any(__wrap_JTAG_set_tap_state, state);
    expect_value(__wrap_JTAG_set_tap_state, tap_state, jtag_shf_dr);

    assert_false(uncore_discovery(*state, &uncore, &args));
}

static void uncore_discovery_jtag_shift_fail_test(void** state)
{
    uncore_info uncore;
    jtag_test_args args = get_default_args();
    command_result[0] = ST_OK;
    expect_any(__wrap_JTAG_set_tap_state, state);
    expect_value(__wrap_JTAG_set_tap_state, tap_state, jtag_tlr);

    command_result[1] = ST_OK;
    expect_any(__wrap_JTAG_set_tap_state, state);
    expect_value(__wrap_JTAG_set_tap_state, tap_state, jtag_rti);

    command_result[2] = ST_OK;
    expect_any(__wrap_JTAG_set_tap_state, state);
    expect_value(__wrap_JTAG_set_tap_state, tap_state, jtag_shf_dr);

    command_result[3] = ST_ERR;
    expect_any(__wrap_JTAG_shift, state);
    expect_value(__wrap_JTAG_shift, number_of_bits,
                 UNCORE_DISCOVERY_SHIFT_SIZE_IN_BITS);
    expect_value(__wrap_JTAG_shift, input_bytes, sizeof(args.tap_data_pattern));
    expect_any(__wrap_JTAG_shift, input);
    expect_value(__wrap_JTAG_shift, output_bytes, MAX_TDO_SIZE);
    expect_any(__wrap_JTAG_shift, output);
    expect_value(__wrap_JTAG_shift, end_tap_state, jtag_rti);

    assert_false(uncore_discovery(*state, &uncore, &args));
}

static void uncore_discovery_pattern_not_found_test(void** state)
{
    uncore_info uncore;
    jtag_test_args args = get_default_args();
    command_result[0] = ST_OK;
    expect_any(__wrap_JTAG_set_tap_state, state);
    expect_value(__wrap_JTAG_set_tap_state, tap_state, jtag_tlr);

    command_result[1] = ST_OK;
    expect_any(__wrap_JTAG_set_tap_state, state);
    expect_value(__wrap_JTAG_set_tap_state, tap_state, jtag_rti);

    command_result[2] = ST_OK;
    expect_any(__wrap_JTAG_set_tap_state, state);
    expect_value(__wrap_JTAG_set_tap_state, tap_state, jtag_shf_dr);

    command_result[3] = ST_OK;
    expect_any(__wrap_JTAG_shift, state);
    expect_value(__wrap_JTAG_shift, number_of_bits,
                 UNCORE_DISCOVERY_SHIFT_SIZE_IN_BITS);
    expect_value(__wrap_JTAG_shift, input_bytes, sizeof(args.tap_data_pattern));
    expect_any(__wrap_JTAG_shift, input);
    expect_value(__wrap_JTAG_shift, output_bytes, MAX_TDO_SIZE);
    expect_any(__wrap_JTAG_shift, output);
    expect_value(__wrap_JTAG_shift, end_tap_state, jtag_rti);

    assert_false(uncore_discovery(*state, &uncore, &args));
}

static void uncore_discovery_success_test(void** state)
{
    uncore_info uncore;
    jtag_test_args args = get_default_args();
    int i;
    const unsigned char id_code[] = {11, 22, 33, 44};

    command_result[0] = ST_OK;
    expect_any(__wrap_JTAG_set_tap_state, state);
    expect_value(__wrap_JTAG_set_tap_state, tap_state, jtag_tlr);

    command_result[1] = ST_OK;
    expect_any(__wrap_JTAG_set_tap_state, state);
    expect_value(__wrap_JTAG_set_tap_state, tap_state, jtag_rti);

    command_result[2] = ST_OK;
    expect_any(__wrap_JTAG_set_tap_state, state);
    expect_value(__wrap_JTAG_set_tap_state, tap_state, jtag_shf_dr);

    for (i = 0; i < MAX_TAPS_SUPPORTED; i++)
    {
        memcpy(tdo + tdo_bytes, id_code, SIZEOF_ID_CODE);
        tdo_bytes += SIZEOF_ID_CODE;
    }
    for (i = 0; i < sizeof(args.tap_data_pattern); i++)
    {
        args.tap_data_pattern[i] = (unsigned char)i;
    }
    memcpy(tdo + tdo_bytes, args.tap_data_pattern,
           sizeof(args.tap_data_pattern));
    tdo_bytes += sizeof(args.tap_data_pattern);

    command_result[3] = ST_OK;
    expect_any(__wrap_JTAG_shift, state);
    expect_value(__wrap_JTAG_shift, number_of_bits,
                 UNCORE_DISCOVERY_SHIFT_SIZE_IN_BITS);
    expect_value(__wrap_JTAG_shift, input_bytes, sizeof(args.tap_data_pattern));
    expect_any(__wrap_JTAG_shift, input);
    expect_value(__wrap_JTAG_shift, output_bytes, MAX_TDO_SIZE);
    expect_any(__wrap_JTAG_shift, output);
    expect_value(__wrap_JTAG_shift, end_tap_state, jtag_rti);

    assert_true(uncore_discovery(*state, &uncore, &args));
    assert_int_equal(uncore.numUncores, MAX_TAPS_SUPPORTED);
    assert_memory_equal(&uncore.idcode, tdo,
                        MAX_TAPS_SUPPORTED * SIZEOF_ID_CODE);
}

static void jtag_test_shift_ir_fail_test(void** state)
{
    uncore_info uncore;
    jtag_test_args args = get_default_args();
    const unsigned char id_code[] = {11, 22, 33, 44};
    uncore.numUncores = MAX_TAPS_SUPPORTED;
    for (int i = 0; i < uncore.numUncores; i++)
    {
        memcpy(tdo + tdo_bytes, id_code, SIZEOF_ID_CODE);
        tdo_bytes += SIZEOF_ID_CODE;
    }
    command_result[0] = ST_ERR;
    expect_any(__wrap_JTAG_set_tap_state, state);
    expect_value(__wrap_JTAG_set_tap_state, tap_state, jtag_shf_ir);
    assert_true(jtag_test(*state, &uncore, &args));
}

static void jtag_test_jtag_shift_ir_value_test(void** state)
{
    uncore_info uncore;
    uncore.numUncores = MAX_TAPS_SUPPORTED;
    jtag_test_args args = get_default_args();
    int i, j;
    const unsigned char id_code[] = {11, 22, 33, 44};
    size_t ir_size = ((uncore.numUncores * args.ir_shift_size) + 7) / 8;
    unsigned char* expected_ir = (unsigned char*)malloc(ir_size);
    memset(expected_ir, '\0', sizeof(expected_ir));
    for (i = 0; i < uncore.numUncores; i++)
    {
        memcpy(uncore.idcode + (i * SIZEOF_ID_CODE), id_code, SIZEOF_ID_CODE);
        // also build the expected_ir
        for (j = 0; j < args.ir_shift_size; j++)
            shift_left(expected_ir, ir_size);
        expected_ir[0] = 2;
    }
    MEMCPY_SAFE_RESULT = 0;
    command_result[0] = ST_OK;
    expect_any(__wrap_JTAG_set_tap_state, state);
    expect_value(__wrap_JTAG_set_tap_state, tap_state, jtag_shf_ir);

    command_result[1] = ST_ERR;
    expect_any(__wrap_JTAG_shift, state);
    expect_value(__wrap_JTAG_shift, number_of_bits,
                 args.ir_shift_size * uncore.numUncores);
    expect_value(__wrap_JTAG_shift, input_bytes, ir_size);
    expect_any(__wrap_JTAG_shift, input);
    expect_value(__wrap_JTAG_shift, output_bytes, 0);
    expect_any(__wrap_JTAG_shift, output);
    expect_value(__wrap_JTAG_shift, end_tap_state, jtag_rti);

    assert_false(jtag_test(*state, &uncore, &args));
    assert_memory_equal(tdi, expected_ir, sizeof(expected_ir));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(find_pattern_not_found_returns_0),
        cmocka_unit_test(find_pattern_found_returns_value),
        cmocka_unit_test(shift_right_test),
        cmocka_unit_test(shift_right_with_carry_over_test),
        cmocka_unit_test(shift_left_test),
        cmocka_unit_test(shift_left_with_carry_over_test),
        cmocka_unit_test(init_jtag_null_jtag_check_test),
        cmocka_unit_test(init_jtag_JTAG_initialize_fail_returns_null_test),
        cmocka_unit_test(init_jtag_JTAG_set_jtag_tck_fail_returns_null_test),
        cmocka_unit_test(init_jtag_success_test),
        cmocka_unit_test_setup_teardown(uncore_discovery_reset_rti_fail_test,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(uncore_discovery_shift_dr_fail_test,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(uncore_discovery_jtag_shift_fail_test,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(uncore_discovery_pattern_not_found_test,
                                        setup, teardown),
        cmocka_unit_test_setup_teardown(uncore_discovery_success_test, setup,
                                        teardown)
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
