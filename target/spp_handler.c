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
#include <sys/file.h>
#include <unistd.h>
// clang-format on

#include "logging.h"
unsigned int fail_read_counter = 0;
bool spp_threshold_status[MAX_SPP_BUS_DEVICES] = { false };

#define SPP_DEV_FILE_NAME "/dev/i3c-debug"
#define MAX_SPP_DEV_FILENAME 256
#define FAILURE_THRESHOLD 0

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
        for (int i = 0; i < MAX_SPP_BUS_DEVICES; i++)
        {
            state->spp_dev_handlers[i] = UNINITIALIZED_SPP_DEBUG_DRIVER_HANDLE;
            spp_threshold_status[i] = false;
        }
        state->spp_device_count = 0;
        state->device_index = 0;
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
    uint8_t send_data = write_buffer[0];
    ASD_log(ASD_LogLevel_Info, stream, option, "ASD spp_send[%d] - 0x%x",
            state->device_index, send_data);
    i3c_cmd cmd = {0};
    cmd.msgType = sppPayload;
    cmd.tx_buffer = write_buffer;
    cmd.write_len = size;
    spp_threshold_status[state->device_index] = true;
    return send_i3c_cmd(state, &cmd);
}

STATUS spp_receive(SPP_Handler* state, uint16_t * size, uint8_t * read_buffer)
{
    ASD_log(ASD_LogLevel_Info, stream, option, "ASD spp_receive[%d]",
            state->device_index);
    i3c_cmd cmd = {0};
    cmd.rx_buffer = read_buffer;
    cmd.read_len = 255;
    ssize_t ret = receive_i3c(state, &cmd);
    if( ret > 0)
    {
        *size = cmd.read_len;
        if (*size > 255)
            *size = 255;
        return ST_OK;
    }
    else
    {
        *size = 0;
        fail_read_counter++;
        ASD_log(ASD_LogLevel_Debug, stream, option, "fail_read_counter %d", fail_read_counter);
        if (fail_read_counter > FAILURE_THRESHOLD)
        {
            send_reset_rx(state);
            fail_read_counter=0;
        }
        return ST_OK;
    }
}

STATUS spp_send_cmd(SPP_Handler* state, spp_command_t cmd, uint16_t size,
                    uint8_t * write_buffer)
{
    STATUS result = ST_ERR;
    STATUS status = ST_ERR;

    ASD_log(ASD_LogLevel_Info, stream, option, "ASD spp_send_cmd[%d] 0x%x",
            state->device_index, cmd);

    ASD_log_buffer(ASD_LogLevel_Info, stream, option, write_buffer, size,
                   "SndCmd");

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
        const char *filepath = BROADCASTACTIONFILE;
        char dbg_byte[5]={0};
        ssize_t write_len;
        int broadcast_fd;
        sprintf(dbg_byte, "%x", write_buffer[0]);
        broadcast_fd = open(filepath, O_WRONLY);
        if (broadcast_fd == -1)
        {
            ASD_log(ASD_LogLevel_Error,stream, option,
                    "Error opening file: %s for broadcast command\n ",
                    strerror(errno));
            return ST_ERR;
        }
        write_len = write(broadcast_fd, dbg_byte, sizeof(dbg_byte));
        ASD_log(ASD_LogLevel_Debug,stream, option,
                "Broadcast Action: Write status: %zi, errno=%i\n",
                write_len, errno);
        if (write_len == -1)
        {
             ASD_log(ASD_LogLevel_Error,stream, option,
                    "Error %s when writing to file %s with payload 0x%x\n",
                    strerror(errno), filepath, write_buffer[0]);
            if (close(broadcast_fd) == -1)
            {
                ASD_log(ASD_LogLevel_Error,stream, option,
                            "Error closing file: %s\ns", strerror(errno));
            }
            return ST_ERR;
        }
        if (close(broadcast_fd) == -1)
        {
            ASD_log(ASD_LogLevel_Error,stream, option,
                            "Error closing to file: %s\ns", strerror(errno));
            return ST_ERR;
        }
        result = ST_OK;
    }
    return result;
}

