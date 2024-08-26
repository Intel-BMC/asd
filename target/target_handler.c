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

#include "target_handler.h"

#include <fcntl.h>
#include <gpiod.h>
#include <poll.h>
#include <safe_mem_lib.h>
#include <safe_str_lib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include "i3c_debug_handler.h"
#include "asd_common.h"
#include "gpio.h"
#include "logging.h"

#define JTAG_CLOCK_CYCLE_MILLISECONDS 1000
#define GPIOD_CONSUMER_LABEL "ASD"
#define GPIOD_DEV_ROOT_FOLDER "/dev/"
#define GPIOD_DEV_ROOT_FOLDER_STRLEN strnlen_s(GPIOD_DEV_ROOT_FOLDER, 6)

static inline void get_pin_events(Target_Control_GPIO gpio, short* events)
{
#ifdef GPIO_SYSFS_SUPPORT_DEPRECATED
    if (gpio.type == PIN_GPIO)
    {
        *events = POLL_GPIO;
    }
    else
#endif
        if (gpio.type == PIN_GPIOD)
    {
        *events = POLLIN | POLLPRI;
    }
}

static inline void string_to_enum(char* str, const char* (*enum_strings)[],
                                  int arr_size, int* val)
{
    int cmp = 0;

    if ((str != NULL) && (enum_strings != NULL) && (val != NULL))
    {
        for (int index = 0; index < arr_size; index++)
        {
            strcmp_s(str, TARGET_JSON_MAX_LABEL_SIZE, (*enum_strings)[index],
                     &cmp);
            if (cmp == 0)
            {
                *val = index;
                return;
            }
        }
    }
}

STATUS initialize_gpios(Target_Control_Handle* state);
#ifdef GPIO_SYSFS_SUPPORT_DEPRECATED
STATUS initialize_gpio(Target_Control_GPIO* gpio);
STATUS find_gpio_base(char* gpio_name, int* gpio_base);
STATUS find_gpio(char* gpio_name, int* gpio_number);
#endif
STATUS deinitialize_gpios(Target_Control_Handle* state);
STATUS on_power_event(Target_Control_Handle* state, ASD_EVENT* event);
STATUS on_platform_reset_event(Target_Control_Handle* state, ASD_EVENT* event);
STATUS on_prdy_event(Target_Control_Handle* state, ASD_EVENT* event);
STATUS on_xdp_present_event(Target_Control_Handle* state, ASD_EVENT* event);
STATUS initialize_gpiod(Target_Control_GPIO* gpio);
STATUS platform_init(Target_Control_Handle* state);

static const ASD_LogStream stream = ASD_LogStream_Pins;
static const ASD_LogOption option = ASD_LogOption_None;

#ifdef GPIO_SYSFS_SUPPORT_DEPRECATED
static STATUS read_gpio_pin(struct Target_Control_Handle * state, int gpio_index, int* value)
{
    STATUS result = ST_ERR;
    if (state != NULL && value != NULL)
    {
        Target_Control_GPIO gpio = state->gpios[gpio_index];
        if (gpio.type == PIN_GPIO)
        {
            result = gpio_get_value(gpio.fd, value);
        }
        ASD_log(ASD_LogLevel_Debug, stream, option, "read_gpio_pin %d", *value);
    }
    return result;
}
#endif

static STATUS read_gpiod_pin(struct Target_Control_Handle * state, int gpio_index, int* value)
{
    STATUS result = ST_ERR;
    if (state != NULL && value != NULL)
    {
        Target_Control_GPIO gpio = state->gpios[gpio_index];
        if (gpio.type == PIN_GPIOD)
        {
            *value = gpiod_line_get_value(gpio.line);
            if (*value == -1)
                result = ST_ERR;
            else
                result = ST_OK;
        }
        ASD_log(ASD_LogLevel_Debug, stream, option, "read_gpiod_pin %d", *value);
    }
    return result;
}

static STATUS read_pin_none(struct Target_Control_Handle * state, int gpio_index, int* value)
{
    STATUS result = ST_ERR;
    if (value != NULL)
    {
        *value = 0;
        ASD_log(ASD_LogLevel_Debug, stream, option, "read_pin_none %d", *value);
    }
    return ST_OK;
}

static STATUS read_dbus_pwrgood_pin(struct Target_Control_Handle * state, int gpio_index, int* value)
{
    STATUS result = ST_ERR;
    if (state != NULL && value != NULL)
    {
        result = dbus_get_powerstate(state->dbus, value);
        if (result != ST_OK)
        {
            ASD_log(ASD_LogLevel_Error, stream, option, "failed to read powerstate from dbus");
            // If asd cannot read power status from dbus assume it is on
            *value = 1;
            result = ST_OK;
        }
        ASD_log(ASD_LogLevel_Debug, stream, option, "read_dbus_pwrgood_pin %d", *value);
    }
    return result;
}

#ifdef GPIO_SYSFS_SUPPORT_DEPRECATED
static STATUS write_gpio_pin(struct Target_Control_Handle * state, int gpio_index, int value)
{
    STATUS result = ST_ERR;
    if (state != NULL)
    {
        Target_Control_GPIO gpio = state->gpios[gpio_index];
        if (gpio.type == PIN_GPIO)
        {
            result = gpio_set_value(gpio.fd, value);
        }
        ASD_log(ASD_LogLevel_Debug, stream, option, "write_gpio_pin %d %s",
                value, result == ST_OK ? "ST_OK" : "ST_ERR");
    }
    return result;
}
#endif

static STATUS write_gpiod_pin(struct Target_Control_Handle * state, int gpio_index, int value)
{
    STATUS result = ST_ERR;
    int rv = 0;
    if (state != NULL)
    {
        Target_Control_GPIO gpio = state->gpios[gpio_index];
        if (gpio.type == PIN_GPIOD)
        {
            rv = gpiod_line_set_value(gpio.line, value);
            if (rv == 0)
                result = ST_OK;
            else
                result = ST_ERR;
        }
        ASD_log(ASD_LogLevel_Debug, stream, option, "write_gpiod_pin %d %s",
                value, result == ST_OK ? "ST_OK" : "ST_ERR");
    }
    return result;
}

static STATUS write_pin_none(struct Target_Control_Handle * state, int gpio_index, int value)
{
    ASD_log(ASD_LogLevel_Debug, stream, option, "write_pin_none");
    return ST_OK;
}

static STATUS write_dbus_power_button(struct Target_Control_Handle * state, int gpio_index, int value)
{
    STATUS result = ST_OK;
    ASD_log(ASD_LogLevel_Debug, stream, option, "write_dbus_power_button %d", value);
    if (state != NULL)
    {
        if (value)
        {
            int powerstate = 0;
            Target_Control_GPIO gpio_pwrgood = state->gpios[BMC_CPU_PWRGD];
            result = gpio_pwrgood.read(state, BMC_CPU_PWRGD, &powerstate);
            if (powerstate)
            {
                result = dbus_power_off(state->dbus);
                ASD_log(ASD_LogLevel_Info, stream, option, "dbus_power_off");
            }
            else
            {
                result = dbus_power_on(state->dbus);
                ASD_log(ASD_LogLevel_Info, stream, option, "dbus_power_on");
            }
        }
    }
    return result;
}

static STATUS write_dbus_reset(struct Target_Control_Handle * state, int gpio_index, int value)
{
    STATUS result = ST_ERR;
    ASD_log(ASD_LogLevel_Debug, stream, option, "write_dbus_reset %d", value);
    if (state != NULL)
    {
        if (value)
        {
            ASD_log(ASD_LogLevel_Info, stream, option, "dbus_power_reset");
            result = dbus_power_reset(state->dbus);
        }
        else
            result = ST_OK;
    }
    return result;
}

