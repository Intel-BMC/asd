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
#include "i3c_debug_handler.h"
// clang-format off
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
// clang-format on

#include "logging.h"

#define SPP_DEV_FILE_NAME "/dev/i3c-debug"
#define MAX_SPP_DEV_FILENAME 256

static const ASD_LogStream stream = ASD_LogStream_SPP;
static const ASD_LogOption option = ASD_LogOption_None;

static bool spp_enabled(SPP_Handler* state);
static STATUS spp_open_driver(SPP_Handler* state, uint8_t bus);
static void spp_close_driver(SPP_Handler* state);
static bool spp_bus_allowed(SPP_Handler* state, uint8_t bus);

SPP_Handler* SPPHandler(bus_config* config)
{
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
    }
    else
    {
        state->spp_driver_handle = UNINITIALIZED_SPP_DEBUG_DRIVER_HANDLE;
        state->config = config;
    }

    return state;
}

STATUS spp_initialize(SPP_Handler* state)
{
    STATUS status = ST_ERR;
    if (state != NULL && spp_enabled(state))
    {
        status = spp_bus_select(state, state->config->default_bus);
        if (status == ST_OK)
        {
            state->ibi_handled = false;
        }
    }
    return status;
}

STATUS spp_deinitialize(SPP_Handler* state)
{
    if (state == NULL)
        return ST_ERR;
    spp_close_driver(state);
    state = NULL;
    return ST_OK;
}

STATUS spp_send(SPP_Handler* state, uint16_t size, uint8_t * write_buffer)
{
    ASD_log(ASD_LogLevel_Debug, stream, option, "ASD SPP_send");
    i3c_cmd cmd = {0};
    cmd.msgType = sppPayload;
    cmd.tx_buffer = write_buffer;
    cmd.write_len = size;
    return send_i3c_cmd(state, &cmd);
}

STATUS spp_receive(SPP_Handler* state, uint16_t * size, uint8_t * read_buffer)
{
    ASD_log(ASD_LogLevel_Debug, stream, option, "ASD SPP_receive");
    i3c_cmd cmd = {0};
    cmd.rx_buffer = read_buffer;
    cmd.read_len = 255;
    ssize_t ret = receive_i3c(state, &cmd);
    if( ret > 0)
    {
        *size = cmd.read_len;
        return ST_OK;
    }
    else if (ret == 0)
    {
        *size = 0;
        return ST_OK;
    }
    return ST_ERR;
}

STATUS spp_send_cmd(SPP_Handler* state, spp_command_t cmd, uint16_t size,
                    uint8_t * write_buffer)
{
    ASD_log(ASD_LogLevel_Debug, stream, option, "ASD SPP_send_cmd");
    if (cmd == BroadcastResetAction)
    {
        ASD_log(ASD_LogLevel_Debug, stream, option, "BroadcastResetAction");
    }
    else if(cmd == DirectResetAction)
    {
        ASD_log(ASD_LogLevel_Debug, stream, option, "DirectResetAction");
    }
    else if (cmd == BpkOpcode )
    {
        ASD_log(ASD_LogLevel_Debug,stream, option,"BpkOpcode");
        i3c_cmd i3ccmd = {0};
        if (size > 0)
        {
            size = size - 1 ;
        }
        i3ccmd.tx_buffer = write_buffer + 1;
        i3ccmd.msgType = opcode;
        i3ccmd.opcode = write_buffer[0];
        i3ccmd.write_len = size;
        i3ccmd.read_len = 0;
        return send_i3c_opcode(state, &i3ccmd);
    }
    else if (cmd == DebugAction )
    {
        i3c_cmd i3ccmd = {0};
        if (size > 0)
        {
            size = size - 1 ;
        }
        i3ccmd.tx_buffer = write_buffer + 1;
        i3ccmd.msgType = action;
        i3ccmd.action = write_buffer[0];
        i3ccmd.write_len = size;
        i3ccmd.read_len = 0;
        return send_i3c_action(state, &i3ccmd);
    }
    else if (cmd == BroadcastDebugAction )
    {
        ASD_log(ASD_LogLevel_Debug,stream, option, "BroadcastDebugAction");
    }
    return ST_OK;
}

