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
#ifndef _I3C_DBG_TEST_H_
#define _I3C_DBG_TEST_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include "jtag_handler.h"
#include "logging.h"
#include <errno.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <stdint.h>
#include <safe_mem_lib.h>
#include <safe_str_lib.h>
#include "spp_handler.h"

#define MAX_TAPS_SUPPORTED 1
#define MAX_TDO_SIZE 2048
#define BUFFER_SIZE_MAX 255
#define DEFAULT_TAP_DATA_PATTERN 0xdeadbeefbad4f00d // for tap comparison
#define SIZEOF_TAP_DATA_PATTERN 8
#define IR08_SHIFT_SIZE 8        // 8  bits per uncore
#define DEFAULT_IR_SHIFT_SIZE 11 // 11 bits per uncore
#define IR12_SHIFT_SIZE 12       // 12 bits per uncore
#define IR14_SHIFT_SIZE 14       // 14 bits per uncore
#define IR16_SHIFT_SIZE 16       // 16 bits per uncore
#define MAX_IR_SHIFT_SIZE 0x400
#define DEFAULT_NUMBER_TEST_ITERATIONS 11500
#define DEFAULT_RUNTIME 1
#define DEFAULT_IR_VALUE 2
#define DEFAULT_ERROR_INJECTION_POS 0
#define DEFAULT_TEST_SIZE 12
#define MINIMUM_TEST_SIZE 12
#define DEFAULT_DR_SHIFT_SIZE 32
#define MAX_DR_SHIFT_SIZE 0x20000
#define DEFAULT_TO_MANUAL_MODE false
#define SIZEOF_ID_CODE 4
#define UNCORE_DISCOVERY_SHIFT_SIZE_IN_BITS                                    \
    (((MAX_TAPS_SUPPORTED * SIZEOF_ID_CODE) + SIZEOF_TAP_DATA_PATTERN) * 8)
#define DEFAULT_LOG_LEVEL ASD_LogLevel_Info
#define DEFAULT_LOG_STREAMS ASD_LogStream_Test

#define IR_SIG_MASK 0x0FFFFFFF

#define TinySPP_Version 0x2
#define FullSPP_Version 0x3
#define Opcode_Pad  0
#define Opcode_Nop  1
#define Opcode_InitializeSPEngine 2
#define Opcode_UnblockSPEEngine 3
#define Opcode_ReadSPConfig 4
#define Opcode_WriteSPConfig 5
#define Opcode_ReadSystem 6
#define Opcode_WriteSystem 7
#define Opcode_WriteReadSystem 8
#define Opcode_LoopTrigSystem 9

#define bpk_engine 0
#define use_polling 1
#define use_interrupt 3
#define SP_VERSIONS 0x0
#define SP_IDCODE 0x4
#define SP_PROD_ID 0x20
#define SP_CAP_AS_PRESENT 0x60
#define SP_AS_EN_STAT 0xc0
#define SP_AS_EN_SET 0xc8
#define SP_AS_AVAIL_STAT 0xd0
#define SP_AS_EN_CLEAR 0xcc
#define SP_AS_AVAIL_REQ_SET 0xd8
#define SP_SESSION_MGMT_0 0x180
#define SP_SESSION_MGMT_1 0x184
#define JTAG_SET 0x1
#define CLEAR_ALL 0xFFFFFFFF


struct jtagSppCommandPacket {
    __u8    next_state: 4;
    __u8    bfc: 1;
    __u8    tdi_in: 2;
    __u8    gtu: 1;
    __u32   shift_length: 24;
} __attribute__((__packed__));

struct tinySppCommandPacket {
    union{
        struct{
            __u8	version : 4;
            __u8	opcode : 4;
            __u8	accessSpace : 3;
            __u8	continueOnFault : 1;
            __u8	sendResponseImmediately : 1;
            __u8	lastCommandPacket : 1;
            __u8	action : 2;
            __u8	tranByteCount : 7;
            __u8	spconf_addr : 2;
            __u8	addr: 6;
            __u32   payload0;
            __u32   payload1;
        } detailed;
        struct{
            __u8    Rxbuffer[12];
        } raw;
    };
} __attribute__((__packed__));

struct tinySppCommandPacketReceive {
    union{
        struct{
            __u8	version : 4;    //4 bits
            __u8	opcode : 4;
            __u8	errorType : 4;
            __u8	Rsvd1 : 1;
            __u8	LRP : 1;
            __u8	Event : 2;
            __u8	tranByteCount : 7;
            __u8	Rsvd2 : 1;
            __u8	ErrorCode : 8;
            __u32   Payload1;
            __u32   Payload2;
        } generic;
        struct{
            __u64   Rx;
        } Raw;
        struct{
            __u8    Rxbuffer[8];
        } buffer;
    };
} __attribute__((__packed__));

enum tdi_in
{
    fill_tdi_zero,
    data_for_tdi,
    tdo_as_tdi,
    pad_tdi_ones
};

enum next_state
{
    TLR,
    IDLE,
    SelectDRScan,
    CaptureDR,
    ShiftDR,
    Exit1DR,
    PauseDR,
    Exit2DR,
    UpdateDR,
    SelectIRScan,
    CaptureIR,
    ShiftIR,
    Exit1IR,
    PauseIR,
    Exit2IR,
    UpdateIR,
};