Target_Control_Handle* TargetHandler()
{
    Target_Control_Handle* state =
        (Target_Control_Handle*)malloc(sizeof(Target_Control_Handle));

    if (state == NULL)
        return NULL;

    state->dbus = dbus_helper();
    if (state->dbus == NULL)
    {
        // Continue with target handler creation even without dbus
        ASD_log(ASD_LogLevel_Error, stream, option, "dbus cannot be allocated");
    }

    state->spp_handler = NULL;

    state->initialized = false;

    explicit_bzero(&state->gpios, sizeof(state->gpios));

    for (int i = 0; i < NUM_GPIOS; i++)
    {
        state->gpios[i].number = -1;
        state->gpios[i].fd = -1;
        state->gpios[i].line = NULL;
        state->gpios[i].chip = NULL;
        state->gpios[i].read = NULL;
        state->gpios[i].write = NULL;
        state->gpios[i].handler = NULL;
        state->gpios[i].active_low = false;
        state->gpios[i].type = PIN_GPIOD;
    }

    /*******************************************************************************
        Not all pins are defined for each applicable platform.
        Please see At-Scale Debug Documentation Appendix on Pin Descriptions.
    *******************************************************************************/

    strcpy_s(state->gpios[BMC_TCK_MUX_SEL].name,
             sizeof(state->gpios[BMC_TCK_MUX_SEL].name), "TCK_MUX_SEL");
    state->gpios[BMC_TCK_MUX_SEL].direction = GPIO_DIRECTION_LOW;
    state->gpios[BMC_TCK_MUX_SEL].edge = GPIO_EDGE_NONE;

    strcpy_s(state->gpios[BMC_PREQ_N].name,
             sizeof(state->gpios[BMC_PREQ_N].name), "PREQ_N");
    state->gpios[BMC_PREQ_N].direction = GPIO_DIRECTION_HIGH;
    state->gpios[BMC_PREQ_N].edge = GPIO_EDGE_NONE;
    state->gpios[BMC_PREQ_N].active_low = true;

    strcpy_s(state->gpios[BMC_PRDY_N].name,
             sizeof(state->gpios[BMC_PRDY_N].name), "PRDY_N");
    state->gpios[BMC_PRDY_N].direction = GPIO_DIRECTION_IN;
    state->gpios[BMC_PRDY_N].edge = GPIO_EDGE_FALLING;
    state->gpios[BMC_PRDY_N].active_low = true;
    state->gpios[BMC_PRDY_N].handler = on_prdy_event;

    strcpy_s(state->gpios[BMC_RSMRST_B].name,
             sizeof(state->gpios[BMC_RSMRST_B].name), "RSMRST_N");
    state->gpios[BMC_RSMRST_B].direction = GPIO_DIRECTION_IN;
    state->gpios[BMC_RSMRST_B].edge = GPIO_EDGE_NONE;

    // BMC_CPU_PWRGD pin mapping is platform dependent. Please check
    // At-scale-debug Software Guide to confirm the right connection
    // for your system.
    strcpy_s(state->gpios[BMC_CPU_PWRGD].name,
             sizeof(state->gpios[BMC_CPU_PWRGD].name), "SIO_POWER_GOOD");
    state->gpios[BMC_CPU_PWRGD].direction = GPIO_DIRECTION_IN;
    state->gpios[BMC_CPU_PWRGD].edge = GPIO_EDGE_BOTH;
    state->gpios[BMC_CPU_PWRGD].type = PIN_DBUS;
    state->gpios[BMC_CPU_PWRGD].read = read_dbus_pwrgood_pin;
    state->gpios[BMC_CPU_PWRGD].handler = on_power_event;

    strcpy_s(state->gpios[BMC_PLTRST_B].name,
             sizeof(state->gpios[BMC_PLTRST_B].name), "PLTRST_N");
    state->gpios[BMC_PLTRST_B].direction = GPIO_DIRECTION_IN;
    state->gpios[BMC_PLTRST_B].edge = GPIO_EDGE_BOTH;
    state->gpios[BMC_PLTRST_B].active_low = true;
    state->gpios[BMC_PLTRST_B].handler = on_platform_reset_event;

    strcpy_s(state->gpios[BMC_SYSPWROK].name,
             sizeof(state->gpios[BMC_SYSPWROK].name), "SYSPWROK");
    state->gpios[BMC_SYSPWROK].direction = GPIO_DIRECTION_HIGH;
    state->gpios[BMC_SYSPWROK].edge = GPIO_EDGE_NONE;
    state->gpios[BMC_SYSPWROK].active_low = true;

    strcpy_s(state->gpios[BMC_PWR_DEBUG_N].name,
             sizeof(state->gpios[BMC_PWR_DEBUG_N].name), "PWR_DEBUG_N");
    state->gpios[BMC_PWR_DEBUG_N].direction = GPIO_DIRECTION_HIGH;
    state->gpios[BMC_PWR_DEBUG_N].edge = GPIO_EDGE_NONE;
    state->gpios[BMC_PWR_DEBUG_N].active_low = true;

    strcpy_s(state->gpios[BMC_DEBUG_EN_N].name,
             sizeof(state->gpios[BMC_DEBUG_EN_N].name), "DEBUG_EN_N");
    state->gpios[BMC_DEBUG_EN_N].direction = GPIO_DIRECTION_HIGH;
    state->gpios[BMC_DEBUG_EN_N].edge = GPIO_EDGE_NONE;
    state->gpios[BMC_DEBUG_EN_N].active_low = true;

    strcpy_s(state->gpios[BMC_XDP_PRST_IN].name,
             sizeof(state->gpios[BMC_XDP_PRST_IN].name), "XDP_PRST_N");
    state->gpios[BMC_XDP_PRST_IN].direction = GPIO_DIRECTION_IN;
    state->gpios[BMC_XDP_PRST_IN].active_low = true;
    state->gpios[BMC_XDP_PRST_IN].edge = GPIO_EDGE_BOTH;
    state->gpios[BMC_XDP_PRST_IN].handler = on_xdp_present_event;

    strcpy_s(state->gpios[POWER_BTN].name,
             sizeof(state->gpios[POWER_BTN].name), "POWER_BTN");
    state->gpios[POWER_BTN].direction = GPIO_DIRECTION_HIGH;
    state->gpios[POWER_BTN].edge = GPIO_EDGE_NONE;
    state->gpios[POWER_BTN].active_low = true;
    state->gpios[POWER_BTN].type = PIN_DBUS;
    state->gpios[POWER_BTN].write = write_dbus_power_button;

    strcpy_s(state->gpios[RESET_BTN].name,
             sizeof(state->gpios[RESET_BTN].name), "RESET_BTN");
    state->gpios[RESET_BTN].direction = GPIO_DIRECTION_HIGH;
    state->gpios[RESET_BTN].edge = GPIO_EDGE_NONE;
    state->gpios[RESET_BTN].active_low = true;
    state->gpios[RESET_BTN].type = PIN_DBUS;
    state->gpios[RESET_BTN].write = write_dbus_reset;

    // BMC_PWRGD2 pin mapping is platform dependent. Please check
    // At-scale-debug Software Guide to confirm the right connection
    // for your system.
    strcpy_s(state->gpios[BMC_PWRGD2].name,
             sizeof(state->gpios[BMC_PWRGD2].name), "BMC_PWRGD2");
    state->gpios[BMC_PWRGD2].direction = GPIO_DIRECTION_IN;
    state->gpios[BMC_PWRGD2].edge = GPIO_EDGE_BOTH;
    state->gpios[BMC_PWRGD2].handler = on_power2_event;

    // BMC_PWRGD3 pin mapping is platform dependent. Please check
    // At-scale-debug Software Guide to confirm the right connection
    // for your system.
    strcpy_s(state->gpios[BMC_PWRGD3].name,
             sizeof(state->gpios[BMC_PWRGD3].name), "BMC_PWRGD3");
    state->gpios[BMC_PWRGD3].direction = GPIO_DIRECTION_IN;
    state->gpios[BMC_PWRGD3].edge = GPIO_EDGE_BOTH;
    state->gpios[BMC_PWRGD3].handler = on_power3_event;

    platform_init(state);

    /*******************************************************************************
        Initialize Read and Write handlers based on pin type
    *******************************************************************************/

    for (int i = 0; i < NUM_GPIOS; i++)
    {
        switch(state->gpios[i].type)
        {
#ifdef GPIO_SYSFS_SUPPORT_DEPRECATED
            case PIN_GPIO:
                if (state->gpios[i].read == NULL)
                    state->gpios[i].read = (TargetReadFunctionPtr) read_gpio_pin;
                if (state->gpios[i].write == NULL)
                    state->gpios[i].write = (TargetWriteFunctionPtr) write_gpio_pin;
                break;
#endif
            case PIN_GPIOD:
                if (state->gpios[i].read == NULL)
                    state->gpios[i].read = (TargetReadFunctionPtr) read_gpiod_pin;
                if (state->gpios[i].write == NULL)
                    state->gpios[i].write = (TargetWriteFunctionPtr) write_gpiod_pin;
                break;

             default:
                if (state->gpios[i].read == NULL)
                    state->gpios[i].read = (TargetReadFunctionPtr) read_pin_none;
                if (state->gpios[i].write == NULL)
                    state->gpios[i].write = (TargetWriteFunctionPtr) write_pin_none;
        }
    }

    state->event_cfg.break_all = false;
    state->event_cfg.report_MBP = false;
    state->event_cfg.report_PLTRST = false;
    state->event_cfg.report_PRDY = false;
    state->event_cfg.reset_break = false;
    state->xdp_present = false;

    // Change is_controller_probe accordingly on your BMC implementations.
    // <MODIFY>
    state->is_controller_probe = false;
    // </MODIFY>

    return state;
}

