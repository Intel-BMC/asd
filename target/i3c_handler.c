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
#include <limits.h>
#include <sys/ioctl.h>
#include <unistd.h>
// Include local copy of unofficial i3cdev header
// Taken from linux:include/uapi/linux/i3c/i3cdev.h
// Replace with <linux/i3c/i3cdev.h> when available in upstream kernel
#include <uapi/linux/i3c/i3cdev.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <regex.h>
#include <dirent.h>
// clang-format on

#include "logging.h"

#define I3C_DEV_FILE_NAME "/dev/i3c"
#define I3C_SYS_BUS_DEVICES "/sys/bus/i3c/devices/"
#define MAX_I3C_DEV_FILENAME 256
#define I3C_BUS_ADDRESS_RESERVED 127

static const ASD_LogStream stream = ASD_LogStream_I2C;
static const ASD_LogOption option = ASD_LogOption_None;

static bool i3c_enabled(I3C_Handler* state);
static bool i3c_device_drivers_opened(I3C_Handler* state);
static STATUS i3c_open_device_drivers(I3C_Handler* state, uint8_t bus);
static void i3c_close_device_drivers(I3C_Handler* state);
static bool i3c_bus_allowed(I3C_Handler* state, uint8_t bus);
static STATUS is_spd_bus(char * bus_name, bool * ret);
static STATUS get_bound_index(char * bus_name, int * boundIndex);
static STATUS get_platform_index(char * bus_name, uint8_t * platIndex);
static STATUS create_spd_mapping(I3C_Handler* state);

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
        state->bus_token = UNINITIALIZED_I3C_BUS_TOKEN;
        state->dbus = dbus_helper();
        if (state->dbus == NULL)
        {
            free(state);
            state = NULL;
        }
    }

    return state;
}

STATUS i3c_initialize(I3C_Handler* state)
{
    STATUS status = ST_ERR;
    if (state != NULL && i3c_enabled(state))
    {
        status = dbus_initialize(state->dbus);
        if (status == ST_OK)
        {
            state->dbus->fd = sd_bus_get_fd(state->dbus->bus);
            if (state->dbus->fd < 0)
            {
                ASD_log(ASD_LogLevel_Error, stream, option,
                        "sd_bus_get_fd failed");
                status = ST_ERR;
            }
        }
        else
        {
            ASD_log(ASD_LogLevel_Error, stream, option,
                    "Failed to init i3c dbus handler");
        }
        state->i3c_bus = I3C_BUS_ADDRESS_RESERVED;
    }
    return status;
}

STATUS i3c_deinitialize(I3C_Handler* state)
{
    STATUS status = ST_OK;

    if (state == NULL)
        return ST_ERR;

    i3c_close_device_drivers(state);

    // Release I3C BMC ownership at ASD exit
    if (state->bus_token != UNINITIALIZED_I3C_BUS_TOKEN)
    {
        status = dbus_rel_i3c_ownership(state->dbus, state->bus_token);
        if (status == ST_OK)
        {
            ASD_log(ASD_LogLevel_Debug, ASD_LogStream_I2C, ASD_LogOption_None,
                    "Release BMC i3c bus ownership succeed");

            state->bus_token = UNINITIALIZED_I3C_BUS_TOKEN;
        }
        else
        {
            ASD_log(ASD_LogLevel_Error, ASD_LogStream_I2C, ASD_LogOption_None,
                    "Release BMC i3c bus ownership failed");
        }
    }

    if (state->dbus != NULL)
    {
        dbus_deinitialize(state->dbus);
        free(state->dbus);
        state->dbus = NULL;
    }
    state = NULL;
    return status;
}

