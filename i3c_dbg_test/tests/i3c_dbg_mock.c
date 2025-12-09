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
static const ASD_LogOption option = ASD_LogOption_None;

#define MAX_RESPONSES 20
#define RESPONSE_SIZE 255

uint8_t mock_data[MAX_RESPONSES][RESPONSE_SIZE];
size_t mock_data_len[MAX_RESPONSES];
int current_response_index = 0;

void prepare_buffer_read(uint8_t* read_buffer, size_t size, int index)
{
    if (index < MAX_RESPONSES)
    {
        memcpy_s(mock_data[index], RESPONSE_SIZE, read_buffer, size);
        mock_data_len[index] = size;
        ASD_log_buffer(ASD_LogLevel_Info, stream, option, read_buffer, size,
                       "Inc");
        ASD_log_buffer(ASD_LogLevel_Info, stream, option, mock_data[index],
                       mock_data_len[index], "Inc");
    }
}

void reset_mock_data()
{
    // Reset each response in mock_data to zero
    for (int i = 0; i < MAX_RESPONSES; i++)
    {
        memset(mock_data[i], 0, RESPONSE_SIZE);
        mock_data_len[i] = 0; // Reset the length of each response
    }
    current_response_index = 0; // Reset the response index
}

SPP_Handler* SPPHandler(bus_config* config)
{
    uint16_t i = 0;
    if (config == NULL)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "Invalid config parameter.");
        return NULL;
    }
    SPP_Handler* state = (SPP_Handler*)malloc(sizeof(SPP_Handler));
    if (state == NULL)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "Failed to malloc SPP_Handler.");
        return NULL;
    }

    return state;
}

STATUS spp_device_select(SPP_Handler* state, uint8_t device)
{
    if (state == NULL || device >= MAX_SPP_BUS_DEVICES)
        return ST_ERR;

    if (state->spp_dev_handlers[device] ==
        UNINITIALIZED_SPP_DEBUG_DRIVER_HANDLE)
        return ST_ERR;

    ASD_log(ASD_LogLevel_Info, stream, option,
            "Device select /dev/i3c-debug%d handle fd: %d", device,
            state->spp_dev_handlers[device]);

    state->spp_driver_handle = state->spp_dev_handlers[device];
    state->device_index = device;

    return ST_OK;
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

STATUS spp_send(SPP_Handler* state, uint16_t size, uint8_t* write_buffer)
{
    ASD_log(ASD_LogLevel_Debug, stream, option, "spp_send(%d bytes)", size);
    ASD_log_buffer(ASD_LogLevel_Debug, stream, option, write_buffer,
                   (size_t)size, "Spp");
    return ST_OK;
}

STATUS spp_bus_device_count(SPP_Handler* state, uint8_t* count)
{
    if (state == NULL || count == NULL)
        return ST_ERR;

    *count = state->spp_device_count;

    return ST_OK;
}

STATUS spp_receive(SPP_Handler* state, uint16_t* size, uint8_t* read_buffer)
{
    STATUS status = ST_OK;
    uint16_t num_of_bytes = *size;
    uint16_t i = 0;
    ASD_log(ASD_LogLevel_Debug, stream, option, "spp_receive", size);
    if (current_response_index < MAX_RESPONSES)
    {
        *size = mock_data_len[current_response_index];
        memcpy_s(read_buffer, *size, mock_data[current_response_index],
                 mock_data_len[current_response_index]);
        current_response_index++;
    }
    else
    {
        // Handle case where no more responses are available
        status = ST_ERR;
    }
    return ST_OK;
}

STATUS spp_send_cmd(SPP_Handler* state, spp_command_t cmd, uint16_t size,
                    uint8_t* write_buffer)
{
    ASD_log(ASD_LogLevel_Debug, stream, option, "spp_send_cmd(%d bytes)", size);
    ASD_log_buffer(ASD_LogLevel_Debug, stream, option, write_buffer,
                   (size_t)size, "SppCmd");
    return ST_OK;
}

STATUS spp_send_receive_cmd(SPP_Handler* state, spp_command_t cmd,
                            uint16_t wsize, uint8_t* write_buffer,
                            const uint16_t* rsize, uint8_t* read_buffer)
{
    STATUS status = ST_OK;
    uint16_t num_of_bytes = wsize;
    uint16_t i = 0;
    ASD_log(ASD_LogLevel_Debug, stream, option,
            "spp_send_receive_cmd(%d bytes)", num_of_bytes);
    ASD_log_buffer(ASD_LogLevel_Debug, stream, option, write_buffer,
                   (size_t)num_of_bytes, "SppCmd");
    if (current_response_index < MAX_RESPONSES)
    {
        //*rsize = mock_data_len[current_response_index];
        memcpy_s(read_buffer, *rsize, mock_data[current_response_index],
                 mock_data_len[current_response_index]);
        current_response_index++;
    }
    else
    {
        // Handle case where no more responses are available
        status = ST_ERR;
    }
    return ST_OK;
}

// Mock for spp_receive_autocommand function
STATUS spp_receive_autocommand(SPP_Handler* state, uint16_t* size,
                               uint8_t* read_buffer)
{
    STATUS status = ST_OK;
    ASD_log(ASD_LogLevel_Debug, stream, option, "spp_receive_autocommand");

    if (current_response_index < MAX_RESPONSES)
    {
        *size = mock_data_len[current_response_index];
        memcpy_s(read_buffer, *size, mock_data[current_response_index],
                 mock_data_len[current_response_index]);
        current_response_index++;
        ASD_log(ASD_LogLevel_Debug, stream, option,
                "Mock spp_receive_autocommand returning size=%d", *size);
    }
    else
    {
        // Handle case where no more responses are available
        *size = 0;
        status = ST_ERR;
        ASD_log(ASD_LogLevel_Debug, stream, option,
                "Mock spp_receive_autocommand: no more mock data available");
    }

    return status;
}