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

#include "jtag_test.h"

#include <fcntl.h>
#include <getopt.h>
#include <safe_mem_lib.h>
#include <safe_str_lib.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "logging.h"

#ifndef timersub
#define timersub(a, b, result)                                                 \
    do                                                                         \
    {                                                                          \
        (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;                          \
        (result)->tv_usec = (a)->tv_usec - (b)->tv_usec;                       \
        if ((result)->tv_usec < 0)                                             \
        {                                                                      \
            --(result)->tv_sec;                                                \
            (result)->tv_usec += 1000000;                                      \
        }                                                                      \
    } while (0)
#endif

bool continue_loop = true;
const ASD_LogStream stream = ASD_LogStream_Test;
const ASD_LogOption option = ASD_LogOption_None;
uint64_t failures = 0;
const ir_shift_size_map ir_map[] = {{0x0E7BB013, IR14_SHIFT_SIZE},
                                    {0x00044113, IR16_SHIFT_SIZE},
                                    {0x00111113, IR16_SHIFT_SIZE},
                                    {0x00133113, IR16_SHIFT_SIZE},
                                    {0x0E7C5013, IR14_SHIFT_SIZE},
                                    {0x00155113, IR16_SHIFT_SIZE},
                                    {0x00128113, IR12_SHIFT_SIZE},
                                    {0x00125113, IR16_SHIFT_SIZE},
                                    {0x00138113, IR12_SHIFT_SIZE},
                                    {0x0E7D4113, IR08_SHIFT_SIZE},
                                    {0x0012d113, IR16_SHIFT_SIZE},
                                    {0x00168113, IR12_SHIFT_SIZE}};

#define MAP_LINE_SIZE 58
char ir_size_map_str[((sizeof(ir_map)/sizeof(ir_shift_size_map)) + 6) * MAP_LINE_SIZE];

#ifndef UNIT_TEST_MAIN
int main(int argc, char** argv)
{
    return jtag_test_main(argc, argv);
}
#endif

int jtag_test_main(int argc, char** argv)
{
    JTAG_Handler* jtag = NULL;
    uncore_info uncore;
    explicit_bzero(uncore.idcode, sizeof(uncore.idcode));
    uncore.numUncores = 0;
    jtag_test_args args;
    bool result;
    bool print_results = false;

    signal(SIGINT, interrupt_handler); // catch ctrl-c

    ASD_initialize_log_settings(DEFAULT_LOG_LEVEL, DEFAULT_LOG_STREAMS, false,
                                false, NULL, NULL);

    result = parse_arguments(argc, argv, &args);

    ASD_initialize_log_settings(args.log_level, args.log_streams, false, false,
                                NULL, NULL);

    if (result)
    {
        jtag = init_jtag(&args);
        result = jtag != NULL;
    }

    if (result)
        result = uncore_discovery(jtag, &uncore, &args);

    if (result)
    {
        if (args.ir_shift_size == 0)
        {
            args.ir_shift_size = DEFAULT_IR_SHIFT_SIZE;
            // Use lookup table to get the right ir_shift_size from idcode
            int n = sizeof(ir_map)/sizeof(ir_shift_size_map);
            for (int i = 0; i < n; i++)
            {
                if ((uncore.idcode[0] & IR_SIG_MASK) == ir_map[i].signature)
                {
                    args.ir_shift_size = ir_map[i].ir_shift_size;
                    break;
                }
            }
            ASD_log(ASD_LogLevel_Debug, stream, option,
                    "Using 0x%x for ir_shift_size", args.ir_shift_size);
        }
        result = reset_jtag_to_RTI(jtag);
    }

    if (result)
        result = jtag_test(jtag, &uncore, &args);

    if (jtag)
    {
        if (JTAG_deinitialize(jtag) != ST_OK)
        {
            ASD_log(ASD_LogLevel_Error, stream, option,
                    "Failed to deinitialize the JTAG handler.");
            result = false;
        }
        else
        {
            free(jtag);
        }
    }

    return result ? 0 : -1;
}

// interrupt handler for ctrl-c
void interrupt_handler(int dummy)
{
    (void)dummy;
    continue_loop = false;
}

bool parse_arguments(int argc, char** argv, jtag_test_args* args)
{
    int c = 0;
    opterr = 0; // prevent getopt_long from printing shell messages
    failures = 0;

    // Set Default argument values.
    args->human_readable = DEFAULT_TAP_DATA_PATTERN;
    args->ir_shift_size = 0;
    args->loop_forever = false;
    args->numIterations = DEFAULT_NUMBER_TEST_ITERATIONS;
    args->runTime = DEFAULT_RUNTIME;
    args->ir_value = DEFAULT_IR_VALUE;           // overridden in manual mode
    args->dr_shift_size = DEFAULT_DR_SHIFT_SIZE; // overridden in manual mode
    args->manual_mode = DEFAULT_TO_MANUAL_MODE;
    args->count_mode = false;
    args->pattern_mode = false;
    args->inject_error = false;
    args->mode = DEFAULT_JTAG_CONTROLLER_MODE;
    args->tck = DEFAULT_JTAG_TCK;
    args->log_level = DEFAULT_LOG_LEVEL;
    args->log_streams = DEFAULT_LOG_STREAMS;
    args->inject_error_byte = DEFAULT_ERROR_INJECTION_POS;

    enum
    {
        ARG_IR_SIZE = 256,
        ARG_DR_SIZE,
        ARG_IR_VALUE,
        ARG_DR_OVERSHIFT,
        ARG_LOG_LEVEL,
        ARG_LOG_STREAMS,
        ARG_SEED,
        ARG_PATTERN,
        ARG_RUNTIME,
        ARG_INJECT,
        ARG_HELP
    };

    struct option opts[] = {
        {"ir-size", 1, NULL, ARG_IR_SIZE},
        {"dr-size", 1, NULL, ARG_DR_SIZE},
        {"ir-value", 1, NULL, ARG_IR_VALUE},
        {"dr-overshift", 1, NULL, ARG_DR_OVERSHIFT},
        {"log-level", 1, NULL, ARG_LOG_LEVEL},
        {"log-streams", 1, NULL, ARG_LOG_STREAMS},
        {"seed", 1, NULL, ARG_SEED},
        {"pattern", 1, NULL, ARG_PATTERN},
        {"runtime", 1, NULL, ARG_RUNTIME},
        {"injecterror", 1, NULL, ARG_INJECT},
        {"help", 0, NULL, ARG_HELP},
        {NULL, 0, NULL, 0},
    };

    load_ir_size_map_str();

    while ((c = getopt_long(argc, argv, "fci:ht:?", opts, NULL)) != -1)
    {
        switch (c)
        {
            case 'f':
                args->loop_forever = true;
                break;
            case 'c':
                args->count_mode = true;
                break;
            case 'i':
                args->loop_forever = false;
                if ((args->numIterations = (int)strtol(optarg, NULL, 10)) <= 0)
                {
                    showUsage(argv);
                    return false;
                }
                break;

            case 'h':
                args->mode = HW_MODE;
                break;

            case 't':
                args->tck = (unsigned int)strtol(optarg, NULL, 10);
                break;

            case ARG_IR_SIZE:
                args->ir_shift_size = (unsigned int)strtol(optarg, NULL, 16);
                if (args->ir_shift_size > MAX_IR_SHIFT_SIZE)
                {
                    showUsage(argv);
                    return false;
                }
                if (args->ir_shift_size != DEFAULT_IR_SHIFT_SIZE &&
                    args->ir_shift_size != IR14_SHIFT_SIZE &&
                    args->ir_shift_size != IR16_SHIFT_SIZE)
                {
                    ASD_log(ASD_LogLevel_Warning, stream, option,
                            "IR shift size should be 0xb for 14nm-family, 0xe"
                            " for 10nm-family and 0x10 for Intel 7 family."
                            " IR shift size given value = %d.",
                            args->ir_shift_size);
                }
                break;

            case ARG_DR_SIZE:
                args->dr_shift_size = (unsigned int)strtol(optarg, NULL, 16);
                args->manual_mode = true;
                if (args->dr_shift_size > MAX_DR_SHIFT_SIZE)
                {
                    showUsage(argv);
                    return false;
                }
                break;

            case ARG_IR_VALUE:
                args->ir_value = (unsigned int)strtol(optarg, NULL, 16);
                args->manual_mode = true;
                break;

            case ARG_DR_OVERSHIFT:
                args->human_readable = strtoull(optarg, NULL, 16);
                break;

            case ARG_LOG_LEVEL:
                if (!strtolevel(optarg, &args->log_level))
                {
                    showUsage(argv);
                    return false;
                }
                break;

            case ARG_LOG_STREAMS:
                if (!strtostreams(optarg, &args->log_streams))
                {
                    showUsage(argv);
                    return false;
                }
                break;

            case ARG_SEED:
                args->seed = (unsigned int)strtol(optarg, NULL, 10);
                break;
            case ARG_PATTERN:
                args->pattern = optarg;
                args->pattern_mode = true;
                break;
            case ARG_RUNTIME:
                args->loop_forever = true;
                args->runTime = (unsigned int)strtol(optarg, NULL, 10);
                break;
            case ARG_INJECT:
                args->inject_error = true;
                args->inject_error_byte = (unsigned int)strtol(optarg, NULL, 10);
                break;
            case '?':
            case ARG_HELP:
            default:
                showUsage(argv);
                return false;
        }
    }

    if (args->dr_shift_size > MAX_TDO_SIZE * 8)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "DR shift size cannot be larger than %d", MAX_TDO_SIZE * 8);
        showUsage(argv);
        return false;
    }

    if (args->manual_mode)
    {
        ASD_log(ASD_LogLevel_Error, stream, option, "IR Value = 0x%x",
                args->ir_value);
        ASD_log(ASD_LogLevel_Error, stream, option, "IR shift size = 0x%x",
                args->ir_shift_size);
        ASD_log(ASD_LogLevel_Error, stream, option, "DR shift size = 0x%x",
                args->dr_shift_size);
    }
    if (memcpy_s(args->tap_data_pattern, sizeof(args->tap_data_pattern),
                 &args->human_readable, sizeof(args->human_readable)))
    {
        ASD_log(ASD_LogLevel_Error, ASD_LogStream_JTAG, ASD_LogOption_None,
                "memcpy_s: human_readable to tap_data_pattern copy failed.");
        return false;
    }

    return true;
}