STATUS platform_override_gpio(const Dbus_Handle* dbus, char* interface,
                              Target_Control_GPIO* gpio)
{
    STATUS result = ST_ERR;
    int* enum_val = NULL;
    bool* bool_val = NULL;
    int match = 0;
    union out_data
    {
        bool bval;
        char str[MAX_PLATFORM_PATH_SIZE];
    } rval;

    static const data_json_map TARGET_JSON_MAP[] = {
        {"PinName", 's', offsetof(Target_Control_GPIO, name), NULL, 0},
        {"PinDirection", 's', offsetof(Target_Control_GPIO, direction),
         &GPIO_DIRECTION_STRINGS,
         sizeof(GPIO_DIRECTION_STRINGS) / sizeof(char*)},
        {"PinEdge", 's', offsetof(Target_Control_GPIO, edge),
         &GPIO_EDGE_STRINGS, sizeof(GPIO_EDGE_STRINGS) / sizeof(char*)},
        {"PinActiveLow", 'b', offsetof(Target_Control_GPIO, active_low), NULL,
         0},
        {"PinType", 's', offsetof(Target_Control_GPIO, type), &PIN_TYPE_STRINGS,
         sizeof(PIN_TYPE_STRINGS) / sizeof(char*)}};

    if ((dbus == NULL) || (interface == NULL))
    {
        return ST_ERR;
    }

    // Search for Target_Control_GPIO settings in the interface
    for (int i = 0; i < sizeof(TARGET_JSON_MAP) / sizeof(data_json_map); i++)
    {
        result =
            dbus_read_asd_config(dbus, interface, TARGET_JSON_MAP[i].fname_json,
                                 TARGET_JSON_MAP[i].ftype, &rval);
        if (result == ST_OK)
        {
            switch (TARGET_JSON_MAP[i].ftype)
            {
                case 'b':
                    // Copy active_low
                    bool_val = (bool*)((char*)gpio + TARGET_JSON_MAP[i].offset);
#ifdef ENABLE_DEBUG_LOGGING
                    ASD_log(ASD_LogLevel_Trace, stream, option, "%s = %d",
                            TARGET_JSON_MAP[i].fname_json, rval.bval);
#endif
                    *bool_val = rval.bval;
                    break;
                case 's':
                    // Copy GPIO name
                    match = 0;
                    strcmp_s(TARGET_JSON_MAP[i].fname_json,
                             TARGET_JSON_MAX_LABEL_SIZE, "PinName", &match);
                    if (match == 0)
                    {
                        // Clear Pin Name
                        memset_s((char*)gpio + TARGET_JSON_MAP[i].offset,
                                 PIN_NAME_MAX_SIZE, 0x0, PIN_NAME_MAX_SIZE);
                        // Copy Pin Name from dbus object to
                        if(memcpy_s((char*)gpio + TARGET_JSON_MAP[i].offset,
                                 PIN_NAME_MAX_SIZE, &rval.str,
                                 strnlen_s(rval.str, PIN_NAME_MAX_SIZE)))
                        {
                            ASD_log(ASD_LogLevel_Error, stream, option,
                                    "memcpy_s: Pin Name to target failure");
                            return ST_ERR;
                        }
                        break;
                    }
                    // Convert strings to enum values and set values
                    enum_val = (int*)((char*)gpio + TARGET_JSON_MAP[i].offset);
                    string_to_enum(rval.str, TARGET_JSON_MAP[i].enum_strings,
                                   TARGET_JSON_MAP[i].arr_size, enum_val);
                    break;
                default:
                    return ST_ERR;
            }
        }
    }
    return result;
}

STATUS platform_init(Target_Control_Handle* state)
{
    STATUS result = ST_ERR;
    Dbus_Handle* dbus = dbus_helper();
    char interfaces[NUM_GPIOS][MAX_PLATFORM_PATH_SIZE];

    // Read configuration from dbus
    if (dbus)
    {
        // Connect to the system bus
        int retcode = sd_bus_open_system(&dbus->bus);
        if (retcode >= 0)
        {
            dbus->fd = sd_bus_get_fd(dbus->bus);
            if (dbus->fd < 0)
            {
#ifdef ENABLE_DEBUG_LOGGING
                ASD_log(ASD_LogLevel_Error, stream, option,
                        "sd_bus_get_fd failed: %d", retcode);
#endif
                result = ST_ERR;
            }
            else
            {
                // get interface paths
                explicit_bzero(interfaces, sizeof(interfaces));
                dbus_get_asd_interface_paths(dbus, TARGET_CONTROL_GPIO_STRINGS,
                                             interfaces, NUM_GPIOS);
                for (int i = 0; i < NUM_GPIOS; i++)
                {
                    if (strnlen_s(interfaces[i], MAX_PLATFORM_PATH_SIZE) != 0)
                    {
                        ASD_log(ASD_LogLevel_Info, stream, option,
                                "interface[%d]: %s - %s", i,
                                TARGET_CONTROL_GPIO_STRINGS[i], interfaces[i]);
                        // Override Target_Control_GPIO settings for pin using
                        // entity mnager ASD settings.
                        platform_override_gpio(dbus, interfaces[i],
                                               &state->gpios[i]);
                    }
                }
#ifdef ENABLE_DEBUG_LOGGING
                ASD_log_buffer(ASD_LogLevel_Debug, stream, option,
                               (char*)&state->gpios, sizeof(state->gpios),
                               "JSON");
#endif
            }
            dbus_deinitialize(dbus);
        }
        else
        {
#ifdef ENABLE_DEBUG_LOGGING
            ASD_log(ASD_LogLevel_Error, stream, option,
                    "sd_bus_open_system failed: %d", retcode);
#endif
            result = ST_ERR;
        }
        free(dbus);
    }
    return result;
}

STATUS target_initialize(Target_Control_Handle* state, bool xdp_fail_enable)
{
    STATUS result;
    int value = 0;
    if (state == NULL || state->initialized)
        return ST_ERR;

    result = initialize_gpios(state);

    if (result == ST_OK)
    {

        Target_Control_GPIO xdp_gpio = state->gpios[BMC_XDP_PRST_IN];
        result = xdp_gpio.read(state, BMC_XDP_PRST_IN, &value);
        if (result != ST_OK)
        {
            ASD_log(ASD_LogLevel_Error, stream, option,
                    "Failed check XDP state or XDP not available");
        }
        else if (value == 1)
        {
            state->xdp_present = true;
            if (xdp_fail_enable)
            {
                ASD_log(ASD_LogLevel_Error, stream, option,
                        "Exiting due XDP presence detected");
                result = ST_ERR;
            }
            ASD_log(ASD_LogLevel_Error, stream, option,
                    "XDP presence detected");
        }
    }

    // specifically drive debug enable to assert
    if (result == ST_OK)
    {
        Target_Control_GPIO dbg_en_gpio = state->gpios[BMC_DEBUG_EN_N];
        result = dbg_en_gpio.write(state, BMC_DEBUG_EN_N, 1);
        if (result != ST_OK)
        {
            ASD_log(ASD_LogLevel_Error, stream, option,
                    "Failed to assert debug enable");
        }
    }

    if (result == ST_OK)
    {
        result = dbus_initialize(state->dbus);
    }

    if (result == ST_OK)
        state->initialized = true;
    else
        deinitialize_gpios(state);

    return result;
}

