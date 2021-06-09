/*
Copyright (c) 2021, Intel Corporation

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

#include "i3c_handler.h"

// Disabling clang-format to avoid include alphabetical re-order that leads
// into a conflict for i2c-dev.h that requires std headers to be added before.

// clang-format off
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <uapi/linux/i3c/i3cdev.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
// clang-format on

#include "logging.h"

#define I3C_DEV_FILE_NAME "/dev/i3c"
#define I3C_MASTER_DRV_FILE_NAME "/sys/bus/platform/drivers/dw-i3c-master"
#define MAX_I3C_DEV_FILENAME 256
#define I3C_BUS_ADDRESS_RESERVED 127

static const ASD_LogStream stream = ASD_LogStream_I2C;
static const ASD_LogOption option = ASD_LogOption_None;

static bool i3c_enabled(I3C_Handler* state);
static STATUS i3c_open_device_drivers(I3C_Handler* state, uint8_t bus);
static void i3c_close_device_drivers(I3C_Handler* state);
static bool i3c_bus_allowed(I3C_Handler* state, uint8_t bus);
static STATUS i3c_get_dev_name(I3C_Handler* state, uint8_t bus, uint8_t* dev);

#define AST2600_I3C_BUSES 4
const char* i3c_bus_names[AST2600_I3C_BUSES] = {
    "1e7a2000.i3c0", "1e7a3000.i3c1", "1e7a4000.i3c2", "1e7a5000.i3c3"};

I3C_Handler* I3CHandler(bus_config* config)
{
    if (config == NULL)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "Invalid config parameter.");
        return NULL;
    }

    I3C_Handler* state = (I3C_Handler*)malloc(sizeof(I3C_Handler));
    if (state == NULL)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "Failed to malloc I3C_Handler.");
    }
    else
    {
        for (int i = 0; i < i3C_MAX_DEV_HANDLERS; i++)
            state->i3c_driver_handlers[i] = UNINITIALIZED_I3C_DRIVER_HANDLE;
        state->config = config;
    }

    return state;
}

STATUS i3c_initialize(I3C_Handler* state)
{
    if (state != NULL && i3c_enabled(state))
    {
        state->i3c_bus = I3C_BUS_ADDRESS_RESERVED;
        return ST_OK;
    }
    return ST_ERR;
}

STATUS i3c_deinitialize(I3C_Handler* state)
{
    if (state == NULL)
        return ST_ERR;
    i3c_close_device_drivers(state);
    state = NULL;
    return ST_OK;
}

STATUS i3c_bus_flock(I3C_Handler* state, uint8_t bus, int op)
{
    STATUS status = ST_OK;
    ASD_log(ASD_LogLevel_Debug, ASD_LogStream_I2C, ASD_LogOption_None,
            "i3c - bus %d %s", bus, op == LOCK_EX ? "LOCK" : "UNLOCK");
    if (state != NULL && state->i3c_bus == I3C_BUS_ADDRESS_RESERVED)
    {
        status = i3c_bus_select(state, bus);
    }
    if (status == ST_OK)
    {
        for (int i = 0; i < i3C_MAX_DEV_HANDLERS; i++)
        {
            if (flock(state->i3c_driver_handlers[i], op) != 0)
            {
                ASD_log(ASD_LogLevel_Error, ASD_LogStream_I2C,
                        ASD_LogOption_None,
                        "i3c flock for bus %d failed dev %d handler = 0x%x",
                        bus, i, state->i3c_driver_handlers[i]);
                status = ST_ERR;
                break;
            }
        }
    }
    return status;
}

STATUS i3c_bus_select(I3C_Handler* state, uint8_t bus)
{
    STATUS status = ST_ERR;
    if (state != NULL && i3c_enabled(state))
    {
        if (bus == state->i3c_bus)
        {
            status = ST_OK;
        }
        else if (i3c_bus_allowed(state, bus))
        {
            i3c_close_device_drivers(state);
            ASD_log(ASD_LogLevel_Error, stream, option, "Selecting Bus %d",
                    bus);
            status = i3c_open_device_drivers(state, bus);
        }
        else
        {
            ASD_log(ASD_LogLevel_Error, stream, option, "Bus %d not allowed",
                    bus);
        }
    }
    return status;
}

STATUS i3c_set_sclk(I3C_Handler* state, uint16_t sclk)
{
    if (state == NULL || !i3c_enabled(state))
        return ST_ERR;
    return ST_OK;
}

STATUS i3c_read_write(I3C_Handler* state, void* msg_set)
{
    if (state == NULL || msg_set == NULL || !i3c_enabled(state))
        return ST_ERR;

    // Convert i2c packet to i3c request format
    struct i2c_rdwr_ioctl_data* ioctl_data = msg_set;
    struct i3c_ioc_priv_xfer* xfers;
    xfers =
        (struct i3c_ioc_priv_xfer*)calloc(ioctl_data->nmsgs, sizeof(*xfers));

    for (int i = 0; i < ioctl_data->nmsgs; i++)
    {
        xfers[i].len = ioctl_data->msgs[i].len;
        xfers[i].data = ioctl_data->msgs[i].buf;
        xfers[i].rnw = (ioctl_data->msgs[i].flags & I2C_M_RD) ? 1 : 0;
    }

    int ret = ioctl(state->i3c_driver_handlers[0],
                    I3C_IOC_PRIV_XFER(ioctl_data->nmsgs), xfers);

    if (ret < 0)
    {
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Debug, stream, option,
                "I3C_RDWR ioctl returned %d - %d - %s", ret, errno,
                strerror(errno));
#endif
        return ST_ERR;
    }

    return ST_OK;
}

static bool i3c_enabled(I3C_Handler* state)
{
    return state->config->enable_i3c;
}

static STATUS i3c_open_device_drivers(I3C_Handler* state, uint8_t bus)
{
    STATUS status = ST_ERR;
    char i3c_dev_name[MAX_I3C_DEV_FILENAME];
    char i3c_dev[MAX_I3C_DEV_FILENAME];

    status = i3c_get_dev_name(state, bus, i3c_dev_name);
    if (status != ST_OK)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "could not find i3c bus %d dev name", bus);
        return ST_ERR;
    }
    for (int i = 0; i < i3C_MAX_DEV_HANDLERS; i++)
    {
        snprintf(i3c_dev, sizeof(i3c_dev), "/dev/%s-3c00000000%d", i3c_dev_name,
                 i);
        state->i3c_driver_handlers[i] = open(i3c_dev, O_RDWR);
        if (state->i3c_driver_handlers[i] == UNINITIALIZED_I3C_DRIVER_HANDLE)
        {
            ASD_log(ASD_LogLevel_Error, stream, option, "Can't open %s",
                    i3c_dev);
            i3c_close_device_drivers(state);
            return ST_ERR;
        }
        ASD_log(ASD_LogLevel_Debug, stream, option,
                "open device driver %s for bus %d", i3c_dev, bus);
        state->i3c_bus = bus;
        state->config->default_bus = bus;
    }
    return ST_OK;
}

static void i3c_close_device_drivers(I3C_Handler* state)
{

    for (int i = 0; i < i3C_MAX_DEV_HANDLERS; i++)
    {
        if (state->i3c_driver_handlers[i] != UNINITIALIZED_I3C_DRIVER_HANDLE)
        {
            close(state->i3c_driver_handlers[i]);
            ASD_log(ASD_LogLevel_Debug, stream, option,
                    "closing dev handler %d", i);
            state->i3c_driver_handlers[i] = UNINITIALIZED_I3C_DRIVER_HANDLE;
        }
    }
}

static bool i3c_bus_allowed(I3C_Handler* state, uint8_t bus)
{
    if (state == NULL)
        return false;
    for (int i = 0; i < MAX_IxC_BUSES; i++)
    {
        if (state->config->bus_config_map[i] == bus &&
            state->config->bus_config_type[i] == BUS_CONFIG_I3C)
            return true;
    }
    return false;
}

static STATUS i3c_get_dev_name(I3C_Handler* state, uint8_t bus, uint8_t* dev)
{
    STATUS status = ST_ERR;
    char i3c_bus_drv[MAX_I3C_DEV_FILENAME];
    int dev_handle = UNINITIALIZED_I3C_DRIVER_HANDLE;

    if (bus >= AST2600_I3C_BUSES)
    {
        ASD_log(ASD_LogLevel_Error, stream, option, "Unexpected i3c bus");
        return ST_ERR;
    }

    for (int i = 0; i < AST2600_I3C_BUSES; i++)
    {
        snprintf(dev, MAX_I3C_DEV_FILENAME, "i3c-%d", i);
        snprintf(i3c_bus_drv, MAX_I3C_DEV_FILENAME, "%s/%s/i3c-%d",
                 I3C_MASTER_DRV_FILE_NAME, i3c_bus_names[bus], i);

        dev_handle = open(i3c_bus_drv, O_RDONLY);
        if (dev_handle == UNINITIALIZED_I3C_DRIVER_HANDLE)
        {
            ASD_log(ASD_LogLevel_Debug, stream, option,
                    "Can't open i3c bus driver %s", i3c_bus_drv);
        }
        else
        {
            ASD_log(ASD_LogLevel_Debug, stream, option, "Found dev %s", dev);
            close(dev_handle);
            status = ST_OK;
            break;
        }
    }
    return status;
}