void load_ir_size_map_str()
{
    int arr_size = sizeof(ir_map)/sizeof(ir_shift_size_map);
    sprintf_s(&ir_size_map_str[0], MAP_LINE_SIZE,
              "                             +------------+------------+\n");
    sprintf_s(&ir_size_map_str[MAP_LINE_SIZE - 1], MAP_LINE_SIZE,
              "                             |  ID CODE   |IR-SIZE(HEX)|\n");
    sprintf_s(&ir_size_map_str[(MAP_LINE_SIZE - 1) * 2], MAP_LINE_SIZE,
              "                             +------------+------------+\n");

    for (int i=0; i<arr_size; i++) {
        sprintf_s(&ir_size_map_str[(MAP_LINE_SIZE - 1)*(3 + i)], MAP_LINE_SIZE,
                  "                             | 0x%08x | 0x%x%s    |\n",
                  ir_map[i].signature,
                  ir_map[i].ir_shift_size,
                  ir_map[i].ir_shift_size < 0x10 ? "    ":
                  ir_map[i].ir_shift_size < 0x100 ? "   ":
                  ir_map[i].ir_shift_size < 0x1000 ? "  ": " ");
    }
    sprintf_s(&ir_size_map_str[(MAP_LINE_SIZE-1) * (arr_size+3)], MAP_LINE_SIZE,
              "                             | DEFAULT    | 0x%x%s    |\n",
              DEFAULT_IR_SHIFT_SIZE,
              DEFAULT_IR_SHIFT_SIZE < 0x10 ? "    ":
              DEFAULT_IR_SHIFT_SIZE < 0x100 ? "   ":
              DEFAULT_IR_SHIFT_SIZE < 0x1000 ? "  ": " ");

    sprintf_s(&ir_size_map_str[(MAP_LINE_SIZE-1) * (arr_size+4)], MAP_LINE_SIZE,
              "                             +------------+------------+\n");

}