STATUS initialize_gpios(Target_Control_Handle* state)
{
    STATUS result = ST_OK;
    STATUS status = ST_ERR;
    int i;

    for (i = 0; i < NUM_GPIOS; i++)
    {
#ifdef GPIO_SYSFS_SUPPORT_DEPRECATED
        if (state->gpios[i].type == PIN_GPIO)
        {
            result = initialize_gpio(&state->gpios[i]);
            if (result != ST_OK)
            {
                state->gpios[i].type = PIN_NONE;
                continue;
            }
            // do a read to clear any bogus events on startup
            int dummy;
            result = gpio_get_value(state->gpios[i].fd, &dummy);
            if (result != ST_OK)
                continue;
            status = ST_OK;
        }
        else
#endif
            if (state->gpios[i].type == PIN_GPIOD)
        {
            result = initialize_gpiod(&state->gpios[i]);
            if (result != ST_OK)
            {
                state->gpios[i].type = PIN_NONE;
                continue;
            }
            status = ST_OK;
        }
    }

    // If at least one PIN has been successfully configured
    if (status == ST_OK)
        ASD_log(ASD_LogLevel_Info, stream, option,
                "GPIOs initialized successfully");
    else
        ASD_log(ASD_LogLevel_Error, stream, option,
                "GPIOs initialization failed");
    return status;
}

#ifdef GPIO_SYSFS_SUPPORT_DEPRECATED
STATUS initialize_gpio(Target_Control_GPIO* gpio)
{
    int num;
    STATUS result = find_gpio(gpio->name, &num);

    if (result != ST_OK)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "Failed to find gpio for %s", gpio->name);
    }
    else
    {
        result = gpio_export(num, &gpio->fd);
        if (result != ST_OK)
            ASD_log(ASD_LogLevel_Error, stream, option,
                    "Gpio export failed for %s", gpio->name);
#ifdef ENABLE_DEBUG_LOGGING
        else
            ASD_log(ASD_LogLevel_Debug, stream, option,
                    "Gpio export succeeded for %s num %d fd %d", gpio->name,
                    num, gpio->fd);
#endif
    }

    if (result == ST_OK)
    {
        result = gpio_set_active_low(num, gpio->active_low);
        if (result != ST_OK)
            ASD_log(ASD_LogLevel_Error, stream, option,
                    "Gpio set active low failed for %s", gpio->name);
    }

    if (result == ST_OK)
    {
        result = gpio_set_direction(num, gpio->direction);
        if (result != ST_OK)
            ASD_log(ASD_LogLevel_Error, stream, option,
                    "Gpio set direction failed for %s", gpio->name);
    }

    if (result == ST_OK)
    {
        result = gpio_set_edge(num, gpio->edge);
        if (result != ST_OK)
            ASD_log(ASD_LogLevel_Error, stream, option,
                    "Gpio set edge failed for %s", gpio->name);
    }

    if (result == ST_OK)
    {
        gpio->number = num;
        ASD_log(ASD_LogLevel_Info, stream, option, "gpio %s initialized to %d",
                gpio->name, gpio->number);
    }

    return result;
}
#endif

STATUS initialize_gpiod(Target_Control_GPIO* gpio)
{
    int offset = -1;
    uint8_t chip_name[CHIP_BUFFER_SIZE];
    int rv;
    int default_val = 0;
    struct gpiod_line_request_config config;

    if (gpio == NULL)
    {
        return ST_ERR;
    }

    explicit_bzero(chip_name, CHIP_BUFFER_SIZE);
    if(memcpy_s(chip_name, CHIP_BUFFER_SIZE, GPIOD_DEV_ROOT_FOLDER,
             GPIOD_DEV_ROOT_FOLDER_STRLEN))
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
            "memcpy_s: chip_name gpiod copy failure");
        return ST_ERR;
    }

    rv = gpiod_ctxless_find_line(
        gpio->name, &chip_name[GPIOD_DEV_ROOT_FOLDER_STRLEN],
        CHIP_BUFFER_SIZE - GPIOD_DEV_ROOT_FOLDER_STRLEN, &offset);
    if (rv < 0)
    {
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Error, stream, option,
                "error performing the line lookup");
#endif
        return ST_ERR;
    }
    else if (rv == 0)
    {
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Error, stream, option, "line %s doesn't exist",
                gpio->name);
#endif
        return ST_ERR;
    }

#ifdef ENABLE_DEBUG_LOGGING
    ASD_log(ASD_LogLevel_Info, stream, option,
            "gpio: %s gpio device: %s line offset: %d", gpio->name, chip_name,
            offset);
#endif

    gpio->chip = gpiod_chip_open(chip_name);
    if (!gpio->chip)
    {
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Error, stream, option, "Failed to open the chip");
#endif
        return ST_ERR;
    }

    gpio->line = gpiod_chip_get_line(gpio->chip, offset);
    if (!gpio->line)
    {
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Error, stream, option,
                "Failed to get line reference");
#endif
        gpiod_chip_close(gpio->chip);
        return ST_ERR;
    }

    config.consumer = GPIOD_CONSUMER_LABEL;

    switch (gpio->direction)
    {
        case GPIO_DIRECTION_IN:
            switch (gpio->edge)
            {
                case GPIO_EDGE_RISING:
                    config.request_type = GPIOD_LINE_REQUEST_EVENT_RISING_EDGE;
                    break;
                case GPIO_EDGE_FALLING:
                    config.request_type = GPIOD_LINE_REQUEST_EVENT_FALLING_EDGE;
                    break;
                case GPIO_EDGE_BOTH:
                    config.request_type = GPIOD_LINE_REQUEST_EVENT_BOTH_EDGES;
                    break;
                case GPIO_EDGE_NONE:
                    config.request_type = GPIOD_LINE_REQUEST_DIRECTION_INPUT;
                default:
                    break;
            }
            break;
        case GPIO_DIRECTION_HIGH:
            config.request_type = GPIOD_LINE_REQUEST_DIRECTION_OUTPUT;
            default_val = gpio->active_low ? 0 : 1;
            break;
        case GPIO_DIRECTION_OUT:
        case GPIO_DIRECTION_LOW:
            config.request_type = GPIOD_LINE_REQUEST_DIRECTION_OUTPUT;
            default_val = gpio->active_low ? 1 : 0;
            break;
        default:
            break;
    }

    config.flags = gpio->active_low ? GPIOD_LINE_REQUEST_FLAG_ACTIVE_LOW : 0;
#ifdef ENABLE_DEBUG_LOGGING
    ASD_log(ASD_LogLevel_Info, stream, option,
            "default_val = %d request_type = 0x%x flags = 0x%x consumer = %s",
            default_val, config.request_type, config.flags, config.consumer);
#endif

    // Default value have a different behavior in SysFs and gpiod. For
    // SysFs, the setting HIGH or LOW means the pin level while in gpiod
    // the value describes if signal will be active or not. The pin level
    // in gpiod will vary according with the active_low settings.

    rv = gpiod_line_request(gpio->line, &config, default_val);
    if (rv)
    {
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Error, stream, option,
                "Failed to process line request");