STATUS i3c_bus_flock_dev_handlers(I3C_Handler* state, uint8_t bus, int op)
{
    STATUS status = ST_OK;
    ASD_log(ASD_LogLevel_Debug, ASD_LogStream_I2C, ASD_LogOption_None,
            "i3c - bus %d %s", bus, op == LOCK_EX ? "LOCK" : "UNLOCK");

    if (state == NULL)
        return ST_ERR;

    if (state->i3c_bus == I3C_BUS_ADDRESS_RESERVED)
    {
        status = i3c_bus_select(state, bus);
    }
    if (status == ST_OK)
    {
        for (int i = 0; i < i3C_MAX_DEV_HANDLERS; i++)
        {
            if (state->i3c_driver_handlers[i] ==
                UNINITIALIZED_I3C_DRIVER_HANDLE)
                continue;

            if (flock(state->i3c_driver_handlers[i], op) != 0)
            {
                ASD_log(ASD_LogLevel_Error, ASD_LogStream_I2C,
                        ASD_LogOption_None,
                        "i3c flock for bus %d failed dev %x handler = 0x%x",
                        bus, i, state->i3c_driver_handlers[i]);
                status = ST_ERR;
            }
        }
    }
    return status;
}

STATUS i3c_bus_flock(I3C_Handler* state, uint8_t bus, int op)
{
    STATUS status = ST_OK;
    I3c_Ownership owner = CPU_OWNER;

    ASD_log(ASD_LogLevel_Debug, ASD_LogStream_I2C, ASD_LogOption_None,
            "i3c - bus %d %s", bus, op == LOCK_EX ? "LOCK" : "UNLOCK");

    if (state == NULL || state->dbus == NULL)
        return ST_ERR;

    // Request I3C BMC ownership on the first xfer attempt
    if (state->bus_token == UNINITIALIZED_I3C_BUS_TOKEN)
    {
        ASD_log(ASD_LogLevel_Debug, ASD_LogStream_I2C, ASD_LogOption_None,
                "Request i3c bus ownership");

        status = dbus_req_i3c_ownership(state->dbus, &state->bus_token);
        if (status == ST_OK)
        {
            ASD_log(ASD_LogLevel_Debug, ASD_LogStream_I2C, ASD_LogOption_None,
                    "Request i3c bus ownership succeed token: %d",
                    state->bus_token);
            // Wait 1s for system to bind the driver and create dev handlers
            usleep(1000000);
            status = i3c_open_device_drivers(state, bus);
            if (status != ST_OK)
            {
                ASD_log(ASD_LogLevel_Error, ASD_LogStream_I2C,
                        ASD_LogOption_None, "Open i3c device drivers failed");
                status = dbus_rel_i3c_ownership(state->dbus, state->bus_token);
                if (status == ST_OK)
                {
                    ASD_log(ASD_LogLevel_Debug, ASD_LogStream_I2C,
                            ASD_LogOption_None,
                            "Release BMC i3c bus ownership succeed");
                }
                state->bus_token = UNINITIALIZED_I3C_BUS_TOKEN;
                return ST_ERR;
            }
        }
        else
        {
            ASD_log(ASD_LogLevel_Error, ASD_LogStream_I2C, ASD_LogOption_None,
                    "Request i3c bus ownership failed");
            return status;
        }
    }

    // Read configuration from dbus
    status = dbus_read_i3c_ownership(state->dbus, &owner);
    if (status == ST_OK)
    {
        ASD_log(ASD_LogLevel_Debug, ASD_LogStream_I2C, ASD_LogOption_None,
                "i3c ownership %s", owner == CPU_OWNER ? "CPU" : "BMC");
        if (owner == BMC_OWNER)
        {
            status = i3c_bus_flock_dev_handlers(state, bus, op);
            if (status != ST_OK)
            {
                // If lock fail, unlock all files
                if (op == LOCK_EX)
                {
                    i3c_bus_flock_dev_handlers(state, bus, LOCK_UN);
                }
            }
        }
        else
        {
            ASD_log(ASD_LogLevel_Error, ASD_LogStream_I2C, ASD_LogOption_None,
                    "BMC does not have i3c bus ownership");
            return ST_ERR;
        }
    }
    else
    {
        ASD_log(ASD_LogLevel_Error, ASD_LogStream_I2C, ASD_LogOption_None,
                "Fail to read i3c bus ownership from dbus");
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
            if (i3c_device_drivers_opened(state))
            {
                status = ST_OK;
            }
            else
            {
                ASD_log(ASD_LogLevel_Error, stream, option, "Selecting Bus %d",
                        bus);
                status = i3c_open_device_drivers(state, bus);
            }
        }
        else if (i3c_bus_allowed(state, bus))
        {
            i3c_close_device_drivers(state);
            ASD_log(ASD_LogLevel_Error, stream, option, "Selecting Bus %d",
                    bus);
            status = i3c_open_device_drivers(state, bus);
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

STATUS i3c_set_sclk(I3C_Handler* state, uint16_t sclk)
{
    if (state == NULL || !i3c_enabled(state))
        return ST_ERR;
    return ST_OK;
}

STATUS i3c_read_write(I3C_Handler* state, void* msg_set)
{
    STATUS status = ST_OK;
    if (state == NULL || msg_set == NULL || !i3c_enabled(state))
        return ST_ERR;

    // Convert i2c packet to i3c request format
    struct i2c_rdwr_ioctl_data* ioctl_data = msg_set;
    struct i3c_ioc_priv_xfer* xfers;
    int handle = UNINITIALIZED_I3C_DRIVER_HANDLE;
    int addr = UNINITIALIZED_I3C_DRIVER_HANDLE;
    xfers =
        (struct i3c_ioc_priv_xfer*)calloc(ioctl_data->nmsgs, sizeof(*xfers));

    if (xfers == NULL)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "I3C_RDWR memory allocation failed");
        return ST_ERR;
    }

    for (int i = 0; i < ioctl_data->nmsgs; i++)
    {
        xfers[i].len = ioctl_data->msgs[i].len;
        xfers[i].data = ioctl_data->msgs[i].buf;
        xfers[i].rnw = (ioctl_data->msgs[i].flags & I2C_M_RD) ? 1 : 0;
        if (handle == UNINITIALIZED_I3C_DRIVER_HANDLE)
        {
            addr = ioctl_data->msgs[i].addr;
            if (addr >= 0 && addr < i3C_MAX_DEV_HANDLERS)
            {
                handle = state->i3c_driver_handlers[addr];
                ASD_log(ASD_LogLevel_Debug, stream, option,
                        "I3C_RDWR ioctl addr 0x%x handle %d len %d rnw %d",
                        addr, handle, xfers[i].len, xfers[i].rnw);
                ASD_log_buffer(ASD_LogLevel_Debug, stream, option,
                               xfers[i].data, xfers[i].len, "I3cBuf");
            }
            else
            {
                ASD_log(ASD_LogLevel_Error, stream, option,
                        "I3C_RDWR wrong addr %d", addr);
            }
        }
    }

    if (handle != UNINITIALIZED_I3C_DRIVER_HANDLE)
    {
        int ret = ioctl(handle, I3C_IOC_PRIV_XFER(ioctl_data->nmsgs), xfers);
        if (ret < 0)
        {
            ASD_log(ASD_LogLevel_Error, stream, option,
                    "I3C_RDWR ioctl returned %d - %d - %s", ret, errno,
                    strerror(errno));
            status = ST_ERR;
        }
    }
    else
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "I3C_RDWR invalid handle for addr %x", addr);
        status = ST_ERR;
    }

    if (xfers)
        free(xfers);

    return status;
}