void showUsage(char** argv)
{
    ASD_log(
        ASD_LogLevel_Error, stream, option,
        "\nVersion: %s \n"
        "Usage: %s [option]\n\n"
        "  -f          Run endlessly until ctrl-c is used\n"
        "  -c          Complete all iterations and count failing cases\n"
        "  -i <number> Run [number] of iterations (default: %d)\n"
        "  -h          Run in Hardware JTAG mode (default: %s)\n"
        "  -t <number> JTAG tck divisor (default: %d)\n"
        "\n"
        "  --dr-overshift=<hex value> Specify 64bit overscan (default: "
        "0x%llx)\n"
        "  --ir-size=<hex bits>       Specify IR size (max: 0x%x)\n"
        "                             See default IR size setting rules for\n"
        "                             known ID codes in the following table:\n"
        "%s"
        "  --dr-size=<hex bits>       Specify DR size (default: 0x%x) (max: "
        "0x%x)\n"
        "  --ir-value=<hex value>     Specify IR command (default: 0x%x)\n"
        "  --log-level=<level>        Specify Logging Level (default: %s)\n"
        "                             Levels:\n"
        "                               %s\n"
        "                               %s\n"
        "                               %s\n"
        "                               %s\n"
        "                               %s\n"
        "                               %s\n"
        "  --log-streams=<streams>    Specify Logging Streams (default: %s)\n"
        "                             Multiple streams can be comma "
        "separated.\n"
        "                             Streams:\n"
        "                               %s\n"
        "                               %s\n"
        "                               %s\n"
        "                               %s\n"
        "                               %s\n"
        "                               %s\n"
        "  --seed=<value>             Specify seed for random mode: (default: time seed), <value> (0 - 2147483647)\n"
        "  --pattern=<type>           Specify pattern type (Default:(Random (RN)), Static (ST) [dr-overshift value]\n"
        "                             Checkerboard (CB),Walkingzero (WZ) , Walkingone (WO))\n"
        "  --runtime=<number>         Specify time in seconds jtag_test will run. (disables iterations) (Default: 1s)\n"
        "  --injecterror=<byte>       Inject Error to test bit flip at position byte (Default: byte = 0)\n"
        "  --help                     Show this list\n"
        "\n"
        "Examples:\n"
        "\n"
        "Log from the test app and jtag at trace level.\n"
        "     jtag_test --log-level=trace --log-streams=test,jtag\n"
        "\n"
        "Read a register, such as SA_TAP_LR_UNIQUEID_CHAIN.\n"
        "     jtag_test --ir-value=0x22 --dr-size=0x40\n"
        "\n",
        asd_version,
        argv[0], DEFAULT_NUMBER_TEST_ITERATIONS,
        DEFAULT_JTAG_CONTROLLER_MODE == SW_MODE ? "SW" : "HW", DEFAULT_JTAG_TCK,
        DEFAULT_TAP_DATA_PATTERN, MAX_IR_SHIFT_SIZE, ir_size_map_str,
        DEFAULT_DR_SHIFT_SIZE, MAX_DR_SHIFT_SIZE, DEFAULT_IR_VALUE,
        ASD_LogLevelString[DEFAULT_LOG_LEVEL],
        ASD_LogLevelString[ASD_LogLevel_Off],
        ASD_LogLevelString[ASD_LogLevel_Error],
        ASD_LogLevelString[ASD_LogLevel_Warning],
        ASD_LogLevelString[ASD_LogLevel_Info],
        ASD_LogLevelString[ASD_LogLevel_Debug],
        ASD_LogLevelString[ASD_LogLevel_Trace],
        streamtostring(DEFAULT_LOG_STREAMS), streamtostring(ASD_LogStream_All),
        streamtostring(ASD_LogStream_Test), streamtostring(ASD_LogStream_I2C),
        streamtostring(ASD_LogStream_Pins), streamtostring(ASD_LogStream_JTAG),
        streamtostring(ASD_LogStream_Network));
}