#endif
        gpiod_chip_close(gpio->chip);
        return ST_ERR;
    }

    // File Descriptor
    switch (gpio->direction)
    {
        case GPIO_DIRECTION_IN:
            switch (gpio->edge)
            {
                case GPIO_EDGE_RISING:
                case GPIO_EDGE_FALLING:
                case GPIO_EDGE_BOTH:
                    // Get GPIOD line file descriptor
                    gpio->fd = gpiod_line_event_get_fd(gpio->line);
                    if (gpio->fd == -1)
                    {
#ifdef ENABLE_DEBUG_LOGGING
                        ASD_log(ASD_LogLevel_Error, stream, option,
                                "Failed to get file descriptor");
#endif
                        return ST_ERR;
                    }
#ifdef ENABLE_DEBUG_LOGGING
                    ASD_log(ASD_LogLevel_Info, stream, option, "%s.fd = 0x%x",
                            gpio->name, gpio->fd);
#endif
                    break;
                case GPIO_EDGE_NONE:
#ifdef ENABLE_DEBUG_LOGGING
                    ASD_log(ASD_LogLevel_Info, stream, option, "No event");
#endif
                default:
                    break;
            }
            break;
        case GPIO_DIRECTION_HIGH:
        case GPIO_DIRECTION_OUT:
        case GPIO_DIRECTION_LOW:
        default:
            break;
    }

    gpio->number = offset;
#ifdef ENABLE_DEBUG_LOGGING
    ASD_log(ASD_LogLevel_Info, stream, option, "gpio %s initialized to %d",
            gpio->name, gpio->number);
#endif

    return ST_OK;
}

#ifdef GPIO_SYSFS_SUPPORT_DEPRECATED
STATUS find_gpio_base(char* gpio_name, int* gpio_base)
{
    int fd;
    char buf[CHIP_FNAME_BUFF_SIZE];
    char ch;

    *gpio_base = 0;
    if (strcpy_s(buf, CHIP_FNAME_BUFF_SIZE, AST2500_GPIO_BASE_FILE))
    {
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Debug, stream, option,
                "strcpy_s: gpio base filename failed");
#endif
        return ST_ERR;
    }
#ifdef ENABLE_DEBUG_LOGGING
    ASD_log(ASD_LogLevel_Debug, stream, option, "open gpio base file %s", buf);
#endif
    fd = open(buf, O_RDONLY);
    if (fd < 0)
    {
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Debug, stream, option,
                "open gpio base file %s failed", buf);
#endif
        return ST_ERR;
    }
    lseek(fd, 0, SEEK_SET);
    // read all characters in the file
    while (read(fd, &ch, 1))
    {
        if ((ch >= '0') && (ch <= '9'))
            *gpio_base = (*gpio_base * 10) + (ch - '0');
    }
#ifdef ENABLE_DEBUG_LOGGING
    ASD_log(ASD_LogLevel_Debug, stream, option, "base is %d", *gpio_base);
#endif
    close(fd);
    return ST_OK;
}

STATUS find_gpio(char* gpio_name, int* gpio_number)
{
    STATUS result = ST_OK;
    int gpio_base = 0;
    int cmp = 0;

    result = find_gpio_base(gpio_name, &gpio_base);
    if (result != ST_OK)
        return result;

    strcmp_s(gpio_name, PIN_NAME_MAX_SIZE, "TCK_MUX_SEL", &cmp);
    if (cmp == 0)
    {
        *gpio_number = gpio_base + 213;
        return result;
    }
    strcmp_s(gpio_name, PIN_NAME_MAX_SIZE, "PREQ_N", &cmp);
    if (cmp == 0)
    {
        *gpio_number = gpio_base + 212;
        return result;
    }
    strcmp_s(gpio_name, PIN_NAME_MAX_SIZE, "PRDY_N", &cmp);
    if (cmp == 0)
    {
        *gpio_number = gpio_base + 47;
        return result;
    }
    strcmp_s(gpio_name, PIN_NAME_MAX_SIZE, "RSMRST_N", &cmp);
    if (cmp == 0)
    {
        *gpio_number = gpio_base + 146;
        return result;
    }
    strcmp_s(gpio_name, PIN_NAME_MAX_SIZE, "SIO_POWER_GOOD", &cmp);
    if (cmp == 0)
    {
        *gpio_number = gpio_base + 201;
        return result;
    }
    strcmp_s(gpio_name, PIN_NAME_MAX_SIZE, "PLTRST_N", &cmp);
    if (cmp == 0)
    {
        *gpio_number = gpio_base + 46;
        return result;
    }
    strcmp_s(gpio_name, PIN_NAME_MAX_SIZE, "SYSPWROK", &cmp);
    if (cmp == 0)
    {
        *gpio_number = gpio_base + 145;
        return result;
    }
    strcmp_s(gpio_name, PIN_NAME_MAX_SIZE, "PWR_DEBUG_N", &cmp);
    if (cmp == 0)
    {
        *gpio_number = gpio_base + 135;
        return result;
    }
    strcmp_s(gpio_name, PIN_NAME_MAX_SIZE, "DEBUG_EN_N", &cmp);
    if (cmp == 0)
    {
        *gpio_number = gpio_base + 37;
        return result;
    }
    strcmp_s(gpio_name, PIN_NAME_MAX_SIZE, "XDP_PRST_N", &cmp);
    if (cmp == 0)
    {
        *gpio_number = gpio_base + 137;
        return result;
    }
    else
        result = ST_ERR;

    return result;
}
#endif

STATUS target_deinitialize(Target_Control_Handle* state)
{
    if (state == NULL || !state->initialized)
        return ST_ERR;

#ifdef GPIO_SYSFS_SUPPORT_DEPRECATED
    for (int i = 0; i < NUM_GPIOS; i++)
    {
        if (state->gpios[i].type == PIN_GPIO)
        {
            if (state->gpios[i].fd != -1)
            {
                close(state->gpios[i].fd);
                state->gpios[i].fd = -1;
            }
        }
    }
#endif

    if (state->dbus != NULL)
    {
        dbus_deinitialize(state->dbus);
        free(state->dbus);
        state->dbus = NULL;
    }

    return deinitialize_gpios(state);
}

STATUS deinitialize_gpios(Target_Control_Handle* state)
{
    STATUS result = ST_OK;
    STATUS retcode = ST_OK;
    int i;

    // Configure all ASD PINs as input to deinitialize GPIOs
    for (i = 0; i < NUM_GPIOS; i++)
    {
#ifdef GPIO_SYSFS_SUPPORT_DEPRECATED
        if (state->gpios[i].type == PIN_GPIO)
        {
            retcode =
                gpio_set_direction(state->gpios[i].number, GPIO_DIRECTION_IN);
            if (retcode != ST_OK)
            {
                ASD_log(ASD_LogLevel_Error, stream, option,
                        "Gpio set direction failed for %s",
                        state->gpios[i].name);
                result = ST_ERR;
            }
            retcode = gpio_unexport(state->gpios[i].number);
            if (retcode != ST_OK)
            {
                ASD_log(ASD_LogLevel_Error, stream, option,
                        "Gpio export failed for %s", state->gpios[i].name);
                result = ST_ERR;
            }
        }
        else
#endif
            if (state->gpios[i].type == PIN_GPIOD)
        {
            int rv;
            gpiod_line_release(state->gpios[i].line);
            rv = gpiod_line_request_input(state->gpios[i].line,
                                          GPIOD_CONSUMER_LABEL);
            if (rv)
            {
                ASD_log(ASD_LogLevel_Error, stream, option,
                        "Failed to process line request input for %s",
                        state->gpios[i].name);
                result = ST_ERR;
            }
            gpiod_chip_close(state->gpios[i].chip);
        }
    }

    ASD_log(ASD_LogLevel_Info, stream, option,
            (result == ST_OK) ? "GPIOs deinitialized successfully"
                              : "GPIOs deinitialized failed");
    return result;
}