STATUS spp_send_receive_cmd(SPP_Handler* state, spp_command_t cmd, uint16_t  wsize,
                            uint8_t * write_buffer, const uint16_t* rsize, uint8_t * read_buffer)
{
    ASD_log(ASD_LogLevel_Debug,stream, option,"ASD SPP_send_receive_cmd");
    if (cmd == BroadcastResetAction)
    {
        ASD_log(ASD_LogLevel_Debug,stream, option,"BroadcastResetAction");
    }
    else if(cmd == DirectResetAction)
    {
        ASD_log(ASD_LogLevel_Debug,stream, option,"DirectResetAction");
    }
    else if (cmd == BpkOpcode)
    {
        ASD_log(ASD_LogLevel_Debug,stream, option,"BpkOpcode");
        i3c_cmd i3ccmd = {0};
        if (wsize > 0)
        {
            wsize = wsize - 1 ;
        }
        i3ccmd.rx_buffer = read_buffer;
        i3ccmd.tx_buffer = write_buffer + 1;
        i3ccmd.msgType = opcode;
        i3ccmd.opcode = write_buffer[0];
        i3ccmd.write_len = wsize;
        i3ccmd.read_len = 32;
        return send_i3c_opcode(state, &i3ccmd);
    }
    else if (cmd == DebugAction )
    {
        ASD_log(ASD_LogLevel_Debug,stream, option,"DebugAction");
        i3c_cmd i3ccmd = {0};
        if (wsize > 0)
        {
            wsize = wsize - 1 ;
        }
        i3ccmd.tx_buffer = write_buffer + 1;
        i3ccmd.msgType = action;
        i3ccmd.action = write_buffer[0];
        i3ccmd.write_len = wsize;
        i3ccmd.read_len = *rsize;
        return send_i3c_action(state, &i3ccmd);
    }
    return ST_OK;
}

STATUS spp_set_sim_data_cmd(SPP_Handler* state, uint16_t size,
                            uint8_t * read_buffer)
{

    return ST_OK;
}

STATUS spp_bus_flock(SPP_Handler* state, uint8_t bus, int op)
{
    STATUS status = ST_OK;
    ASD_log(ASD_LogLevel_Debug, ASD_LogStream_SPP, ASD_LogOption_None,
            "i2c - bus %d %s", bus, op == LOCK_EX ? "LOCK" : "UNLOCK");
    if (bus != state->spp_bus)
    {
        spp_close_driver(state);
        status = spp_open_driver(state, bus);
    }
    if (status == ST_OK)
    {
        if (flock(state->spp_driver_handle, op) != 0)
        {
            ASD_log(ASD_LogLevel_Debug, ASD_LogStream_SPP, ASD_LogOption_None,
                    "spp flock for bus %d failed", bus);
            status = ST_ERR;
        }
    }

    return status;
}

static bool spp_bus_allowed(SPP_Handler* state, uint8_t bus)
{
    for (int i = 0; i < MAX_IxC_BUSES + MAX_SPP_BUSES; i++)
    {
        if (state->config->bus_config_map[i] == bus &&
            state->config->bus_config_type[i] == BUS_CONFIG_SPP)
            return true;
    }
    return false;
}

static bool spp_enabled(SPP_Handler* state)
{
    return state->config->enable_spp;
}

static STATUS spp_open_driver(SPP_Handler* state, uint8_t bus)
{
    char spp_dev[MAX_SPP_DEV_FILENAME];
    state->spp_bus = bus;
    snprintf(spp_dev, sizeof(spp_dev), "%s-%d", SPP_DEV_FILE_NAME, bus);
    state->spp_driver_handle = open(spp_dev, O_RDWR);
    ASD_log(ASD_LogLevel_Debug, stream, option,
            "SPP initialize with fd: %d", state->spp_driver_handle);
    if (state->spp_driver_handle == UNINITIALIZED_SPP_DEBUG_DRIVER_HANDLE)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "Can't open %s, please install driver", spp_dev);
        return ST_ERR;
    }
    return ST_OK;
}

static void spp_close_driver(SPP_Handler* state)
{
    if (state->spp_driver_handle != UNINITIALIZED_SPP_DEBUG_DRIVER_HANDLE)
    {
        close(state->spp_driver_handle);
        state->spp_driver_handle = UNINITIALIZED_SPP_DEBUG_DRIVER_HANDLE;
    }
}

STATUS spp_bus_select(SPP_Handler* state, uint8_t bus)
{
    STATUS status = ST_ERR;
    if (state != NULL && spp_enabled(state))
    {
        ASD_log(ASD_LogLevel_Trace, stream, option, "bus %d state->spp_bus %d",
                    bus, state->spp_bus);
        if (bus == state->spp_bus && !(state->spp_driver_handle == UNINITIALIZED_SPP_DEBUG_DRIVER_HANDLE))
        {
            status = ST_OK;
        }
        else if (spp_bus_allowed(state, bus))
        {
            spp_close_driver(state);
            ASD_log(ASD_LogLevel_Error, stream, option, "Selecting Bus %d",
                    bus);
            status = spp_open_driver(state, bus);
            if (status == ST_OK)
            {
                state->config->default_bus = bus;
            }
        }
        else
        {
            ASD_log(ASD_LogLevel_Error, stream, option, "Bus %d not allowed",
                    bus);
        }
    }
    return status;
}
STATUS spp_set_sclk(SPP_Handler* state, uint16_t sclk)
{
    return ST_OK;
}