JTAG_Handler* init_jtag(jtag_test_args* args)
{
    JTAG_Handler* jtag = JTAGHandler();
    if (jtag == NULL)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "Failed to initialize the driver.");
    }
    else if (JTAG_initialize(jtag, args->mode == SW_MODE) != ST_OK)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "Failed to initialize JTAG handler.");
        free(jtag);
        jtag = NULL;
    }
    else if (args->mode == HW_MODE &&
             JTAG_set_jtag_tck(jtag, args->tck) != ST_OK)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "Failed to set jtag clock divisor.");
        close(jtag->JTAG_driver_handle);
        free(jtag);
        jtag = NULL;
    }
    return jtag;
}

bool uncore_discovery(JTAG_Handler* jtag, uncore_info* uncore,
                      jtag_test_args* args)
{
    char prefix[32];
    unsigned int shift_size = UNCORE_DISCOVERY_SHIFT_SIZE_IN_BITS;
    unsigned int index = 0;
    unsigned char tdo[MAX_TDO_SIZE];

    if (!reset_jtag_to_RTI(jtag))
        return false;

    if (JTAG_set_tap_state(jtag, jtag_shf_dr) != ST_OK)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "Unable to set the tap state to ShfDR.");
        return false;
    }

    memset_s(tdo, sizeof(tdo), 0xff, sizeof(tdo));

    // shift empty array, plus our known pattern so that we can hopefully
    // read out all of the idcodes on the target system
    if (JTAG_shift(jtag, shift_size, sizeof(args->tap_data_pattern),
                   (unsigned char*)&args->tap_data_pattern, sizeof(tdo),
                   (unsigned char*)&tdo, jtag_rti) != ST_OK)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "Uncore discovery shift failed.");
        return false;
    }

    // this assumes that the idcodes are byte aligned since the spec says
    // they are 32bits each.
    index = find_pattern(tdo, shift_size, args->tap_data_pattern,
                        sizeof(args->tap_data_pattern)) * 8;
    if (index > 0)
    {
        ASD_log(ASD_LogLevel_Info, stream, option,
                "Found TDI data on TDO after %d bits.", index);
    }
    else
    {
        ASD_log(
            ASD_LogLevel_Warning, stream, option,
            "TDI data was not seen on TDO.  Please ensure the target is on.");
        ASD_log(
            ASD_LogLevel_Warning, stream, option,
            "Here is the first %d bits of data seen on TDO that might help to "
            "debug the problem:",
            shift_size);
        ASD_log_buffer(ASD_LogLevel_Warning, stream, option, tdo,
                       (shift_size / 8), "TDO");
        return false;
    }

    // The number of uncores in the system will be the number of bits in the
    // shiftDR / 32 bits (each id code is 32 bits)
    uncore->numUncores = index / 32;
    ASD_log(ASD_LogLevel_Info, stream, option, "Found %d possible device%s",
            uncore->numUncores, (uncore->numUncores == 1) ? "" : "s");
    for (int i = 0; i < uncore->numUncores; i++)
    {
        sprintf_s(prefix, sizeof(prefix), "Device %d", i );
        ASD_log_shift(ASD_LogLevel_Info, stream, option, 32, 4, &tdo[i * 4],
                      prefix);
    }
    ASD_log_shift(ASD_LogLevel_Info, stream, option, 64, 8,
                  &tdo[uncore->numUncores * 4], "Discovery Overshift");

    // save the idcodes for later comparison
    if (memcpy_s(uncore->idcode, sizeof(uncore->idcode), tdo,
                 4 * uncore->numUncores))
    {
        ASD_log(ASD_LogLevel_Error, ASD_LogStream_JTAG, ASD_LogOption_None,
                "memcpy_s: tdo to uncore->idcode \
				copy buffer failed.");
        return false;
    }

    return true;
}