static bool i3c_enabled(I3C_Handler* state)
{
    return state->config->enable_i3c;
}

static bool i3c_device_drivers_opened(I3C_Handler* state)
{
    for (int i = 0; i < i3C_MAX_DEV_HANDLERS; i++)
    {
        if (state->i3c_driver_handlers[i] != UNINITIALIZED_I3C_DRIVER_HANDLE)
        {
            return true;
        }
    }
    return false;
}

/* Under /sys/bus/platform/devices select the buses which have *.i3c* in
 * their name and the of_node directory contains jdec-spd. In case of
 * Hub based platforms (BHS) a further check of hub@70,3C000000100 under
 * of_node directory is required to confirm the given bus is i3c-spd.
 */
static STATUS is_spd_bus(char * bus_name, bool * ret)
{
    FILE * fd = NULL;
    char jedec_name[MAX_I3C_DEV_FILENAME];
    char spd_hub_name[MAX_I3C_DEV_FILENAME];

    if(bus_name == NULL || ret == NULL)
        return ST_ERR;

    // Check if i3c device has jdec-spd file
    snprintf(jedec_name, MAX_I3C_DEV_FILENAME, "%s%s/of_node/jdec-spd",
             I3C_SYS_BUS_DEVICES, bus_name);
    fd = fopen(jedec_name, "rb");
    if (fd == NULL)
    {
        ASD_log(ASD_LogLevel_Debug, stream, option,
                "No SPD BUS: Can't find %s", jedec_name);
        return ST_ERR;
    }
    ASD_log(ASD_LogLevel_Debug, stream, option, "SPD device found: %s",
            jedec_name);
    fclose(fd);
    fd = NULL;

    // Check if i3c device is a hub
    snprintf(spd_hub_name, MAX_I3C_DEV_FILENAME,
             "%s%s/of_node/hub@70,3C000000100",
             I3C_SYS_BUS_DEVICES, bus_name);
    fd = fopen(spd_hub_name, "rb");
    if (fd != NULL)
    {
        ASD_log(ASD_LogLevel_Debug, stream, option,
                "No SPD BUS: Dev is a HUB, found %s", spd_hub_name);
        *ret = false;
        fclose(fd);
    }
    else
    {
        ASD_log(ASD_LogLevel_Debug, stream, option, "Dev %s is an SPD bus",
                spd_hub_name);
        *ret = true;
    }

    return ST_OK;
}