STATUS spp_send_receive_cmd(SPP_Handler* state, spp_command_t cmd, uint16_t  wsize,
                            uint8_t * write_buffer, const uint16_t* rsize, uint8_t * read_buffer)
{
    ASD_log(ASD_LogLevel_Info,stream, option,"ASD spp_send_receive_cmd[%d] 0x%x",
            state->device_index, cmd);
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
    int flock_count = 0;
    ASD_log(ASD_LogLevel_Debug, ASD_LogStream_SPP, ASD_LogOption_None,
            "i3c-debug%d bus %s", bus, op == LOCK_EX ? "LOCK" : "UNLOCK");

    if (bus != state->spp_bus)
    {
        spp_close_driver(state);
        status = spp_open_driver(state, bus);
    }
    if (status == ST_OK)
    {
        for(int i = 0; i< MAX_SPP_BUS_DEVICES; i++)
        {
            if (state->spp_dev_handlers[i] !=
                UNINITIALIZED_SPP_DEBUG_DRIVER_HANDLE)
            {
                if (flock(state->spp_dev_handlers[i], op) != 0)
                {
                    ASD_log(ASD_LogLevel_Debug, ASD_LogStream_SPP,
                            ASD_LogOption_None,
                            "spp flock for bus %d device %d failed",
                            bus, i);
                    break;
                }
                else
                {
                    flock_count++;
                }
            }
        }
        if (flock_count != state->spp_device_count)
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
    STATUS status = ST_ERR;
    char spp_dev[MAX_SPP_DEV_FILENAME];
    state->spp_bus = bus;
    state->spp_device_count = 0;

    for(int i = 0; i< MAX_SPP_BUS_DEVICES; i++)
    {
        // In the future bus should affect device path mapping, but for now
        // it is not used because we only have one i3c_debug bus.
        snprintf(spp_dev, sizeof(spp_dev), "%s-%d", SPP_DEV_FILE_NAME, i);
        state->spp_dev_handlers[i] = open(spp_dev, O_RDWR);
        if (state->spp_dev_handlers[i] == UNINITIALIZED_SPP_DEBUG_DRIVER_HANDLE)
        {
            ASD_log(ASD_LogLevel_Debug, stream, option, "Can't open %s",
                    spp_dev);
        }
        else
        {
            ASD_log(ASD_LogLevel_Info, stream, option,
                    "Open %s spp device with fd: %d", spp_dev,
                    state->spp_dev_handlers[i]);
            state->spp_device_count++;
            status = ST_OK;
        }
    }
    if (state->spp_device_count == 0)
    {
        ASD_log(ASD_LogLevel_Debug, stream, option,
                "Can't find a device on i3c-debug%d, please install driver",
                bus);
        status = ST_ERR;
    }
    return status;
}

static void spp_close_driver(SPP_Handler* state)
{
    for(int i = 0; i< MAX_SPP_BUS_DEVICES; i++)
    {
        if (state->spp_dev_handlers[i] != UNINITIALIZED_SPP_DEBUG_DRIVER_HANDLE)
        {
            close(state->spp_dev_handlers[i]);
            state->spp_dev_handlers[i] = UNINITIALIZED_SPP_DEBUG_DRIVER_HANDLE;
        }
    }

    if (state->spp_driver_handle != UNINITIALIZED_SPP_DEBUG_DRIVER_HANDLE)
    {
        state->spp_driver_handle = UNINITIALIZED_SPP_DEBUG_DRIVER_HANDLE;
    }

    state->spp_device_count = 0;
    state->device_index = 0;
}

STATUS spp_bus_select(SPP_Handler* state, uint8_t bus)
{
    STATUS status = ST_ERR;
    if (state != NULL && spp_enabled(state))
    {
        ASD_log(ASD_LogLevel_Trace, stream, option, "bus %d state->spp_bus %d",
                    bus, state->spp_bus);
        if (bus == state->spp_bus && state->spp_device_count > 0)
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
                state->spp_bus = bus;
                status = spp_device_select(state, 0);
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

STATUS spp_bus_device_count(SPP_Handler* state, uint8_t * count)
{
    if (state == NULL || count == NULL)
        return ST_ERR;

    *count = state->spp_device_count;

    return ST_OK;
}

STATUS spp_bus_get_device_map(SPP_Handler* state, uint32_t * device_mask)
{
    STATUS status;

    if (state == NULL || device_mask == NULL)
        return ST_ERR;

    *device_mask = 0x0;

    if(!state->config->enable_spp)
    {
        ASD_log(ASD_LogLevel_Info, stream, option, "SPP is not enabled");
        return ST_OK;
    }

    // The spp_bus_get_device_map function is called during initial connection,
    // prior to handlers initialization. For that reason spp_dev_handlers are
    // not initialized at this point. We need to open the driver and see how many
    // i3c-debug handlers are available in the system.
    spp_close_driver(state);
    status = spp_open_driver(state, state->spp_bus);

    if (status == ST_OK)
    {
        for(int i = 0; i< MAX_SPP_BUS_DEVICES; i++)
        {
            if (state->spp_dev_handlers[i] !=
                UNINITIALIZED_SPP_DEBUG_DRIVER_HANDLE)
            {
                *device_mask |= (0x1 << i);
            }
        }
    }
    spp_close_driver(state);

    return status;
}

STATUS spp_device_select(SPP_Handler* state, uint8_t device)
{
    if (state == NULL || device >= MAX_SPP_BUS_DEVICES)
        return ST_ERR;

    if (state->spp_dev_handlers[device] == UNINITIALIZED_SPP_DEBUG_DRIVER_HANDLE)
        return ST_ERR;

    ASD_log(ASD_LogLevel_Info, stream, option,
            "Device select /dev/i3c-debug%d handle fd: %d", device,
            state->spp_dev_handlers[device]);

    state->spp_driver_handle = state->spp_dev_handlers[device];
    state->device_index = device;

    return ST_OK;
}

STATUS send_reset_rx(SPP_Handler* state)
{
    uint8_t count,i;
    STATUS result;
    uint8_t write_buffer[2] = {0};
    uint16_t size = 1;
    result = spp_bus_device_count(state, &count);
    if (result == ST_OK)
    {
        for (i = 0; i < count; i++)
        {
            result = spp_device_select(state, i);
            if (result == ST_OK)
            {
                ASD_log(ASD_LogLevel_Info,stream, option,"send_reset_rx %d", i);
                i3c_cmd i3ccmd = {0};
                if (size > 0)
                {
                    size = size - 1 ;
                }
                write_buffer[0] = CLEAR_ERROR_ACTION;
                i3ccmd.tx_buffer = write_buffer;
                i3ccmd.msgType = action;
                i3ccmd.action = write_buffer[0];
                i3ccmd.write_len = size;
                i3ccmd.read_len = 0;
                result = send_i3c_action(state, &i3ccmd);
                if (result == ST_ERR)
                {
                    ASD_log(ASD_LogLevel_Error,stream, option,"send_reset_rx failed");
                }
            }
        }
    }
    return ST_OK;
}

STATUS disconnect(SPP_Handler* state)
{
    uint8_t count,i;
    STATUS result;
    uint8_t write_buffer[12] = SPASENCLEAR_CMD;
    uint16_t read_len;
    uint8_t read_data[255] = {0};
    ASD_log(ASD_LogLevel_Debug, stream, option, "Disconnect");
    result = spp_bus_device_count(state, &count);
    if (result == ST_OK)
    {
        for (i = 0; i < count; i++)
        {
            result = spp_device_select(state, i);
            if (result == ST_OK)
            {
                ASD_log(ASD_LogLevel_Debug, stream, option,
                        "Disconnect cmd /dev/i3c-debug%d", i);
                result = spp_send(state, 12,  write_buffer);
                if (result != ST_OK)
                {
                    ASD_log(ASD_LogLevel_Error, stream, option,
                            "Disconnect spp_send error");
                    break;
                }
                spp_receive(state, &read_len, read_data);
                if (read_len > 0)
                {
                    ASD_log_buffer(ASD_LogLevel_Debug, stream, option, read_data, read_len, "[RX1]");
                }
            }
            else
            {
                ASD_log(ASD_LogLevel_Error, stream, option,
                        "Disconnect device select error");
                return ST_ERR;
            }
        }
    }
    return ST_OK;
}

bool check_spp_prdy_event(ASD_EVENT event, ASD_EVENT_DATA event_data)
{
    if(event == ASD_EVENT_BPK) {
        if (event_data.size >= 2 &&
            (event_data.buffer[0] == SPP_IBI_STATUS_CHANGED) &&
            ((event_data.buffer[1] == SPP_IBI_SUBREASON_PRDY) ||
            (event_data.buffer[1] == SPP_IBI_SUBREASON_OVERFLOW))) {
            return true;
        }
    }
    return false;
}