bool reset_jtag_to_RTI(JTAG_Handler* jtag)
{
    if (JTAG_set_tap_state(jtag, jtag_tlr) != ST_OK)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "Unable to set TLR tap state.");
        return false;
    }

    if (JTAG_set_tap_state(jtag, jtag_rti) != ST_OK)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "Unable to set RTI tap state.");
        return false;
    }
    return true;
}

unsigned int find_pattern(const unsigned char* haystack,
                          unsigned int haystack_size,
                          const unsigned char* needle,
                          unsigned int needle_size)
{
    int cmp = 0;
    for (unsigned int i = 0; i <= (haystack_size - needle_size); i++)
    {
        memcmp_s(&haystack[i], haystack_size - i, needle, needle_size, &cmp);
        if (cmp == 0)
        {
            return i;
        }
    }
    return 0;
}

bool count_jtag_failure(jtag_test_args* args, unsigned int iteration)
{
    if (args->count_mode && continue_loop)
    {
        failures++;
        return true;
    }
    else
        return false;
}

bool apply_pattern(bool pattern_mode, unsigned char* buffer, size_t size, const char* pattern,
                unsigned int iteration, unsigned long long int fixedpattern) {
    unsigned int bit_position = iteration % (size * 8);
    if (!pattern_mode)  //Random Default
    {
        for (size_t i = 0; i < size; i += 4) {
            unsigned int random_value = random();
            for (size_t j = 0; j < 4 && (i + j) < size; j++)
            {
                buffer[i + j] = (random_value >> (j * 8)) & 0xFF;
            }
        }
    }
    else
    {
        if (strcmp(pattern, "Checkerboard") == 0 || strcmp(pattern, "CB") == 0) {
            for (size_t i = 0; i < size; i++) {
                buffer[i] = (i % 2 == 0) ? 0xAA : 0x55;
            }
            return true;
        } else if (strcmp(pattern, "WalkingZero") == 0 || strcmp(pattern, "WZ") == 0) {
            memset(buffer, 0xFF, size);
            buffer[bit_position / 8] &= ~(1 << (7 - (bit_position % 8)));
            return true;
        } else if (strcmp(pattern, "WalkingOne") == 0 || strcmp(pattern, "WO") == 0) {
            memset(buffer, 0x00, size);
            buffer[bit_position / 8] = 1 << (7 - (bit_position % 8));
            return true;
        } else if (strcmp(pattern, "Random") == 0 || strcmp(pattern, "RN") == 0) {
            for (size_t i = 0; i < size; i += 4) {
                unsigned int random_value = rand();
                for (size_t j = 0; j < 4 && (i + j) < size; j++)
                {
                    buffer[i + j] = (random_value >> (j * 8)) & 0xFF;
                }
            }
            return true;
        } else if (strcmp(pattern, "Static") == 0 || strcmp(pattern, "ST") == 0) {
            if (memcpy_s(buffer, size, &fixedpattern, sizeof(fixedpattern)))
            {
                ASD_log(ASD_LogLevel_Error, ASD_LogStream_JTAG, ASD_LogOption_None,
                "memcpy_s: human_readable to tap_data_pattern copy failed.");
                return false;
            }
            return true;
        }
        else
        {
            ASD_log(ASD_LogLevel_Error, stream, option,
                "Unknown Pattern!!");
                return false;
        }
    }
    return true;
}