/* Once the binding is successful, we should be able to see all the available
 * devices connected to the bus which can be viewed under
 * /sys/bus/i3c/devices and also under /dev.
 * These devices would have an index assigned by Linux depending on when and
 * the order in which the binding and enumeration of devices was done.
 * /sys/bus/i3c/devices would have directories in the form of
 * i3c-<bound index>.
 */
static STATUS get_bound_index(char * bus_name, int * boundIndex)
{
    char * endptr = NULL;
    char * str = NULL;
    long val = 0;

    if(bus_name == NULL || boundIndex == NULL)
        return ST_ERR;

    str = &bus_name[4];
    val = strtol(str, &endptr, 10);

    // check if strtol failed
    if (endptr == str || *endptr != '\0' ||
        ((val == LONG_MIN || val == LONG_MAX) && errno == ERANGE))
        return ST_ERR;

    *boundIndex = (int) val;

    return ST_OK;
}

/* /sys/bus/i3c/devices/i3c-<bound index>/of_name/name will give the actual
 * platform index.
 */
static STATUS get_platform_index(char * bus_name, uint8_t * platIndex)
{
    STATUS status = ST_ERR;
    char bus_filename[MAX_I3C_DEV_FILENAME];
    char buffer[MAX_I3C_DEV_FILENAME];
    regex_t platformNameRegex;
    int ret_value = 0;
    FILE * fd = NULL;
    char * endptr = NULL;
    char * str = NULL;
    char * pstr = NULL;
    long length = 0;
    long val = 0;

    if(bus_name == NULL || platIndex == NULL)
        return ST_ERR;

    // calling regcomp() function to create regex and check if the compilation
    // was successful
    ret_value = regcomp(&platformNameRegex, "i3c[0-9]+", REG_EXTENDED);

    if (ret_value != 0)
    {
        ASD_log(ASD_LogLevel_Debug, stream, option,
                "platformNameRegex compilation Process failed");
        return ST_ERR;
    }

    snprintf(bus_filename, MAX_I3C_DEV_FILENAME, "%s%s/of_node/name",
             I3C_SYS_BUS_DEVICES, bus_name);

    fd = fopen(bus_filename, "rb");
    if (fd == NULL)
    {
        ASD_log(ASD_LogLevel_Debug, stream, option,
                "Can't open i3c bus file name %s", bus_filename);
    }
    else
    {
        // Read Linux assigned bus from bus_filename
        fseek (fd, 0, SEEK_END);
        length = ftell (fd);
        fseek (fd, 0, SEEK_SET);

        if (sizeof(buffer) > length)
        {
            if(fread (buffer, 1, length, fd) == length)
            {
                buffer[length-1] = 0x0; // Null character
                str = buffer;
                ret_value = regexec(&platformNameRegex,
                                    str, 0, NULL, 0);
                if (ret_value == 0)
                {
                    ASD_log(ASD_LogLevel_Debug, stream, option,
                            "%s pattern found in %s", buffer,
                            bus_filename);

                    pstr = &buffer[3];
                    val = strtol(pstr, &endptr, 10);
                    // check if strtol failed
                    if (endptr == pstr || *endptr != '\0' ||
                        ((val == LONG_MIN || val == LONG_MAX) &&
                        errno == ERANGE))
                    {
                        ASD_log(ASD_LogLevel_Debug, stream, option,
                                "strtol failed for platIndex");
                    }
                    else
                    {
                        if (val < MAX_IxC_BUSES)
                        {
                            *platIndex = (uint8_t) val;
                            ASD_log(ASD_LogLevel_Debug, stream, option,
                                    "platIndex = %d", *platIndex);
                            status = ST_OK;
                        }
                        else
                        {
                            ASD_log(ASD_LogLevel_Debug, stream, option,
                                    "platIndex %d out of bus boundaries", val);
                        }
                    }
                }
                else
                {
                    ASD_log(ASD_LogLevel_Debug, stream, option,
                            "i3cX pattern not found in %s", bus_filename);
                }
            }
            else
            {
                ASD_log(ASD_LogLevel_Debug, stream, option, "%s fread error",
                        bus_filename);
            }
        }
        fclose(fd);
    }
    // free REGEX allocated memory
    regfree(&platformNameRegex);

    return status;
}