STATUS target_event(Target_Control_Handle* state, struct pollfd poll_fd,
                    ASD_EVENT* event, ASD_EVENT_DATA * event_data)
{
    STATUS result = ST_ERR;
    int i, rv = 0;
    size_t ret;

    if (state == NULL || !state->initialized || event == NULL)
        return ST_ERR;

    *event = ASD_EVENT_NONE;

    if (state->spp_handler != NULL)
    {
        uint8_t count = 0;
        int i = 0;

        if (event_data == NULL)
            return ST_ERR;

        result = spp_bus_device_count(state->spp_handler, &count);
        if (result == ST_OK)
        {
            for(i = 0; i < count; i++)
            {
                if (state->spp_handler->spp_dev_handlers[i] == poll_fd.fd &&
                    (poll_fd.revents & POLLIN) == POLLIN)
                {
                    if (i3c_ibi_handler(poll_fd.fd, event_data->buffer,
                        &event_data->size) == ST_OK)
                    {
                        *event = ASD_EVENT_BPK;
                        event_data->addr = i;
                        state->spp_handler->ibi_handled = true;
                        return ST_OK;
                    }
                    else
                    {
                        ASD_log(ASD_LogLevel_Error, stream, option,
                                "target_event() ASD_EVENT_BPK already processed");
                        return ST_ERR;
                    }
                }
            }
        }
    }

    if (state->dbus && state->dbus->fd == poll_fd.fd &&
        (poll_fd.revents & POLLIN) == POLLIN)
    {
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Debug, stream, option,
                "Handling dbus event for fd: %d", poll_fd.fd);
#endif
        result = dbus_process_event(state->dbus, event);
    }
    else if ((poll_fd.revents & (POLLIN | POLLPRI))
#ifdef GPIO_SYSFS_SUPPORT_DEPRECATED
             || ((poll_fd.revents & POLL_GPIO) == POLL_GPIO)
#endif
            )
    {
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Debug, stream, option, "Handling event for fd: %d",
                poll_fd.fd);
#endif
        for (i = 0; i < NUM_GPIOS; i++)
        {
            if (state->gpios[i].fd == poll_fd.fd)
            {
                result = target_clear_gpio_event(state, state->gpios[i]);
                if (result != ST_OK)
                    break;
                result = state->gpios[i].handler(state, event);
                break;
            }
        }
    }
    else
    {
        result = ST_OK;
    }

    return result;
}

STATUS on_power_event(Target_Control_Handle* state, ASD_EVENT* event)
{
    STATUS result;
    int value;

    Target_Control_GPIO gpio = state->gpios[BMC_CPU_PWRGD];
    result = gpio.read(state, BMC_CPU_PWRGD, &value);
    if (result != ST_OK)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "Failed to get gpio data for CPU_PWRGD: %d", result);
    }
    else if (value == 1)
    {
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Debug, stream, option, "Power restored");
#endif
        *event = ASD_EVENT_PWRRESTORE;
    }
    else
    {
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Debug, stream, option, "Power fail");
#endif
        *event = ASD_EVENT_PWRFAIL;
    }
    return result;
}

STATUS on_platform_reset_event(Target_Control_Handle* state, ASD_EVENT* event)
{
    STATUS result;
    int value;
    Target_Control_GPIO pltrst_gpio = state->gpios[BMC_PLTRST_B];
    Target_Control_GPIO preq_gpio = state->gpios[BMC_PREQ_N];

    result = pltrst_gpio.read(state, BMC_PLTRST_B, &value);
    if (result != ST_OK)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "Failed to get event status for PLTRST: %d", result);
    }
    else if (value == 0)
    {
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Debug, stream, option,
                "Platform reset de-asserted");
#endif
        *event = ASD_EVENT_PLRSTDEASSRT;
        if (state->event_cfg.reset_break)
        {
#ifdef ENABLE_DEBUG_LOGGING
            ASD_log(ASD_LogLevel_Debug, stream, option,
                    "ResetBreak detected PLT_RESET "
                    "assert, asserting PREQ");
#endif
            result = preq_gpio.write(state, BMC_PREQ_N, 1);
            if (result != ST_OK)
            {
                ASD_log(ASD_LogLevel_Error, stream, option,
                        "Failed to assert PREQ");
            }
        }
    }
    else
    {
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Debug, stream, option, "Platform reset asserted");
#endif
        *event = ASD_EVENT_PLRSTASSERT;
    }

    return result;
}

STATUS on_prdy_event(Target_Control_Handle* state, ASD_EVENT* event)
{
    STATUS result = ST_OK;

#ifdef ENABLE_DEBUG_LOGGING
    ASD_log(ASD_LogLevel_Debug, stream, option,
            "CPU_PRDY Asserted Event Detected.");
#endif
    *event = ASD_EVENT_PRDY_EVENT;
    if (state->event_cfg.break_all)
    {
        Target_Control_GPIO gpio = state->gpios[BMC_PREQ_N];
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Debug, stream, option,
                "BreakAll detected PRDY, asserting PREQ");
#endif
        result = gpio.write(state, BMC_PREQ_N, 1);
        if (result != ST_OK)
        {
            ASD_log(ASD_LogLevel_Error, stream, option,
                    "Failed to assert PREQ");
        }
        else if (!state->event_cfg.reset_break)
        {
            usleep(10000);
#ifdef ENABLE_DEBUG_LOGGING
            ASD_log(ASD_LogLevel_Debug, stream, option,
                    "CPU_PRDY, de-asserting PREQ");
#endif
            result = gpio.write(state, BMC_PREQ_N, 0);
            if (result != ST_OK)
            {
                ASD_log(ASD_LogLevel_Error, stream, option,
                        "Failed to deassert PREQ");
            }
        }
    }

    return result;
}

STATUS on_xdp_present_event(Target_Control_Handle* state, ASD_EVENT* event)
{
    STATUS result = ST_OK;
    (void)state; /* unused */

#ifdef ENABLE_DEBUG_LOGGING
    ASD_log(ASD_LogLevel_Debug, stream, option,
            "XDP Present state change detected");
#endif
    *event = ASD_EVENT_XDP_PRESENT;

    return result;
}

STATUS on_power2_event(Target_Control_Handle* state, ASD_EVENT* event)
{
    STATUS result;
    int value;

    Target_Control_GPIO gpio = state->gpios[BMC_PWRGD2];
    result = gpio.read(state, BMC_PWRGD2, &value);
    if (result != ST_OK)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "Failed to get gpio data for BMC_PWRGD2: %d", result);
    }
    else if (value == 1)
    {
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Debug, stream, option, "Power2 restored");
#endif
        *event = ASD_EVENT_PWRRESTORE2;
    }
    else
    {
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Debug, stream, option, "Power2 Power fail");
#endif
        *event = ASD_EVENT_PWRFAIL2;
    }
    return result;
}

STATUS on_power3_event(Target_Control_Handle* state, ASD_EVENT* event)
{
    STATUS result;
    int value;

    Target_Control_GPIO gpio = state->gpios[BMC_PWRGD3];
    result = gpio.read(state, BMC_PWRGD3, &value);
    if (result != ST_OK)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "Failed to get gpio data for BMC_PWRGD3: %d", result);
    }
    else if (value == 1)
    {
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Debug, stream, option, "Power3 restored");
#endif
        *event = ASD_EVENT_PWRRESTORE3;
    }
    else
    {
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Debug, stream, option, "Power3 fail");
#endif
        *event = ASD_EVENT_PWRFAIL3;
    }
    return result;
}

STATUS target_write(Target_Control_Handle* state, const Pin pin,
                    const bool assert)
{
    STATUS result = ST_OK;
    Target_Control_GPIO gpio;
    int value;

    if (state == NULL || !state->initialized)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "target_write, null or uninitialized state");
        return ST_ERR;
    }
    switch (pin)
    {
        case PIN_RESET_BUTTON:
        case PIN_POWER_BUTTON:
        case PIN_PREQ:
        case PIN_TCK_MUX_SELECT:
        case PIN_SYS_PWR_OK:
        case PIN_EARLY_BOOT_STALL:
            gpio = state->gpios[ASD_PIN_TO_GPIO[pin]];
            ASD_log(ASD_LogLevel_Info, stream, option, "Pin Write: %s %s %d",
                    assert ? "assert" : "deassert", gpio.name, gpio.number);
            result = gpio.write(state, ASD_PIN_TO_GPIO[pin], (uint8_t)(assert ? 1 : 0));
            if (result != ST_OK)
            {
                ASD_log(ASD_LogLevel_Error, stream, option,
                        "Failed to set %s %s %d",
                        assert ? "assert" : "deassert", gpio.name, gpio.number);
            }
            break;
        default:
#ifdef ENABLE_DEBUG_LOGGING
            ASD_log(ASD_LogLevel_Debug, stream, option,
                    "Pin write: unsupported pin '%s'", gpio.name);
#endif
            result = ST_ERR;
            break;
    }

    return result;
}