void inject_error(unsigned char* buffer, size_t size, unsigned int byte_position, unsigned int bit_position) {
    unsigned int bit_index = bit_position % 8;
    char value_before;
    if (byte_position < size)
    {
        value_before = buffer[byte_position];
        buffer[byte_position] ^= (1 << (7 - bit_index));
        ASD_log(ASD_LogLevel_Debug, ASD_LogStream_JTAG, ASD_LogOption_None,
            "Byte at position (idcodes) + %d,  with value %x,  after error injection %x\n",
                    byte_position, value_before, buffer[byte_position]);
    }
    else
    {
        ASD_log(ASD_LogLevel_Error, ASD_LogStream_JTAG, ASD_LogOption_None,
            "Could not inject error!");
    }
}

bool validate_data(unsigned char* buffer1, unsigned int size_buffer1, unsigned char* buffer2,
                unsigned int size_buffer2, unsigned int number_of_bits, unsigned int iterations)
{
    int cmp = 0, to, from;
    memcmp_s(buffer1, size_buffer1, buffer2, (number_of_bits + 7) / 8, &cmp);
    if (cmp != 0)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
            "TAP results comparison failed on iteration %d",
            iterations);
            for (int u = 0; u < number_of_bits; u += 256)
            {
                from = u;
                to = u + 256;
                if (to > number_of_bits) {
                    to = number_of_bits;
                }
                ASD_log(ASD_LogLevel_Error, stream, option,
                    "From: %d, To: %d", from, to);
                ASD_log_shift_to_from(ASD_LogLevel_Error, stream, option,
                              number_of_bits, size_buffer2, buffer2, "Actual  ", from, to);
                ASD_log_shift_to_from(ASD_LogLevel_Error, stream, option,
                              number_of_bits, size_buffer1, buffer1, "Expected", from, to);
            }
        return false;
    }
    return true;
}

