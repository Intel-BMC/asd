/*
Copyright (c) 2023, Intel Corporation

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
#include "spp_test.h"
#include <getopt.h>
#include <safe_mem_lib.h>
#include <safe_str_lib.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <time.h>
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
                                    {0x0E7C5013, IR14_SHIFT_SIZE},
                                    {0x00128113, IR12_SHIFT_SIZE},
                                    {0x00125113, IR16_SHIFT_SIZE},
                                    {0x00138113, IR12_SHIFT_SIZE},
                                    {0x0E7D4113, IR08_SHIFT_SIZE},
                                    {0x0012d113, IR16_SHIFT_SIZE}};

#define MAP_LINE_SIZE 55
char ir_size_map_str[((sizeof(ir_map)/sizeof(ir_shift_size_map)) + 6) * MAP_LINE_SIZE];

#ifndef UNIT_TEST_MAIN
int main(int argc, char** argv)
{
    return spp_test_main(argc, argv);
}
#endif

STATUS spp_test_main(int argc, char** argv)
{
    uncore_info uncore[MAX_SPP_BUS_DEVICES];
    spp_test_args args;
    STATUS result;
    int i3c_fd = -1;
    for (int i = 0; i < MAX_SPP_BUS_DEVICES; i++)
    {
        explicit_bzero(uncore[i].idcode, sizeof(uncore[i].idcode));
        uncore[i].numUncores = 0;
    }
    signal(SIGINT, interrupt_handler); // catch ctrl-c

    ASD_initialize_log_settings(DEFAULT_LOG_LEVEL, DEFAULT_LOG_STREAMS, false,
                                false, NULL, NULL);

    result = parse_arguments(argc, argv, &args);

    if (result == ST_ERR)
    {
        return ST_ERR;
    }

    ASD_initialize_log_settings(args.log_level, args.log_streams, false, false,
                                NULL, NULL);
    SPP_Handler* state = SPPHandler(&args.buscfg);

    if(spp_initialize(state) == ST_OK)
    {
        uint8_t count = 0;
        result = spp_bus_device_count(state, &count);
        if (result == ST_OK)
        {
            ASD_log(ASD_LogLevel_Error, stream, option,
                    "spp_test found %d possible bpk link%s on bus: %d", count,
                    count == 1 ? "" : "s", state->spp_bus);
        }
        else
        {
            ASD_log(ASD_LogLevel_Error, stream, option,
                    "spp_test fail to read spp device count");
        }

        if (result == ST_OK)
        {
            for(uint8_t i = 0; i < count; i++)
            {
                result = spp_device_select(state, i);
                if (result != ST_OK)
                {
                    ASD_log(ASD_LogLevel_Error, stream, option,
                            "spp_test failed to select device: %d", i);
                    break;
                }

                ASD_log(ASD_LogLevel_Error, stream, option,
                        "spp_test running on bus: %d bpk link: %d",
                        state->spp_bus, i);

                if (initialize_bpk(state) == ST_OK)
                {
                    if (configure_bpk(state, &args) == ST_OK)
                    {
                        if (discovery(state, &uncore[i], &args) == ST_OK)
                        {
                            if (reset_jtag_to_rti_spp(state) == ST_OK)
                            {
                                if (spp_test(state, &uncore[i], &args) == ST_OK)
                                {
                                    result = ST_OK;
                                }
                            }
                        }
                    }
                }
                if (disconnect_bpk(state) == ST_ERR)
                {
                    ASD_log(ASD_LogLevel_Error, stream, option,
                            "Failed to disconnect.");
                    result = ST_ERR;
                }
            }
        }
    }
    else
    {
        ASD_log(ASD_LogLevel_Error, stream, option,"spp_test failure!");
        result = ST_ERR;
    }
    spp_deinitialize(state);
    free(state);
    return result;
}

// interrupt handler for ctrl-c
void interrupt_handler(int dummy)
{
    (void)dummy;
    continue_loop = false;
}

STATUS parse_arguments(int argc, char** argv, spp_test_args* args)
{
    int c = 0;
    opterr = 0; // prevent getopt_long from printing shell messages
    failures = 0;
    uint8_t spp_counter = 0; // Up to 8 buses
    // Set Default argument values.
    args->log_level = DEFAULT_LOG_LEVEL;
    args->log_streams = DEFAULT_LOG_STREAMS;
    args->human_readable = DEFAULT_TAP_DATA_PATTERN;
    args->ir_shift_size = 12;
    args->loop_forever = false;
    args->numIterations = DEFAULT_NUMBER_TEST_ITERATIONS;
    args->ir_value = DEFAULT_IR_VALUE;           // overridden in manual mode
    args->dr_shift_size = DEFAULT_DR_SHIFT_SIZE; // overridden in manual mode
    args->manual_mode = DEFAULT_TO_MANUAL_MODE;
    args->count_mode = false;
    args->random_mode = false;
    args->buscfg.default_bus = 0;
    args->buscfg.enable_spp = false;
    args->bpk_values = false;
    enum
    {
        ARG_IR_SIZE = 256,
        ARG_DR_SIZE,
        ARG_IR_VALUE,
        ARG_DR_OVERSHIFT,
        ARG_LOG_LEVEL,
        ARG_LOG_STREAMS,
        ARG_HELP
    };
    struct option opts[] = {
            {"ir-size", 1, NULL, ARG_IR_SIZE},
            {"dr-size", 1, NULL, ARG_DR_SIZE},
            {"ir-value", 1, NULL, ARG_IR_VALUE},
            {"dr-overshift", 1, NULL, ARG_DR_OVERSHIFT},
            {"log-level", 1, NULL, ARG_LOG_LEVEL},
            {"log-streams", 1, NULL, ARG_LOG_STREAMS},
            {"help", 0, NULL, ARG_HELP},
            {NULL, 0, NULL, 0},
    };

    while ((c = getopt_long(argc, argv, "fcrbi:d:?", opts, NULL)) != -1)
    {
        switch (c)
        {
            case 'f':
                args->loop_forever = true;
                break;

            case 'c':
                args->count_mode = true;
                break;

            case 'r':
                args->random_mode = true;
                break;

            case 'b':
                args->bpk_values = true;
                break;

            case 'i':
                if ((args->numIterations = (int)strtol(optarg, NULL, 10)) <= 0)
                {
                    showUsage(argv);
                    return false;
                }
                break;
            case 'd':
            {
                char* pch;
                uint8_t bus;
                uint8_t spp_bus_index = 0;
                bool first_spp = true;
                char* endptr;
                args->buscfg.enable_spp = true;
                pch = strtok(optarg, ",");
                while (pch != NULL)
                {
                    errno = 0;
                    bus = (uint8_t)strtol(pch, &endptr, 10);
                    if ((errno == ERANGE) || (endptr == pch))
                    {
                        fprintf(stderr, "Wrong SPP bus list arguments(-d)\n");
                        break;
                    }
                    if (spp_counter >= MAX_SPP_BUSES)
                    {
                        fprintf(stderr, "Discard SPP bus: %d\n", bus);
                    }
                    else
                    {
                        if (first_spp)
                        {
                            args->buscfg.default_bus = bus;
                            first_spp = false;
                        }
                        fprintf(stderr, "Enabling I3C(SPP) bus: %d\n", bus);
                        spp_bus_index = MAX_IxC_BUSES + spp_counter;
                        args->buscfg.bus_config_type[spp_bus_index] =
                            BUS_CONFIG_SPP;
                        args->buscfg.bus_config_map[spp_bus_index] = bus;
                    }
                    pch = strtok(NULL, ",");
                    spp_counter++;
                }
                break;
            }
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
            case '?':
            case ARG_HELP:
            default:
                showUsage(argv);
                return ST_ERR;
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

    memset_s(args->tap_data_pattern, sizeof(args->tap_data_pattern), 0,
        sizeof(args->tap_data_pattern));
    if (memcpy_s(args->tap_data_pattern, sizeof(args->tap_data_pattern),
                 &args->human_readable, sizeof(args->human_readable)))
    {
        ASD_log(ASD_LogLevel_Error, ASD_LogStream_JTAG, ASD_LogOption_None,
                "memcpy_s: human_readable to tap_data_pattern copy failed.");
        return false;
    }
    return ST_OK;
}

void showUsage(char** argv)
{
    ASD_log(
            ASD_LogLevel_Error, stream, option,
            "\nVersion: %s \n"
            "\nUsage: %s [option]\n\n"
            "  -f          Run endlessly until ctrl-c is used\n"
            "  -c          Complete all iterations and count failing cases\n"
            "  -r          Use random pattern\n"
            "  -b          Read BPK information valus\n"
            "  -i <number> Run [number] of iterations (default: %d)\n"
            "  -d <bus >  Decimal i3c debug(SPP) allowed bus (default: none)\n"
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
            "  --help                     Show this list\n"
            "\n"
            "Examples:\n"
            "\n"
            "Log from the test app and jtag at trace level.\n"
            "     spp_test --log-level=trace --log-streams=test,jtag\n"
            "\n"
            "Read a register, such as SA_TAP_LR_UNIQUEID_CHAIN.\n"
            "     spp_test --ir-value=0x22 --dr-size=0x40\n"
            "\n",
            asd_version,
            argv[0], DEFAULT_NUMBER_TEST_ITERATIONS,
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

STATUS initialize_bpk(SPP_Handler* state)
{
    if(capabilities_ccc(state) == ST_OK)
    {
        if(start_ccc(state, bpk_engine) == ST_OK)
        {
            if(start_debugAction(state) == ST_OK)
            {
                if(select_ccc(state, bpk_engine) == ST_OK)
                {
                    if (cfg_ccc(state, use_interrupt) == ST_OK)
                    {
                        ASD_log(ASD_LogLevel_Info, stream, option,
                                "Baltic Peak is found and initialized.");
                        return ST_OK;
                    }
                }
            }
        }
    }
    ASD_log(ASD_LogLevel_Error, stream, option,
                "Failed to initialize Baltic Peak");
    return ST_ERR;
}

STATUS configure_bpk(SPP_Handler* state, spp_test_args* args)
{
    uint8_t output[BUFFER_SIZE_MAX];
    uint8_t num_bytes;
    if(initialize_sp_engine(state) == ST_ERR)
    {
        return ST_ERR;
    }
    if (args->bpk_values)
        ASD_log(ASD_LogLevel_Info, stream, option,
            "Baltic Peak Information.");
    if(read_sp_config_cmd(state, SP_VERSIONS, output, &num_bytes) == ST_OK)
    {
        if ((num_bytes == 0x4) && (args->bpk_values))
        {
            if (output[1] == 0x01)
                ASD_log(ASD_LogLevel_Info, stream, option,
                    "Baltic Peak using Tiny2 SPP.");
            else
                ASD_log(ASD_LogLevel_Info, stream, option,
                    "Baltic Peak using Tiny1 SPP.");
        }
    }
    if(read_sp_config_cmd(state, SP_SESSION_MGMT_0, output, &num_bytes) == ST_OK)
    {
        if ((num_bytes == 0x4) && (args->bpk_values))
        {
            ASD_log(ASD_LogLevel_Info, stream, option,
                "Session Management ID : %d.", output[0]);
        }
    }
    if(read_sp_config_cmd(state, SP_SESSION_MGMT_1, output, &num_bytes) == ST_OK)
    {
        if ((num_bytes == 0x4) && (args->bpk_values))
        {
            ASD_log(ASD_LogLevel_Info, stream, option,
                "DFX Security Policy: %s", (bool)output[1] ? "true" : "false");
            ASD_log(ASD_LogLevel_Info, stream, option,
                "Debug Capabilities Window State: %s",
                (output[2] & 0x1) ? "true" : "false");
            ASD_log(ASD_LogLevel_Info, stream, option,
                "Debug Capabilities Window Lock State:%s",
                (output[2] & 0x2) ? "true" : "false");
        }
    }
    if(read_sp_config_cmd(state, SP_IDCODE, output, &num_bytes) == ST_OK)
    {
        if ((num_bytes == 0x4) && (args->bpk_values))
        {
            uint32_t manuf_id = array_into_value(output);
            ASD_log(ASD_LogLevel_Info, stream, option,
                "Manufacturing ID: 0x%x", manuf_id);
        }
    }
    if(read_sp_config_cmd(state, SP_PROD_ID, output, &num_bytes) == ST_OK)
    {
        if ((num_bytes == 0x4) && (args->bpk_values))
        {
            uint32_t product_id = array_into_value(output);
            ASD_log(ASD_LogLevel_Info, stream, option,
                "Product ID: 0x%x", product_id);
        }
    }
    if(read_sp_config_cmd(state, SP_CAP_AS_PRESENT, output, &num_bytes) == ST_OK)
    {
        if ((num_bytes == 0x4) && (args->bpk_values))
        {
            bool jtag_access_implemented = (bool)(output[0] & 0x1);
            ASD_log(ASD_LogLevel_Info, stream, option,
                "Jtag Access Space implemented in BPK: %s",
                jtag_access_implemented ? "true" : "false");
        }
    }
    if(write_sp_config_cmd(state, SP_AS_AVAIL_REQ_SET, JTAG_SET, output,
        &num_bytes) == ST_ERR)
    {
        ASD_log(ASD_LogLevel_Info, stream, option,
                "Failure Requesting Jtag Space");
        return ST_ERR;
    }
    if(read_sp_config_cmd(state, SP_AS_AVAIL_STAT, output, &num_bytes) == ST_OK)
    {
        if ((num_bytes == 0x4) && (args->bpk_values))
        {
            bool jtag_available = (bool)(output[0] & 0x1);
            ASD_log(ASD_LogLevel_Info, stream, option,
                "Jtag Access Space Available in BPK:  %s",
                jtag_available ? "true" : "false");
        }
    }
    if(write_sp_config_cmd(state, SP_AS_EN_SET, JTAG_SET, output,
        &num_bytes) == ST_ERR)
    {
        ASD_log(ASD_LogLevel_Info, stream, option,
                "Failure Enabling Jtag Space");
        return ST_ERR;
    }
    if(read_sp_config_cmd(state, SP_AS_EN_STAT, output, &num_bytes) == ST_OK)
    {
        if ((num_bytes == 0x4) && (args->bpk_values))
        {
            bool jtag_enabled= (bool)(output[0] & 0x1);
            ASD_log(ASD_LogLevel_Info, stream, option,
                "Jtag Access Space Enabled in BPK: %s",
                jtag_enabled ? "true" : "false");
        }
    }
    return ST_OK;
}

STATUS disconnect_bpk(SPP_Handler* state)
{
    uint8_t output[BUFFER_SIZE_MAX];
    uint8_t num_bytes;
    if(write_sp_config_cmd(state, SP_AS_EN_CLEAR, CLEAR_ALL, output,
                            &num_bytes) == ST_ERR)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "Failure writing to configuration space");
        return ST_ERR;
    }
    if(read_sp_config_cmd(state, SP_AS_EN_STAT, output, &num_bytes) == ST_OK)
    {
        if (num_bytes == 0x4)
        {
            ASD_log(ASD_LogLevel_Info, stream, option,
                    "Status: %s",
                    (output[0] & 0x1) ? "disconnected" : "connected");
        }
    }

    return ST_OK;
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

STATUS discovery(SPP_Handler* state, uncore_info* uncore, spp_test_args* args)
{
    unsigned int shift_size = UNCORE_DISCOVERY_SHIFT_SIZE_IN_BITS;
    char prefix[32];
    unsigned int index = 0;
    unsigned char tdo[MAX_TDO_SIZE];
    if(reset_jtag_to_rti_spp(state) == ST_ERR)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
            "Uncore discovery shift failed.");
        return ST_ERR;
    }
    memset_s(tdo, sizeof(tdo), 0xff, sizeof(tdo));

    // shift empty array, plus our known pattern so that we can hopefully
    // read out all of the idcodes on the target system
    if (jtag_shift_spp(state, jtag_shf_dr, shift_size, sizeof(args->tap_data_pattern),
                       (unsigned char*)&args->tap_data_pattern, sizeof(tdo),
                       (unsigned char*)&tdo, jtag_rti) != ST_OK)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "Uncore discovery shift failed.");
        return ST_ERR;
    }
    index = find_pattern(tdo, shift_size, args->tap_data_pattern,
        sizeof(args->tap_data_pattern)-4) * 8;
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
        return ST_ERR;
    }
    // The number of uncores in the system will be the number of bits in the
    // shiftDR / 32 bits (each id code is 32 bits)
    uncore->numUncores = index / 32;
    int ia[1];
    ASD_log(ASD_LogLevel_Info, stream, option,
            "Found %d possible device%s on bus: %d bpk device link:%d",
            uncore->numUncores, (uncore->numUncores == 1) ? "" : "s",
            state->spp_bus, state->device_index);
    for (int i = 0; i < uncore->numUncores; i++)
    {
        ia[0] = i;
        sprintf_s(prefix, sizeof(prefix), "Device %d", i, 1);
        ASD_log_shift(ASD_LogLevel_Info, stream, option, 32, 4, &tdo[i * 4],
                      prefix);
    }
    ASD_log_shift(ASD_LogLevel_Info, stream, option, 64, 8,
                  &tdo[uncore->numUncores * 4], "Overshift");

    // save the idcodes for later comparison
    if (memcpy_s(uncore->idcode, sizeof(uncore->idcode), tdo,
                 4 * uncore->numUncores))
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "memcpy_s: tdo to uncore->idcode \
				copy buffer failed.");
        return ST_ERR;
    }

    return ST_OK;
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


STATUS spp_test(SPP_Handler* state, uncore_info* uncore, spp_test_args* args)
{
    unsigned char compare_data[MAX_TAPS_SUPPORTED * SIZEOF_ID_CODE + 8];
    unsigned int number_of_bits = 0;
    struct timeval tval_before, tval_after, tval_result;
    unsigned int i = 0, j = 0, iterations = 0;
    uint64_t micro_seconds;
    unsigned int total_bits = 0;
    unsigned char tdo[MAX_TDO_SIZE];
    size_t ir_size = 2;
    unsigned char ir_command[MAX_TDO_SIZE];
    int cmp = 0;
    int* random_pattern;
    explicit_bzero(&ir_command, ir_size);

    // set IR command for each uncore found
    ir_command[0] = (unsigned char)2;

    // build compare data, which we will use to test each iterations success
    explicit_bzero(compare_data, sizeof(compare_data));
    if (memcpy_s(compare_data, sizeof(compare_data), uncore->idcode,
                 4 ))
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "memcpy_s: uncore->idcode to compare_data copy failed.");
        return false;
    }
    // Init random engine
    if (args->random_mode)
    {
        srand(time(NULL));
    }
    else
    {
        if (memcpy_s(&compare_data[4],
                     sizeof(compare_data) - 4,
                     args->tap_data_pattern, sizeof(args->tap_data_pattern)-4))
        {
            ASD_log(ASD_LogLevel_Error, stream, option,
                    "memcpy_s: tap_data_pattern to compare_data copy failed.");
            return false;
        }
    }

    gettimeofday(&tval_before, NULL);

    for (iterations = 0; args->loop_forever || iterations < args->numIterations;
         iterations++)
    {
        number_of_bits = args->ir_shift_size;

        // ShiftIR and remember to set the end state to something OTHER
        // than ShiftDR since we can't move from shiftIR to ShiftDR
        // directly.
        if (jtag_shift_spp(state, jtag_shf_ir, number_of_bits, (unsigned int)ir_size,
                           (unsigned char*)&ir_command, sizeof(tdo),
                           (unsigned char*)&tdo, jtag_rti) != ST_OK)
        {
            ASD_log(ASD_LogLevel_Error, stream, option,
                    "Unable to write IR for idcode.");
            return ST_ERR;
        }
        total_bits += number_of_bits;

        explicit_bzero(tdo, sizeof(tdo));

        number_of_bits = (args->dr_shift_size) + ((sizeof(args->tap_data_pattern)-4) * 8);
        // Generate a pattern in random mode
        if (args->random_mode)
        {
            random_pattern = (int*)args->tap_data_pattern;
            random_pattern[0] = rand();
            random_pattern[1] = rand();
            if (memcpy_s(&compare_data[4],
                         sizeof(compare_data) - 4,
                         args->tap_data_pattern,
                         sizeof(args->tap_data_pattern)-4))
            {
                ASD_log(ASD_LogLevel_Error, stream, option,
                        "memcpy_s: random data to compare_data copy failed.");
                return ST_ERR;
            }
        }

        if (jtag_shift_spp(state, jtag_shf_dr, number_of_bits,
                           sizeof(args->tap_data_pattern),
                           (unsigned char*)args->tap_data_pattern, sizeof(tdo),
                           (unsigned char*)&tdo, jtag_rti) != ST_OK)
        {
            ASD_log(ASD_LogLevel_Error, stream, option,
                    "Unable to read DR shift data.");
            return ST_ERR;
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
            ASD_log_shift(ASD_LogLevel_Info, stream, option,
                          sizeof(args->tap_data_pattern) * 8, sizeof(tdo), tdo,
                          "Overshift");
        }
        else
        {
            memcmp_s(compare_data, sizeof(compare_data), tdo,
                     (number_of_bits + 7) / 8, &cmp);
            if (cmp != 0)
            {
                ASD_log(ASD_LogLevel_Error, stream, option,
                        "TAP results comparison failed on iteration %d",
                        iterations);
                ASD_log_shift(ASD_LogLevel_Error, stream, option,
                              number_of_bits, sizeof(tdo), tdo, "Actual");
                ASD_log_shift(ASD_LogLevel_Error, stream, option,
                              number_of_bits, sizeof(compare_data),
                              compare_data, "Expected");
                return ST_ERR;
            }
        }
        if (continue_loop == false)
            break;
    } // end iterations loop

    gettimeofday(&tval_after, NULL);
    timersub(&tval_after, &tval_before, &tval_result);
    micro_seconds = ((uint64_t)tval_result.tv_sec * (uint64_t)1000000) +
                    (uint64_t)tval_result.tv_usec;

    print_test_results(iterations, micro_seconds, total_bits, failures);

    return true;
}

void print_test_results(uint64_t iterations, uint64_t micro_seconds,
                        uint64_t total_bits, uint64_t failures)
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
                "Successfully finished %llu iteration%s of idcode with 64 "
                "bits of over-shifted data.",
                iterations, iterations > 1 ? "s" : "");
    else
        ASD_log(ASD_LogLevel_Info, stream, option,
                "Finished %llu iteration%s of idcode with 64 bits of "
                "over-shifted data. A total of %llu failed",
                iterations, iterations > 1 ? "s" : "", failures);
}

STATUS spp_packet_check(uint16_t read_len, uint8_t * read_buffer,
                        uint16_t write_len, uint8_t* write_buffer)
{
    if (read_len > 1 && write_len > 0)
    {
        if (write_buffer[0] == read_buffer[0])
        {
            if (read_buffer[1] == 0)
            {
                return ST_OK;
            }
        }
    }
    ASD_log(ASD_LogLevel_Error, ASD_LogStream_SPP, option,
            "Failed spp_packet_check");
    return ST_ERR;
}

STATUS capabilities_ccc(SPP_Handler* state)
{
    uint8_t write_buffer[BUFFER_SIZE_MAX] = {0};
    uint8_t output[BUFFER_SIZE_MAX] = {0};
    uint16_t write_len = 1;
    write_buffer[0] = 0x0;
    uint16_t read_len = 4;
    if (spp_send_receive_cmd(state, BpkOpcode, write_len,
                            write_buffer, &read_len, output) == ST_OK)
    {
        if (output[0] == 0x10 && output[1] == 0x10
            && output[2] == 0x31 && output[3] == 0x42)
        {
            return ST_OK;
        }
    }
    ASD_log(ASD_LogLevel_Error, ASD_LogStream_SPP, option,
            "Failed capabilities_ccc");
    return ST_ERR;
}

STATUS start_ccc(SPP_Handler* state, uint8_t comportIndex)
{
    uint8_t write_buffer[BUFFER_SIZE_MAX] = {0};
    uint8_t output[BUFFER_SIZE_MAX] = {0};
    uint16_t write_len = 2;
    write_buffer[0] = 0x2;
    write_buffer[1] = comportIndex;
    if(spp_send_cmd(state, BpkOpcode, write_len, write_buffer) == ST_OK)
    {
        write_len = 1;
        write_buffer[0] = 0x2;
        uint16_t read_len = 4;
        if (spp_send_receive_cmd(state, BpkOpcode, write_len,
                                write_buffer, &read_len, output) == ST_OK)
        {
            if (output[0] == 0x2b && output[1] == 0x0
                && output[2] == 0x0 && output[3] == 0x0)
            {
                return ST_OK;
            }
        }
    }
    ASD_log(ASD_LogLevel_Error, ASD_LogStream_SPP, option,
            "Failed start_ccc");
    return ST_ERR;
}

STATUS start_debugAction(SPP_Handler* state)
{
    uint8_t write_buffer[BUFFER_SIZE_MAX] = {0};
    uint8_t output[BUFFER_SIZE_MAX] = {0};
    uint16_t write_len = 1;
    write_buffer[0] = 0xFD;
    uint8_t *read_len = 0;
    if (spp_send_cmd(state, DebugAction, write_len, write_buffer) == ST_OK)
    {
        return ST_OK;
    }
    ASD_log(ASD_LogLevel_Error, ASD_LogStream_SPP, option,
            "Failed DebugAction Start");
    return ST_ERR;
}

STATUS select_ccc(SPP_Handler* state, uint8_t comportIndex)
{
    uint8_t write_buffer[BUFFER_SIZE_MAX] = {0};
    uint8_t output[BUFFER_SIZE_MAX] = {0};
    uint16_t write_len = 2;
    write_buffer[0] = 0x6;
    write_buffer[1] = comportIndex;
    if(spp_send_cmd(state, BpkOpcode, write_len, write_buffer) == ST_OK)
    {
        write_len = 1;
        write_buffer[0] = 0x6;
        uint16_t read_len = 1;
        if (spp_send_receive_cmd(state, BpkOpcode, write_len,
                                write_buffer, &read_len, output) == ST_OK)
        {
            if (output[0] == 0x0 && output[1] == 0x0
                && output[2] == 0x0 && output[3] == 0x0)
            {
                return ST_OK;
            }
            
        }
    }
    ASD_log(ASD_LogLevel_Error, ASD_LogStream_SPP, option,
            "Failed Select CCC");
    return ST_ERR;
}

STATUS cfg_ccc(SPP_Handler* state, uint8_t int_type)
{
    uint8_t write_buffer[BUFFER_SIZE_MAX] = {0};
    uint8_t output[BUFFER_SIZE_MAX] = {0};
    uint16_t write_len = 2;
    write_buffer[0] = 0x1;
    write_buffer[1] = int_type;
    if(spp_send_cmd(state, BpkOpcode, write_len, write_buffer) == ST_OK)
    {
        return ST_OK;
    }
    ASD_log(ASD_LogLevel_Error, ASD_LogStream_SPP, option,
            "Failed Cfg CCC");
    return ST_ERR;
}

STATUS initialize_sp_engine(SPP_Handler* state)
{
    uint8_t payload[BUFFER_SIZE_MAX] = {0};
    uint8_t read_data[BUFFER_SIZE_MAX] = {0};
    uint8_t output[BUFFER_SIZE_MAX] = {0};
    uint16_t read_len = 0;
    struct bpk_cmd bpk_cmd = {0};
    bpk_cmd.bpk_opcode = InitializeSPEngine;
    uint8_t payload_size = spp_generate_payload(bpk_cmd, (uint8_t*)&payload);
    if(spp_send(state, payload_size, payload) == ST_OK)
    {
        if(spp_receive(state, &read_len, read_data) == ST_OK)
        {
            if(spp_packet_check(read_len, read_data,
                                payload_size, payload) == ST_OK)
            {
                read_len = decode_rx_packet(read_len, read_data, output);
                    ASD_log(ASD_LogLevel_Debug, ASD_LogStream_SPP, option,
                        "read_len %d", read_len);
                    ASD_log_buffer(ASD_LogLevel_Debug,
                                    ASD_LogStream_SPP, ASD_LogOption_None,
                            output, read_len, "[IN]");
                if (read_len == 8)
                {
                    if (output[0] == 0x11 && output[1] == 0xee
                        && output[2] == 0x77 && output[3] == 0x44
                        && output[4] == 0xa5 && output[5] == 0xc3
                        && output[6] == 0xc3 && output[7] == 0xa5)
                    {
                        return ST_OK;
                    }
                }
            }
        }
    }
    ASD_log(ASD_LogLevel_Error, ASD_LogStream_SPP, option,
            "Failed initialize_sp_engine");
    return ST_ERR;
}

STATUS read_sp_config_cmd(SPP_Handler* state, uint32_t address,
                            uint8_t* output, uint8_t* read_len)
{
    uint8_t payload[BUFFER_SIZE_MAX] = {0};
    uint8_t read_data[BUFFER_SIZE_MAX] = {0};
    struct bpk_cmd bpk_cmd = {0};
    bpk_cmd.bpk_opcode = Opcode_ReadSPConfig;
    bpk_cmd.address = address;
    uint8_t payload_size = spp_generate_payload(bpk_cmd, (uint8_t*)&payload);
    if(spp_send(state, payload_size, payload) == ST_OK)
    {
        if(spp_receive(state, (uint16_t *)read_len, read_data) == ST_OK)
        {
            if(spp_packet_check(*read_len, read_data, payload_size, payload) == ST_OK)
            {
                *read_len = decode_rx_packet(*read_len, read_data, output);
                return ST_OK;
            }
        }
    }
    ASD_log(ASD_LogLevel_Error, ASD_LogStream_SPP, option,
            "Failed read_sp_config_cmd, address: 0x%x\n ", address);
    return ST_ERR;
}

STATUS write_sp_config_cmd(SPP_Handler* state, uint32_t address,
                        uint32_t write_value, uint8_t* output, uint8_t* read_len)
{
    uint8_t payload[BUFFER_SIZE_MAX] = {0};
    uint8_t read_data[BUFFER_SIZE_MAX] = {0};
    uint32_t write_payload[1] = {0};
    struct bpk_cmd bpk_cmd = {0};
    bpk_cmd.bpk_opcode = Opcode_WriteSPConfig;
    bpk_cmd.address = address;
    write_payload[0] = write_value;
    bpk_cmd.data = write_payload;
    bpk_cmd.data_size = 1;
    uint8_t payload_size = spp_generate_payload(bpk_cmd, (uint8_t*)&payload);
    if(spp_send(state, payload_size, payload) == ST_OK)
    {
        if(spp_receive(state, (uint16_t *)read_len, read_data) == ST_OK)
        {
            if(spp_packet_check(*read_len, read_data,
                                payload_size, payload) == ST_OK)
            {
                *read_len = decode_rx_packet(*read_len, read_data, output);
                return ST_OK;
            }
        }
    }
    ASD_log(ASD_LogLevel_Error, ASD_LogStream_SPP, option,
            "Failed write_sp_config_cmd, address: 0x%x\n ", address);
    return ST_ERR;
}

STATUS write_system_cmd(SPP_Handler* state, struct jtag_cmd jtag,
                        uint8_t* output, uint8_t* read_len)
{
    uint8_t payload[BUFFER_SIZE_MAX] = {0};
    uint8_t read_data[BUFFER_SIZE_MAX] = {0};
    uint32_t write_payload[1] = {0};
    struct bpk_cmd bpk_cmd = {0};
    bpk_cmd.bpk_opcode = Opcode_WriteSystem;
    bpk_cmd.address = 0;
    bpk_cmd.next_state = jtag.next_state;
    bpk_cmd.gtu = 0;
    bpk_cmd.tif = fill_tdi_zero;
    bpk_cmd.bfc = 0;
    bpk_cmd.shift = jtag.shift;
    bpk_cmd.tranByteCount = jtag.size_of_payload;
    bpk_cmd.data = jtag.payload;
    bpk_cmd.data_size = jtag.size_of_payload;
    uint8_t payload_size = spp_generate_payload(bpk_cmd, (uint8_t*)&payload);
    if(spp_send(state, payload_size, payload) == ST_OK)
    {
        if(spp_receive(state, (uint16_t *)read_len, read_data) == ST_OK)
        {
            if(spp_packet_check(*read_len, read_data, payload_size, payload) == ST_OK)
            {
                *read_len = decode_rx_packet(*read_len, read_data, output);
                return ST_OK;
            }
        }
    }
    ASD_log(ASD_LogLevel_Error, ASD_LogStream_SPP, option,
            "Failed write_system_cmd");
    return ST_ERR;
}

STATUS write_read_system_cmd(SPP_Handler* state, struct jtag_cmd jtag,
                            uint8_t* output, uint8_t* read_len)
{
    uint8_t payload[BUFFER_SIZE_MAX] = {0};
    uint8_t read_data[BUFFER_SIZE_MAX] = {0};
    struct bpk_cmd bpk_cmd = {0};
    bpk_cmd.bpk_opcode = Opcode_WriteReadSystem;
    bpk_cmd.address = 0;
    bpk_cmd.next_state = jtag.next_state;
    bpk_cmd.gtu = jtag.gtu;
    bpk_cmd.tif = jtag.tif;
    bpk_cmd.bfc = jtag.bfc;
    bpk_cmd.shift = jtag.shift;
    bpk_cmd.tranByteCount = jtag.size_of_payload;
    bpk_cmd.data = jtag.payload;
    bpk_cmd.data8 = jtag.payload8;
    bpk_cmd.data_size = jtag.size_of_payload / 4;
    uint8_t payload_size = spp_generate_payload(bpk_cmd, (uint8_t*)&payload);
    if(spp_send(state, payload_size, payload) == ST_OK)
    {
        if(spp_receive(state, (uint16_t *)read_len, read_data) == ST_OK)
        {
            if(spp_packet_check(*read_len, read_data, payload_size, payload) == ST_OK)
            {
                *read_len = decode_rx_packet(*read_len, read_data, output);
                return ST_OK;
            }
        }
    }
    ASD_log(ASD_LogLevel_Error, ASD_LogStream_SPP, option,
            "Failed write_read_system_cmd");
    return ST_ERR;
}

STATUS reset_jtag_to_rti_spp(SPP_Handler* state)
{
    struct jtag_cmd jtag = {0};
    uint8_t output[BUFFER_SIZE_MAX] = {0};
    uint8_t num_bytes;
    jtag.next_state =jtag_tlr;
    jtag.shift = 0xa;
    jtag.size_of_payload = 0;
    jtag.tif = fill_tdi_zero;
    jtag.bfc = 0;
    jtag.gtu = 0;
    if (write_system_cmd(state, jtag, output, &num_bytes) == ST_OK)
    {
        jtag.next_state =jtag_rti;
        jtag.shift = 0x6;
        jtag.size_of_payload = 0;
        jtag.tif = fill_tdi_zero;
        jtag.bfc = 0;
        jtag.gtu = 0;
        if (write_system_cmd(state,  jtag, output, &num_bytes) == ST_OK)
        {
            return ST_OK;
        }
    }
    ASD_log(ASD_LogLevel_Error, ASD_LogStream_SPP, option,
            "Failed reset_jtag_to_rti_spp");
    return ST_ERR;
}

STATUS jtag_shift_spp(SPP_Handler* state, enum jtag_states next_state,
                      unsigned int number_of_bits,
                      unsigned int input_bytes, unsigned char* input,
                      unsigned int output_bytes, unsigned char* output,
                      enum jtag_states end_tap_state)
{
    struct jtag_cmd jtag = {0};
    jtag.next_state =next_state;
    jtag.shift = number_of_bits;
    jtag.size_of_payload = input_bytes; //bytes
    jtag.tif = data_for_tdi;
    jtag.bfc = 0;
    jtag.gtu = 0;
    jtag.payload = (uint32_t *)input;
    jtag.payload8 = input;
    uint8_t num_bytes;
    if(write_read_system_cmd(state, jtag, output, &num_bytes) == ST_OK)
    {
        output_bytes = num_bytes;
        return ST_OK;
    }
    ASD_log(ASD_LogLevel_Error, ASD_LogStream_SPP, option,
            "Failed jtag_shift_spp");
    return ST_OK;
}

struct tinySppCommandPacket tiny_spp_header_builder(enum bpk_opcode op,
                    uint8_t* size, uint8_t tranByteCount)
{
    struct tinySppCommandPacket cmd = { 0 };
    switch(op)
    {
        case Nop:
            cmd.detailed.version = TinySPP_Version;
            cmd.detailed.opcode = Opcode_Nop;
            cmd.detailed.accessSpace = 0;
            cmd.detailed.continueOnFault = 0;
            cmd.detailed.sendResponseImmediately = 1;
            cmd.detailed.lastCommandPacket = 1;
            cmd.detailed.action = 0;
            cmd.detailed.tranByteCount = 0;
            cmd.detailed.spconf_addr = 0;
            cmd.detailed.addr = 0;
            cmd.detailed.payload0 = 0x8877EE11;
            cmd.detailed.payload1 = 0xA5C3C3A5;
            *size = 12;
            break;
        case InitializeSPEngine:
            cmd.detailed.version = TinySPP_Version;
            cmd.detailed.opcode = Opcode_InitializeSPEngine;
            cmd.detailed.accessSpace = 0;
            cmd.detailed.continueOnFault = 0;
            cmd.detailed.sendResponseImmediately = 1;
            cmd.detailed.lastCommandPacket = 1;
            cmd.detailed.action = 0;
            cmd.detailed.tranByteCount = 0;
            cmd.detailed.spconf_addr = 0;
            cmd.detailed.addr = 0;
            cmd.detailed.payload0 = 0x8877EE11;
            cmd.detailed.payload1 = 0xA5C3C3A5;
            *size = 12;
            break;
        case UnblockSPEngine:
            cmd.detailed.version = TinySPP_Version;
            cmd.detailed.opcode = Opcode_UnblockSPEEngine;
            cmd.detailed.accessSpace = 0;
            cmd.detailed.continueOnFault = 0;
            cmd.detailed.sendResponseImmediately = 1;
            cmd.detailed.lastCommandPacket = 1;
            cmd.detailed.action = 0;
            cmd.detailed.tranByteCount = 0;
            cmd.detailed.spconf_addr = 0;
            cmd.detailed.addr = 0;
            cmd.detailed.payload0 = 0x8877EE11;
            cmd.detailed.payload1 = 0xA5C3C3A5;
            *size = 12;
            break;
        case ReadSPConfig:
            cmd.detailed.version = TinySPP_Version;
            cmd.detailed.opcode = Opcode_ReadSPConfig;
            cmd.detailed.accessSpace = 0;
            cmd.detailed.continueOnFault = 0;
            cmd.detailed.sendResponseImmediately = 1;
            cmd.detailed.lastCommandPacket = 1;
            cmd.detailed.action = 0;
            cmd.detailed.tranByteCount = 4;
            cmd.detailed.spconf_addr = 0;
            cmd.detailed.addr = 0;
            cmd.detailed.payload0 = 0;
            cmd.detailed.payload1 = 0;
            *size = 4;
            break;
        case WriteSPConfig:
            cmd.detailed.version = TinySPP_Version;
            cmd.detailed.opcode = Opcode_WriteSPConfig;
            cmd.detailed.accessSpace = 0;
            cmd.detailed.continueOnFault = 0;
            cmd.detailed.sendResponseImmediately = 1;
            cmd.detailed.lastCommandPacket = 1;
            cmd.detailed.action = 0;
            cmd.detailed.tranByteCount = 4;
            cmd.detailed.spconf_addr = 0;
            cmd.detailed.addr = 0;
            cmd.detailed.payload0 = 0;
            cmd.detailed.payload1 = 0;
            *size = 4;
            break;
        case ReadSystem:
            cmd.detailed.version = TinySPP_Version;
            cmd.detailed.opcode = Opcode_ReadSystem;
            cmd.detailed.accessSpace = 0;
            cmd.detailed.continueOnFault = 0;
            cmd.detailed.sendResponseImmediately = 1;
            cmd.detailed.lastCommandPacket = 1;
            cmd.detailed.action = 0;
            cmd.detailed.tranByteCount = 4;
            cmd.detailed.spconf_addr = 0;
            cmd.detailed.addr = 0;
            cmd.detailed.payload0 = 0;
            cmd.detailed.payload1 = 0;
            *size = 4;
            break;
        case WriteSystem:
            cmd.detailed.version = TinySPP_Version;
            cmd.detailed.opcode = Opcode_WriteSystem;
            cmd.detailed.accessSpace = 0;
            cmd.detailed.continueOnFault = 0;
            cmd.detailed.sendResponseImmediately = 1;
            cmd.detailed.lastCommandPacket = 1;
            cmd.detailed.action = 0;
            cmd.detailed.tranByteCount = tranByteCount;
            cmd.detailed.spconf_addr = 0;
            cmd.detailed.addr = 0;
            cmd.detailed.payload0 = 0;
            cmd.detailed.payload1 = 0;
            *size = 4;
            break;
        case WriteReadSystem:
            cmd.detailed.version = TinySPP_Version;
            cmd.detailed.opcode = Opcode_WriteReadSystem;
            cmd.detailed.accessSpace = 0;
            cmd.detailed.continueOnFault = 0;
            cmd.detailed.sendResponseImmediately = 1;
            cmd.detailed.lastCommandPacket = 1;
            cmd.detailed.action = 0;
            cmd.detailed.tranByteCount = tranByteCount;
            cmd.detailed.spconf_addr = 0;
            cmd.detailed.addr = 0;
            cmd.detailed.payload0 = 0;
            cmd.detailed.payload1 = 0;
            *size = 4;
            break;
        case Loop:
            cmd.detailed.version = TinySPP_Version;
            cmd.detailed.opcode = Opcode_LoopTrigSystem;
            cmd.detailed.accessSpace = 0;
            cmd.detailed.continueOnFault = 0;
            cmd.detailed.sendResponseImmediately = 1;
            cmd.detailed.lastCommandPacket = 1;
            cmd.detailed.action = 0;
            cmd.detailed.tranByteCount = 4;
            cmd.detailed.spconf_addr = 0;
            cmd.detailed.addr = 0;
            cmd.detailed.payload0 = 0;
            cmd.detailed.payload1 = 0;
            *size = 4;
            break;
    }
    return cmd;
}

uint8_t spp_generate_payload(struct bpk_cmd bpk_cmd, uint8_t* payload)
{
    uint8_t header_size = 0;
    struct tinySppCommandPacket sppCmdTX = {0};
    sppCmdTX = tiny_spp_header_builder(bpk_cmd.bpk_opcode, &header_size,
                    bpk_cmd.tranByteCount);
    int *ptr = (int*)&sppCmdTX;
    const unsigned char *byte;
    int size = header_size;
    int u = 0;
    for ( byte = (const unsigned char*)ptr; size--; ++byte )
    {
        payload[u] = *byte;
        u++;
    }
    if (bpk_cmd.bpk_opcode == ReadSPConfig)
    {
        payload[u++] = bpk_cmd.address & 0xFF;
        payload[u++] = (bpk_cmd.address >> 8 )& 0xFF;
        payload[u++] = (bpk_cmd.address >> 16 )& 0xFF;
        payload[u++] = (bpk_cmd.address >> 24 )& 0xFF;
    }
    if (bpk_cmd.bpk_opcode == WriteSPConfig)
    {
        payload[u++] = bpk_cmd.address & 0xFF;
        payload[u++] = (bpk_cmd.address >> 8 )& 0xFF;
        payload[u++] = (bpk_cmd.address >> 16 )& 0xFF;
        payload[u++] = (bpk_cmd.address >> 24 )& 0xFF;
        for (ssize_t n = 0; n < bpk_cmd.data_size; n++)
        {
            int value = *(bpk_cmd.data + n);
            payload[u++] = value & 0xFF;
            payload[u++] = (value >> 8) & 0xFF;
            payload[u++] = (value >> 16) & 0xFF;
            payload[u++] = (value >> 24) & 0xFF;
        }
    }
    if (bpk_cmd.bpk_opcode == WriteSystem)
    {
        struct jtagSppCommandPacket jtag = {0};
        jtag.next_state = bpk_cmd.next_state;
        jtag.bfc = bpk_cmd.bfc;
        jtag.gtu = bpk_cmd.gtu;
        jtag.shift_length = bpk_cmd.shift;
        jtag.tdi_in = bpk_cmd.tif;
        int* ptr = (int*)&jtag;
        const unsigned char* byte;
        int size = 4;
        int value;
        for ( byte = (const unsigned char*)ptr; size--; ++byte )
        {
            payload[u] = *byte;
            u++;
        }
    }
    if (bpk_cmd.bpk_opcode == WriteReadSystem)
    {
        struct jtagSppCommandPacket jtag = {0};
        jtag.next_state = bpk_cmd.next_state;
        jtag.bfc = bpk_cmd.bfc;
        jtag.gtu = bpk_cmd.gtu;
        jtag.shift_length = bpk_cmd.shift;
        jtag.tdi_in = bpk_cmd.tif;
        int* ptr = (int*)&jtag;
        const unsigned char* byte;
        int size = 4;
        int value;
        for ( byte = (const unsigned char*)ptr; size--; ++byte )
        {
            payload[u] = *byte;
            u++;
        }
        for (uint8_t i=0; i < bpk_cmd.tranByteCount; i++)
        {
            payload[u++] = bpk_cmd.data8[i];
        }
    }
    return u;
}


uint16_t decode_rx_packet(ssize_t payload_size, uint8_t* payload, uint8_t* output)
{
    struct tinySppCommandPacketReceive receive = {0};
    char operation[30] = {0};
    char errorType[30] = {0};
    uint8_t i;
    uint16_t output_bytes;
    for (i = 0; i < HEADER_SIZE; i++)
    {
        receive.buffer.Rxbuffer[i] = payload[i];
    }
    payload_size = payload_size - HEADER_SIZE;
    for (output_bytes=0; output_bytes < payload_size; output_bytes++)
    {
        output[output_bytes] = payload[i++];
    }
    return output_bytes;
}

int32_t array_into_value(uint8_t* buffer)
{
    int32_t value = 0;
    value |= (int32_t) buffer[0] << 24;
    value |= (int32_t) buffer[1] << 16;
    value |= (int32_t) buffer[2] <<  8;
    value |= (int32_t) buffer[3];
    return value;
}