STATUS target_read(Target_Control_Handle* state, Pin pin, bool* asserted)
{
    STATUS result;
    Target_Control_GPIO gpio;
    int value;
    if (state == NULL || asserted == NULL || !state->initialized)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "target_read, null or uninitialized state");
        return ST_ERR;
    }
    *asserted = false;

    switch (pin)
    {
        case PIN_PWRGOOD:
        case PIN_PRDY:
        case PIN_PREQ:
        case PIN_SYS_PWR_OK:
        case PIN_EARLY_BOOT_STALL:
            gpio = state->gpios[ASD_PIN_TO_GPIO[pin]];
            result = gpio.read(state, ASD_PIN_TO_GPIO[pin], &value);
            if (result != ST_OK)
            {
                ASD_log(ASD_LogLevel_Error, stream, option,
                        "Failed to read gpio %s %d", gpio.name, gpio.number);
            }
            else
            {
                *asserted = (value != 0);
                ASD_log(ASD_LogLevel_Info, stream, option, "Pin read: %s %s %d",
                        *asserted ? "asserted" : "deasserted", gpio.name,
                        gpio.number);
            }
            break;
        default:
#ifdef ENABLE_DEBUG_LOGGING
            ASD_log(ASD_LogLevel_Debug, stream, option,
                    "Pin read: unsupported gpio read for pin: %d", pin);
#endif
            result = ST_ERR;
    }

    return result;
}

STATUS target_write_event_config(Target_Control_Handle* state,
                                 const WriteConfig event_cfg, const bool enable)
{
    STATUS status = ST_OK;
    if (state == NULL || !state->initialized)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "target_write_event_config, null or uninitialized state");
        return ST_ERR;
    }

    switch (event_cfg)
    {
        case WRITE_CONFIG_BREAK_ALL:
        {
            if (state->event_cfg.break_all != enable)
            {
#ifdef ENABLE_DEBUG_LOGGING
                ASD_log(ASD_LogLevel_Debug, stream, option, "BREAK_ALL %s",
                        enable ? "enabled" : "disabled");
#endif
                state->event_cfg.break_all = enable;
            }
            break;
        }
        case WRITE_CONFIG_RESET_BREAK:
        {
            if (state->event_cfg.reset_break != enable)
            {
#ifdef ENABLE_DEBUG_LOGGING
                ASD_log(ASD_LogLevel_Debug, stream, option, "RESET_BREAK %s",
                        enable ? "enabled" : "disabled");
#endif
                state->event_cfg.reset_break = enable;
            }
            break;
        }
        case WRITE_CONFIG_REPORT_PRDY:
        {
#ifdef ENABLE_DEBUG_LOGGING
            if (state->event_cfg.report_PRDY != enable)
            {
                ASD_log(ASD_LogLevel_Debug, stream, option, "REPORT_PRDY %s",
                        enable ? "enabled" : "disabled");
            }
#endif
            // do a read to ensure no outstanding prdys are present before
            // wait for prdy is enabled.
            int dummy = 0;
            STATUS retval = ST_ERR;
            Target_Control_GPIO gpio = state->gpios[BMC_PRDY_N];
            retval = gpio.read(state, BMC_PRDY_N, &dummy);
            state->event_cfg.report_PRDY = enable;
            break;
        }
        case WRITE_CONFIG_REPORT_PLTRST:
        {
            if (state->event_cfg.report_PLTRST != enable)
            {
#ifdef ENABLE_DEBUG_LOGGING
                ASD_log(ASD_LogLevel_Debug, stream, option, "REPORT_PLTRST %s",
                        enable ? "enabled" : "disabled");
#endif
                state->event_cfg.report_PLTRST = enable;
            }
            break;
        }
        case WRITE_CONFIG_REPORT_MBP:
        {
            if (state->event_cfg.report_MBP != enable)
            {
#ifdef ENABLE_DEBUG_LOGGING
                ASD_log(ASD_LogLevel_Debug, stream, option, "REPORT_MBP %s",
                        enable ? "enabled" : "disabled");
#endif
                state->event_cfg.report_MBP = enable;
            }
            break;
        }
        default:
        {
            ASD_log(ASD_LogLevel_Error, stream, option,
                    "Invalid event config %d", event_cfg);
            status = ST_ERR;
        }
    }
    return status;
}

STATUS target_clear_gpio_event(Target_Control_Handle* state,
                               Target_Control_GPIO pin)
{
    STATUS status = ST_OK;
    int rv = 0;

#ifdef GPIO_SYSFS_SUPPORT_DEPRECATED
    if (pin.type == PIN_GPIO)
    {
        // do a read to clear the event
        int dummy;
        gpio_get_value(pin.fd, &dummy);
    }
    else
#endif
    if (pin.type == PIN_GPIOD)
    {
        // clear the gpiod event
        struct gpiod_line_event levent;
        rv = gpiod_line_event_read(pin.line, &levent);
        if (rv != 0)
            status = ST_ERR;
    }

    return status;
}

STATUS target_wait_PRDY(Target_Control_Handle* state, const uint8_t log2time)
{
    // The design for this calls for waiting for PRDY or until a timeout
    // occurs. The timeout is computed using the PRDY timeout setting
    // (log2time) and the JTAG TCLK.
    // bool platform reset added to check if there has been a call reset during
    // wait PRDY

    int timeout_ms = 0;
    static bool platform_reset = false;
    struct pollfd pfd = {0};
    int poll_result = 0;
    STATUS result = ST_OK;
    STATUS platform_result = ST_OK;
    short events = 0;
    short value = 0;
    if (state == NULL || !state->initialized)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "target_wait_PRDY, null or uninitialized state");
        return ST_ERR;
    }

    // The timeout for commands that wait for a PRDY pulse is defined to be in
    // uSec, we need to convert to mSec, so we divide by 1000. For
    // values less than 1 ms that get rounded to 0 we need to wait 1ms.
    timeout_ms = (1 << log2time) / JTAG_CLOCK_CYCLE_MILLISECONDS;
    if (timeout_ms <= 0)
    {
        timeout_ms = 1;
    }
    get_pin_events(state->gpios[BMC_PRDY_N], &events);
    pfd.events = events;
    pfd.fd = state->gpios[BMC_PRDY_N].fd;

    if (platform_reset)
    {
        timeout_ms = 0;
    }
    poll_result = poll(&pfd, 1, timeout_ms);

    Target_Control_GPIO pltrst_gpio = state->gpios[BMC_PLTRST_B];

    platform_result = pltrst_gpio.read(state, BMC_PLTRST_B, &value);

    if (platform_result != ST_OK)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "Failed to get event status for PLTRST: %d", platform_result);
    }
    else if (value == 0)
    {
        platform_reset = false;
    }
    else
    {
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Debug, stream, option,
                "Platform reset pin in: %d, Next Timeout set to 0", value);
#endif
        platform_reset = true;
    }

    if (poll_result == 0)
    {
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Debug, stream, option,
                "Wait PRDY timed out occurred");
#endif
        timeout_ms = (1 << log2time) / JTAG_CLOCK_CYCLE_MILLISECONDS;
        if (timeout_ms <= 0)
        {
            timeout_ms = 1;
        }
        // future: we should return something to indicate a timeout
    }
    else if (poll_result > 0)
    {
        if (pfd.revents & events)
        {
#ifdef ENABLE_DEBUG_LOGGING
            ASD_log(ASD_LogLevel_Trace, stream, option,
                    "Wait PRDY complete, detected PRDY");
#endif
            result = target_clear_gpio_event(state, state->gpios[BMC_PRDY_N]);
        }
    }
    else
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "target_wait_PRDY poll failed: %d.", poll_result);
        result = ST_ERR;
    }
    return result;
}