bool jtag_test(JTAG_Handler* jtag, uncore_info* uncore, jtag_test_args* args)
{
    unsigned char expected_data[128];
    unsigned char shift_in_data[128];
    unsigned int number_of_bits = 0;
    struct timeval tval_before, tval_after, tval_result, tval_after_loop;
    unsigned int i = 0, j = 0, iterations = 0;
    uint64_t micro_seconds, runtime_seconds;
    unsigned int total_bits = 0;
    unsigned char tdo[MAX_TDO_SIZE];
    size_t ir_size = ((uncore->numUncores * args->ir_shift_size) + 7) / 8;
    unsigned char ir_command[MAX_TDO_SIZE];
    time_t seed = 0;
    bool print_results;
    explicit_bzero(&ir_command, ir_size);

    // set IR command for each uncore found
    for (i = 0; i < uncore->numUncores; i++)
    {
        for (j = 0; j < args->ir_shift_size; j++)
            shift_left((unsigned char*)&ir_command, ir_size);
        // for now we just support 1 byte IR values.
        ir_command[0] = (unsigned char)args->ir_value;
    }

    // build compare data, which we will use to test each iterations success
    explicit_bzero(expected_data, sizeof(expected_data));
    if (memcpy_s(expected_data, sizeof(expected_data), uncore->idcode,
                 4 * uncore->numUncores))
    {
        ASD_log(ASD_LogLevel_Error, ASD_LogStream_JTAG, ASD_LogOption_None,
                "memcpy_s: uncore->idcode to expected_data copy failed.");
        return false;
    }

    if (args->seed != 0) {
        seed = args->seed;
    } else {
         seed = time(NULL);
    }
    srand(seed);
    ASD_log(ASD_LogLevel_Info, stream, option,
        "Using Initial Seed Value of %ld for random patterns", seed);

    gettimeofday(&tval_before, NULL);
    for (iterations = 0; args->loop_forever || iterations < args->numIterations;
         iterations++)
    {
        if (JTAG_set_tap_state(jtag, jtag_shf_ir) != ST_OK)
        {
            ASD_log(ASD_LogLevel_Error, stream, option,
                    "Unable to set the tap state to jtag_shf_ir.");
            if (!count_jtag_failure(args, iterations))
            {
                return false;
            }
        }

        number_of_bits = args->ir_shift_size * uncore->numUncores;

        // ShiftIR and remember to set the end state to something OTHER
        // than ShiftDR since we can't move from shiftIR to ShiftDR
        // directly.
        if (JTAG_shift(jtag, number_of_bits, (unsigned int)ir_size,
                       (unsigned char*)&ir_command, 0, NULL, jtag_rti) != ST_OK)
        {
            ASD_log(ASD_LogLevel_Error, stream, option,
                    "Unable to write IR for idcode.");
            if (!count_jtag_failure(args, iterations))
            {
                return false;
            }
        }
        total_bits += number_of_bits;

        if (JTAG_set_tap_state(jtag, jtag_shf_dr) != ST_OK)
        {
            ASD_log(ASD_LogLevel_Error, stream, option,
                    "Unable to set the tap state to jtag_shf_dr.");
            if (!count_jtag_failure(args, iterations))
            {
                return false;
            }
        }

        explicit_bzero(tdo, sizeof(tdo));
        //number_of_bits = (uncore->numUncores * args->dr_shift_size) +
        //                 (sizeof(args->tap_data_pattern) * 8);

        if(!apply_pattern(args->pattern_mode, args->tap_data_pattern, sizeof(args->tap_data_pattern),
                        args->pattern, iterations, args->human_readable))
        {
            ASD_log(ASD_LogLevel_Error, stream, option,
                "Stopping Execution!");
            print_results = false;
            break;
        }
        memset(shift_in_data, 0x00, sizeof(shift_in_data));
        for (int u = 0; u < sizeof(expected_data); u += 8)
        {
            if ((sizeof(expected_data) - ((4 * uncore->numUncores) + u)) <= 4)
                break;
            if (memcpy_s(&expected_data[(4 * uncore->numUncores)+ u],
                        sizeof(expected_data) - ((4 * uncore->numUncores) + u),
                        args->tap_data_pattern, sizeof(args->tap_data_pattern)))
            {
                ASD_log(ASD_LogLevel_Error, ASD_LogStream_JTAG, ASD_LogOption_None,
                        "memcpy_s: tap_data_pattern to expected_data copy failed.");
                return false;
            }
            if (memcpy_s(&shift_in_data[u], sizeof(shift_in_data) - u,
                        args->tap_data_pattern, sizeof(args->tap_data_pattern)))
            {
                ASD_log(ASD_LogLevel_Error, ASD_LogStream_JTAG, ASD_LogOption_None,
                        "memcpy_s: tap_data_pattern to shift_in_data copy failed.");
                return false;
            }   
        }

        if (args->inject_error == true)
        {
            inject_error(shift_in_data, sizeof(shift_in_data), args->inject_error_byte, 5);  //bit 5 fixed
        }

        number_of_bits = sizeof(shift_in_data)*8; //1024 bits

        if (JTAG_shift(jtag, number_of_bits, sizeof(shift_in_data),
                       (unsigned char*)shift_in_data, sizeof(tdo),
                       (unsigned char*)&tdo, jtag_rti) != ST_OK)
        {
            ASD_log(ASD_LogLevel_Error, stream, option,
                    "Unable to read DR shift data.");
            if (!count_jtag_failure(args, iterations))
            {
                return false;
            }
        }
        total_bits += number_of_bits;

        if (args->manual_mode)
        {
            for (i = 0; i < uncore->numUncores; i++)
            {
                ASD_log_shift(ASD_LogLevel_Info, stream, option,
                              args->dr_shift_size, sizeof(tdo), tdo, "Buffer");
                for (j = 0; j < args->dr_shift_size; j++)
                {
                    shift_right(tdo, sizeof(tdo));
                }
            }

            // print what should be the overshift (deadbeef) pattern
            ASD_log_shift(ASD_LogLevel_Info, stream, option,
                          sizeof(args->tap_data_pattern) * 8, sizeof(tdo), tdo,
                          "Overshift");
        }
        else
        {
            bool result = validate_data(expected_data, sizeof(expected_data),
                            tdo, sizeof(tdo), number_of_bits, iterations);
            if (!result)
            {
                if (!count_jtag_failure(args, iterations))
                {
                    return false;
                }
            }
        }
        print_results = true;
        if (continue_loop == false)
            break;
        gettimeofday(&tval_after_loop, NULL);
        timersub(&tval_after_loop, &tval_before, &tval_result);
        runtime_seconds = (double)tval_result.tv_sec + (double)tval_result.tv_usec / 1000000.0;
        if (runtime_seconds == args->runTime && args->loop_forever)
        {
            break;
        }
        
    } // end iterations loop
    if (!print_results)
        return false;
    ASD_log_shift(ASD_LogLevel_Info, stream, option, 64, 8,
        &tdo[uncore->numUncores * 4], "jtag test Overshift");
    gettimeofday(&tval_after, NULL);
    timersub(&tval_after, &tval_before, &tval_result);
    micro_seconds = ((uint64_t)tval_result.tv_sec * (uint64_t)1000000) +
                    (uint64_t)tval_result.tv_usec;

    print_test_results(iterations, micro_seconds, total_bits);

    return true;
}