typedef struct jtag_cmd
{
    enum next_state next_state;
    uint8_t tif;
    uint8_t bfc;
    uint8_t gtu;
    uint32_t shift;
    uint32_t* payload;
    uint8_t* payload8;
    uint32_t size_of_payload;
} jtag_cmd;

typedef struct i3c_dbg_test_args
{
    bool autocmd_mode;
    unsigned long long int human_readable;
    unsigned int ir_shift_size;
    bool loop_forever;
    int numIterations;
    bool bpk_values;
    unsigned int ir_value;
    unsigned int dr_shift_size;
    bool manual_mode;
    bool count_mode;
    bool random_mode;
    bus_config buscfg;
    unsigned char tap_data_pattern[12];
    unsigned int seed;
    unsigned test_size;
    char* pattern;
    bool pattern_mode;
    bool inject_error;
    unsigned int inject_error_byte;
    unsigned int runTime;
    ASD_LogLevel log_level;
    ASD_LogStream log_streams;
} i3c_dbg_test_args;

typedef struct uncore_info
{
    unsigned int idcode[MAX_TAPS_SUPPORTED];
    unsigned int numUncores;
} uncore_info;

typedef struct ir_shift_size_map
{
    unsigned int signature;
    unsigned int ir_shift_size;
} ir_shift_size_map;

typedef struct bpk_config
{
    uint8_t bpk_version;
    uint8_t spp_engine;
    uint8_t np_engine;
    uint8_t powerman_engine;
} bpk_config;

enum bpk_opcode
{
    Nop=1,
    InitializeSPEngine,
    UnblockSPEngine,
    ReadSPConfig,
    WriteSPConfig,
    ReadSystem,
    WriteSystem,
    WriteReadSystem,
    Loop=9,
};

typedef struct bpk_cmd
{
    enum bpk_opcode bpk_opcode;
    uint32_t address;
    ssize_t data_size;
    uint32_t* data;
    uint8_t* data8;
    enum next_state next_state;
    enum tdi_in tif;
    uint8_t bfc;
    uint8_t gtu;
    uint32_t shift;
    uint8_t tranByteCount;
}bpk_cmd;
STATUS clean_previous_read(SPP_Handler* state);
STATUS i3c_dbg_test_main(int argc, char** argv);
STATUS i3c_dbg_test(SPP_Handler* state, uncore_info* uncore, i3c_dbg_test_args* args);
void print_test_results(uint64_t iterations, uint64_t micro_seconds,
                        uint64_t total_bits);
void interrupt_handler(int dummy);
STATUS parse_arguments(int argc, char** argv, i3c_dbg_test_args* args);
void showUsage(char** argv);
STATUS initialize_bpk(SPP_Handler* state, i3c_dbg_test_args* args);
STATUS disconnect_bpk(SPP_Handler* state, i3c_dbg_test_args* args);
STATUS configure_bpk(SPP_Handler* state, i3c_dbg_test_args* args);
unsigned int find_pattern(const unsigned char* haystack,
                          unsigned int haystack_size,
                          const unsigned char* needle,
                          unsigned int needle_size);
STATUS discovery(SPP_Handler* state, uncore_info* uncore, i3c_dbg_test_args* args);
STATUS capabilities_ccc( SPP_Handler* state);
STATUS start_ccc(SPP_Handler* state, uint8_t comportIndex);
STATUS start_debugAction(SPP_Handler* state);
STATUS select_ccc(SPP_Handler* state, uint8_t comportIndex);
STATUS cfg_ccc(SPP_Handler* state, uint8_t int_type);
STATUS initialize_sp_engine(SPP_Handler* state, i3c_dbg_test_args* args);
STATUS read_sp_config_cmd(SPP_Handler* state, uint32_t address, uint8_t* output, uint16_t* read_len, i3c_dbg_test_args* args);
STATUS write_sp_config_cmd(SPP_Handler* state, uint32_t address, uint32_t write_value, uint8_t* output,
                            uint16_t* read_len, i3c_dbg_test_args* args);
STATUS write_system_cmd(SPP_Handler* state, struct jtag_cmd jtag, uint8_t* output, uint16_t* read_len, i3c_dbg_test_args* args);
STATUS write_read_system_cmd(SPP_Handler* state, struct jtag_cmd jtag, uint8_t* output, uint16_t* read_len, i3c_dbg_test_args* args);
STATUS reset_jtag_to_rti_spp(SPP_Handler* state, i3c_dbg_test_args* args);
STATUS jtag_shift_spp(SPP_Handler* state, enum jtag_states next_state,
                      unsigned int number_of_bits,
                      unsigned int input_bytes, unsigned char* input,
                      unsigned int output_bytes, unsigned char* output,
                      enum jtag_states end_tap_state, i3c_dbg_test_args* args);
uint8_t spp_generate_payload(struct bpk_cmd bpk_cmd, uint8_t* payload);
uint16_t decode_rx_packet(ssize_t payload_size, uint8_t* payload, uint8_t* output);
int32_t array_into_value(uint8_t* buffer);
#endif // _I3C_DBG_TEST_H_
