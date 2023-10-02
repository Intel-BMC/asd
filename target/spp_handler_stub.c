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

#include "spp_handler.h"

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
// clang-format off
#include <safe_mem_lib.h>
// clang-format on

#include "logging.h"

static const ASD_LogStream stream = ASD_LogStream_SPP;
static const ASD_LogOption option = ASD_LogLevel_Info;

#define SPP_READ_BACK_SIMULATOR

#ifdef SPP_READ_BACK_SIMULATOR
#define SPP_SIM_DATA_STATUS_EMPTY 0
#define SPP_SIM_DATA_STATUS_READY 1

typedef struct SPP_Sim_Data
{
    uint16_t status;
    uint16_t size;
    unsigned char buffer[MAX_DATA_SIZE];
} SPP_Sim_Data;

SPP_Sim_Data sim_data;
#endif // SPP_READ_BACK_SIMULATOR

SPP_Handler* SPPHandler(bus_config* config)
{
    uint16_t i = 0;
    SPP_Handler* state = (SPP_Handler*)malloc(sizeof(SPP_Handler));
    if (state == NULL)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "Failed to malloc SPP_Handler.");
        return NULL;
    }

    for (i = 0; i < MAX_SPP_BUSES; i++)
    {
        state->spp_buses[i] = 0;
    }

    state->i3c_debug_driver_handle = UNINITIALIZED_I3C_DEBUG_DRIVER_HANDLE;

#ifdef SPP_READ_BACK_SIMULATOR
    sim_data.size = 0;
    sim_data.status = SPP_SIM_DATA_STATUS_EMPTY;
#endif // SPP_READ_BACK_SIMULATOR

    return state;
}

STATUS spp_initialize(SPP_Handler* state)
{
    if (state == NULL)
        return ST_ERR;

    return ST_OK;
}

STATUS spp_deinitialize(SPP_Handler* state)
{
    if (state == NULL)
        return ST_ERR;

    return ST_OK;
}

STATUS spp_bus_flock(SPP_Handler* state, uint8_t bus, int op)
{
    return ST_OK;
}

STATUS spp_bus_select(SPP_Handler* state, uint8_t bus)
{
    return ST_OK;
}

STATUS spp_set_sclk(SPP_Handler* state, uint16_t sclk)
{
    return ST_OK;
}

STATUS spp_send(SPP_Handler* state, uint16_t size, uint8_t * write_buffer)
{
    ASD_log(ASD_LogLevel_Debug, stream, option, "spp_send(%d bytes)", size);
    ASD_log_buffer(ASD_LogLevel_Debug, stream, option, write_buffer,
                   (size_t)size, "Spp");
    return ST_OK;
}

STATUS spp_receive(SPP_Handler* state, uint16_t * size, uint8_t * read_buffer)
{
    STATUS status = ST_OK;
    uint16_t num_of_bytes = *size;
    uint16_t i = 0;

#ifdef SPP_READ_BACK_SIMULATOR
    if(sim_data.status == SPP_SIM_DATA_STATUS_EMPTY) {
        status = ST_ERR;
    } else {
        for (i = 0; i < sim_data.size; i++) {
            read_buffer[i] = sim_data.buffer[i];
        }
        *size = sim_data.size;
        ASD_log_buffer(ASD_LogLevel_Debug, stream, option,read_buffer,
                       (size_t)*size, "SimOut");
        sim_data.status = SPP_SIM_DATA_STATUS_EMPTY;
        sim_data.size = 0;
    }
#else
    ASD_log(ASD_LogLevel_Debug, stream, option, "spp_receive(PATTERN)");
    read_buffer[0] = 0xBB;
    for (i = 1; i < num_of_bytes - 1; i++) {
        read_buffer[i] = 0xCC;
    }
    read_buffer[num_of_bytes-1] = 0xEE;
#endif // SPP_READ_BACK_SIMULATOR

    return status;
}

STATUS spp_send_cmd(SPP_Handler* state, spp_command_t cmd, uint16_t size,
                    uint8_t * write_buffer)
{
    ASD_log(ASD_LogLevel_Debug, stream, option, "spp_send_cmd(%d bytes)",
            size);
    ASD_log_buffer(ASD_LogLevel_Debug, stream, option, write_buffer,
                   (size_t)size, "SppCmd");
    return ST_OK;
}

STATUS spp_send_receive_cmd(SPP_Handler* state, spp_command_t cmd,
                            uint16_t wsize, uint8_t * write_buffer,
                            uint16_t * rsize, uint8_t * read_buffer)
{
    STATUS status = ST_OK;
    uint16_t num_of_bytes = wsize;
    uint16_t i = 0;
    ASD_log(ASD_LogLevel_Debug, stream, option,
            "spp_send_receive_cmd(%d bytes)", num_of_bytes);
    ASD_log_buffer(ASD_LogLevel_Debug, stream, option, write_buffer,
                   (size_t)num_of_bytes, "SppCmd");

#ifdef SPP_READ_BACK_SIMULATOR
    if(sim_data.status == SPP_SIM_DATA_STATUS_EMPTY) {
        status = ST_ERR;
    } else {
        for (i = 0; i < sim_data.size; i++) {
            read_buffer[i] = sim_data.buffer[i];
        }
        *rsize = sim_data.size;
        ASD_log_buffer(ASD_LogLevel_Debug, stream, option,read_buffer,
                       (size_t)*rsize, "SimOut");
        sim_data.status = SPP_SIM_DATA_STATUS_EMPTY;
        sim_data.size = 0;
    }
#else
    ASD_log(ASD_LogLevel_Debug, stream, option,
            "spp_send_receive_cmd(PATTERN)");
    read_buffer[0] = 0xBB;
    for (i = 1; i < num_of_bytes - 1; i++) {
        read_buffer[i] = 0xCC;
    }
    read_buffer[num_of_bytes-1] = 0xEE;
#endif // SPP_READ_BACK_SIMULATOR
    return status;
}

STATUS spp_set_sim_data_cmd(SPP_Handler* state, uint16_t size,
                            uint8_t * read_buffer)
{
#ifdef SPP_READ_BACK_SIMULATOR
    uint16_t i = 0;
    ASD_log(ASD_LogLevel_Debug, stream, option,
            "spp_set_sim_read_data_cmd(%d bytes)", size);

    for (i = 0; i<size; i++)
    {
        sim_data.buffer[i] = read_buffer[i];
    }
    sim_data.size = size;
    sim_data.status = SPP_SIM_DATA_STATUS_READY;

    ASD_log_buffer(ASD_LogLevel_Debug, stream, option, sim_data.buffer,
                   (size_t)sim_data.size, "SimIn");
#endif // SPP_READ_BACK_SIMULATOR

    return ST_OK;
}