void shift_left(unsigned char* buffer, size_t buffer_size)
{
    unsigned char carry = 0;
    unsigned char next = 0;
    for (size_t i = 0; i < buffer_size; i++)
    {
        next = (unsigned char)((buffer[i] & 0x80) ? 1 : 0);
        buffer[i] = (buffer[i] << 1) | carry;
        carry = next;
    }
}

void shift_right(unsigned char* buffer, size_t buffer_size)
{
    unsigned char carry = 0;
    unsigned char next = 0;
    for (size_t i = buffer_size; i > 0; --i)
    {
        next = (unsigned char)((buffer[i - 1] & 1) ? 0x80 : 0);
        buffer[i - 1] = carry | (buffer[i - 1] >> 1);
        carry = next;
    }
}

void print_test_results(uint64_t iterations, uint64_t micro_seconds,
                        uint64_t total_bits)
{
    ASD_log(ASD_LogLevel_Info, stream, option, "Total bits: %llu", total_bits);
    ASD_log(ASD_LogLevel_Info, stream, option, "Seconds elapsed: %f",
            (float)micro_seconds / 1000000);
    if (micro_seconds != 0)
        ASD_log(ASD_LogLevel_Info, stream, option,
                "Throughput: %llu bps (%f mbps)",
                ((uint64_t)1000000 * total_bits) / micro_seconds,
                (float)(((uint64_t)1000000 * total_bits) / micro_seconds) /
                    1000000);
    else
        ASD_log(ASD_LogLevel_Info, stream, option,
                "(measured zero time, could not compute bandwidth)");

    if (failures == 0)
        ASD_log(ASD_LogLevel_Info, stream, option,
                "Successfully finished %llu iteration%s of idcode with %llu "
                "bits of over-shifted data.",
                iterations, iterations > 1 ? "s" : "", total_bits);
    else
        ASD_log(ASD_LogLevel_Info, stream, option,
                "Finished %llu iteration%s of idcode with %llu bits of "
                "over-shifted data. A total of %llu failed",
                iterations, iterations > 1 ? "s" : "", failures, total_bits);
}