STATUS target_get_fds(Target_Control_Handle* state, target_fdarr_t* fds,
                      int* num_fds)
{
    int index = 0;
    short events = 0;
    STATUS result = ST_ERR;

    if (state == NULL || !state->initialized || fds == NULL || num_fds == NULL)
    {
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Error, stream, option,
                "target_get_fds, null or uninitialized state");
#endif
        return ST_ERR;
    }

    get_pin_events(state->gpios[BMC_PRDY_N], &events);
    if (state->event_cfg.report_PRDY && state->gpios[BMC_PRDY_N].fd != -1)
    {
        (*fds)[index].fd = state->gpios[BMC_PRDY_N].fd;
        (*fds)[index].events = events;
        index++;
    }

    get_pin_events(state->gpios[BMC_PLTRST_B], &events);
    if (state->gpios[BMC_PLTRST_B].fd != -1)
    {
        (*fds)[index].fd = state->gpios[BMC_PLTRST_B].fd;
        (*fds)[index].events = events;
        index++;
    }

    get_pin_events(state->gpios[BMC_CPU_PWRGD], &events);
    if (state->gpios[BMC_CPU_PWRGD].type == PIN_GPIOD
#ifdef GPIO_SYSFS_SUPPORT_DEPRECATED
        || state->gpios[BMC_CPU_PWRGD].type == PIN_GPIO
#endif
    )
    {
        if (state->gpios[BMC_CPU_PWRGD].fd != -1)
        {
            (*fds)[index].fd = state->gpios[BMC_CPU_PWRGD].fd;
            (*fds)[index].events = events;
            index++;
        }
    }

    get_pin_events(state->gpios[BMC_XDP_PRST_IN], &events);
    if (state->gpios[BMC_XDP_PRST_IN].fd != -1)
    {
        (*fds)[index].fd = state->gpios[BMC_XDP_PRST_IN].fd;
        (*fds)[index].events = events;
        index++;
    }

    get_pin_events(state->gpios[BMC_PWRGD2], &events);
    if (state->gpios[BMC_PWRGD2].fd != -1)
    {
        (*fds)[index].fd = state->gpios[BMC_PWRGD2].fd;
        (*fds)[index].events = events;
        index++;
    }

    get_pin_events(state->gpios[BMC_PWRGD3], &events);
    if (state->gpios[BMC_PWRGD3].fd != -1)
    {
        (*fds)[index].fd = state->gpios[BMC_PWRGD3].fd;
        (*fds)[index].events = events;
        index++;
    }

    if (state->dbus && state->dbus->fd != -1)
    {
        (*fds)[index].fd = state->dbus->fd;
        (*fds)[index].events = POLLIN;
        index++;
    }

    if (state->spp_handler != NULL)
    {
        uint8_t count = 0;
        int i = 0;
        result = spp_bus_device_count(state->spp_handler, &count);
        if (result == ST_OK)
        {
            for(i = 0; i<count; i++)
            {
                (*fds)[index].fd = state->spp_handler->spp_dev_handlers[i];
                (*fds)[index].events = POLLIN;
                index++;
            }
        }
    }

    *num_fds = index;

    return ST_OK;
}

// target_wait_sync - This command will only be issued in a multiple
//   probe configuration where there are two or more TAP controllers. The
//   WaitSync command is used to tell all TAP controllers to wait until a
//   sync indication is received. The exact flow of sync signaling is
//   implementation specific. Command processing will continue after
//   either the Sync indication is received or the SyncTimeout is
//   reached. The SyncDelay is intended to be used in an implementation
//   where there is a single sync signal routed from a single designated
//   TAP Controller to all other TAP Controllers. The SyncDelay is used as an
//   implicit means to ensure that all other TAP Controllers have reached
//   the WaitSync before the Sync signal is asserted.
//
// Parameters:
//  timeout - the SyncTimeout provides a timeout value to all target
//    probes. If a target probe does not receive the sync signal during
//    this timeout period, then a timeout occurs. The value is in
//    milliseconds (Range 0ms - 65s).
//  delay - the SyncDelay is meant to be used in a single controller probe
//    sync singal implmentation. Upon receiving the WaitSync command,
//    the probe will delay for the Sync Delay Value before sending
//    the sync signal. This is to ensure that all target probes
//    have reached WaitSync state prior to the sync being sent.
//    The value is in milliseconds (Range 0ms - 65s).
//
// Returns:
//  ST_OK if operation completed successfully.
//  ST_ERR if operation failed.
//  ST_TIMEOUT if failed to detect sync signal
STATUS target_wait_sync(Target_Control_Handle* state, const uint16_t timeout,
                        const uint16_t delay)
{
    STATUS result = ST_OK;
    if (state == NULL || !state->initialized)
    {
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Trace, stream, option,
                "target_wait_sync, null or uninitialized state");
#endif
        return ST_ERR;
    }

#ifdef ENABLE_DEBUG_LOGGING
    ASD_log(ASD_LogLevel_Debug, stream, option,
            "WaitSync(%s) - delay=%u ms - timeout=%u ms",
            state->is_controller_probe ? "controller" : "target", delay, timeout);
#endif

    if (state->is_controller_probe)
    {
        usleep((__useconds_t)(delay * 1000)); // convert from us to ms
        // Once delay has occurred, send out the sync signal.

        // <MODIFY>
        // hard code a error until code is implemented
        result = ST_ERR;
        // </MODIFY>
    }
    else
    {
        // Wait for sync signal to arrive.
        // timeout if sync signal is not received in the
        // milliseconds provided by the timeout parameter

        // <MODIFY>
        usleep((__useconds_t)(timeout * 1000)); // convert from us to ms
        // hard code a error/timeout until code is implemented
        result = ST_TIMEOUT;
        // when sync is detected, set result to ST_OK
        // </MODIFY>
    }

    return result;
}

STATUS target_get_i2c_i3c_config(bus_options* busopt)
{
    STATUS result = ST_ERR;

    if (busopt == NULL)
        return ST_ERR;

    Dbus_Handle* dbus = dbus_helper();
    if (dbus)
    {
        // Connect to the system bus
        int retcode = sd_bus_open_system(&dbus->bus);
        if (retcode >= 0)
        {
            dbus->fd = sd_bus_get_fd(dbus->bus);
            if (dbus->fd < 0)
            {
                ASD_log(ASD_LogLevel_Error, stream, option,
                        "sd_bus_get_fd failed");
            }
            else
            {
                result = dbus_get_platform_bus_config(dbus, busopt);
                if (result != ST_OK)
                {
                    ASD_log(ASD_LogLevel_Error, stream, option,
                            "dbus_get_platform_bus_config failed");
                }
            }
            dbus_deinitialize(dbus);
        }
        free(dbus);
    }
    else
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "failed to get dbus handle");
    }

#ifdef PLATFORM_IxC_LOCAL_CONFIG
    ASD_log(ASD_LogLevel_Info, stream, option,
            "Using local(override) i2c/i3c bus configuration");

    busopt->enable_i2c = false;
    busopt->enable_i3c = false;
    busopt->enable_spp = false;


    for (int i = 0; i < MAX_IxC_BUSES + MAX_SPP_BUSES; i++)
    {
        busopt->bus_config_type[i] = BUS_CONFIG_NOT_ALLOWED;
        busopt->bus_config_map[i] = 0;
    }
#endif

#ifdef ENABLE_DEBUG_LOGGING
    for (int i = 0; i < MAX_IxC_BUSES + MAX_SPP_BUSES; i++)
    {
        ASD_log(ASD_LogLevel_Debug, stream, option, "Bus %d: %s",
                busopt->bus_config_map[i],
                BUS_CONFIG_TYPE_STRINGS[busopt->bus_config_type[i]]);
    }
#endif
    return result;
}