/* The bound index and the platform index described as above are used to form
 * the mapping.
 * Eg: /sys/bus/i3c/devices/i3c-0 is the device file and
 * /sys/bus/i3c/devices/i3c-0/of_name/name contains i3c3 Implies
 * BoundIndex 0 => platIndex 3. Which means all the devices enumerated as
 * 0-XXXXXXXX are under platform bus 3. i.e 1e7a5000.i3c3.In case of hub-based
 * system, platform index 3 implies the devices are under downstream port 3.
 */
STATUS create_spd_mapping(I3C_Handler* state)
{
    STATUS status = ST_ERR;

    DIR *d;
    struct dirent *dir;
    char bus_name[MAX_I3C_DEV_FILENAME];

    int fd = 0;
    char * str;
    char * endptr = NULL;

    uint8_t platIndex = 0;
    int boundIndex = 0;
    bool spd_bus_found = false;
    int ret_value = 0;
    uint8_t spd_bus_count = 0;

    regex_t busRegex;

    if(state == NULL)
        return ST_ERR;

    // cleanup SPD map
    for (int i = 0; i < MAX_IxC_BUSES; i++)
    {
        state->spd_map[i] = UNINITIALIZED_SPD_BUS_MAP_ENTRY;
    }

    // calling regcomp() function to create regex and check if the compilation
    // was successful
    ret_value = regcomp(&busRegex, "i3c-[0-9]+", REG_EXTENDED);
    if (ret_value != 0)
    {
        ASD_log(ASD_LogLevel_Debug, stream, option,
                "busRegex compilation Process failed");
        return ST_ERR;
    }

    // For all files in /sys/bus/i3c/devices/
    d = opendir(I3C_SYS_BUS_DEVICES);
    if (d)
    {
        while (((dir = readdir(d)) != NULL))
        {
            if (dir->d_type != DT_DIR)
            {
                ret_value = regexec(&busRegex, dir->d_name, 0, NULL, 0);
                if (ret_value == 0)
                {
                    status = is_spd_bus(dir->d_name, &spd_bus_found);
                    if(status != ST_OK || !spd_bus_found)
                        continue;

                    status = get_bound_index(dir->d_name, &boundIndex);
                    if (status != ST_OK)
                    {
                        ASD_log(ASD_LogLevel_Debug, stream, option,
                                "failed to get boundIndex on %s",
                                dir->d_name);
                        continue;
                    }
                    ASD_log(ASD_LogLevel_Debug, stream, option,
                            "boundIndex = %d", boundIndex);

                    status = get_platform_index(dir->d_name, &platIndex);
                    if (status != ST_OK)
                    {
                        ASD_log(ASD_LogLevel_Debug, stream, option,
                                "failed to get platIndex on %s", dir->d_name);
                        continue;
                    }

                    if (platIndex < MAX_IxC_BUSES)
                    {
                        state->spd_map[platIndex] = boundIndex;
                        spd_bus_count++;
                        ASD_log(ASD_LogLevel_Debug, stream, option,
                                "spd_map[%d] = %d",  platIndex, boundIndex);
                    }
                    else
                    {
                        ASD_log(ASD_LogLevel_Debug, stream, option,
                                "platIndex out of bus boundaries\r\n");
                        continue;
                    }

                }
            }
        }
        status = (spd_bus_count > 0) ? ST_OK : ST_ERR;
        closedir(d);
    }
    // free REGEX allocated memory
    regfree(&busRegex);

    ASD_log(ASD_LogLevel_Debug, stream, option,"spd map = {");
    for (int i=0; i<MAX_IxC_BUSES; i++)
    {
        if (state->spd_map[i] == UNINITIALIZED_SPD_BUS_MAP_ENTRY)
        {
            ASD_log(ASD_LogLevel_Debug, stream, option,
                    "UNINITIALIZED_SPD_BUS_MAP_ENTRY,");
        }
        else
        {
            ASD_log(ASD_LogLevel_Debug, stream, option,"%d,",
            state->spd_map[i]);
        }
    }
    ASD_log(ASD_LogLevel_Debug, stream, option,"}");

    return status;
}

static STATUS i3c_open_device_drivers(I3C_Handler* state, uint8_t bus)
{
    STATUS status = ST_ERR;
    char i3c_dev[MAX_I3C_DEV_FILENAME];

    if (bus >= MAX_IxC_BUSES)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "bus %d out of platform bounds", bus);
        return status;
    }

    status = create_spd_mapping(state);
    if (status != ST_OK ||
        state->spd_map[bus] == UNINITIALIZED_SPD_BUS_MAP_ENTRY)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "spd map couldn't be created for bus %d", bus);
        return ST_ERR;
    }

    status = ST_ERR;
    for (int i = 0; i < i3C_MAX_DEV_HANDLERS; i++)
    {
        snprintf(i3c_dev, sizeof(i3c_dev), "/dev/i3c-%d-3c00000000%x",
                 state->spd_map[bus], i);
        state->i3c_driver_handlers[i] = open(i3c_dev, O_RDWR);
        if (state->i3c_driver_handlers[i] != UNINITIALIZED_I3C_DRIVER_HANDLE)
        {
            ASD_log(ASD_LogLevel_Debug, stream, option,
                    "open device driver %s for bus %d handle %d", i3c_dev, bus,
                    state->i3c_driver_handlers[i]);
            status = ST_OK;
        }
        else
        {
            ASD_log(ASD_LogLevel_Debug, stream, option, "Can't open %s",
                    i3c_dev);
        }
    }

    state->i3c_bus = bus;

    return status;
}

static void i3c_close_device_drivers(I3C_Handler* state)
{
    for (int i = 0; i < i3C_MAX_DEV_HANDLERS; i++)
    {
        if (state->i3c_driver_handlers[i] != UNINITIALIZED_I3C_DRIVER_HANDLE)
        {
            close(state->i3c_driver_handlers[i]);
            ASD_log(ASD_LogLevel_Debug, stream, option,
                    "closing dev handler %x", i);
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
