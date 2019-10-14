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

#include "dbus_helper.h"

#include <stdio.h>
#include <stdlib.h>

#include "logging.h"

static const ASD_LogStream stream = ASD_LogStream_Pins;
static const ASD_LogOption option = ASD_LogOption_None;
STATUS dbus_power_on(Dbus_Handle*);
STATUS dbus_power_off(Dbus_Handle*);
STATUS dbus_call_set_property_async(const Dbus_Handle* state,
                                    const char* service, const char* object,
                                    const char* interface_name,
                                    const char* method, const char* argument);
bool callb;

Dbus_Handle* dbus_helper()
{
    Dbus_Handle* state = (Dbus_Handle*)malloc(sizeof(Dbus_Handle));
    if (state)
    {
        state->bus = NULL;
        state->fd = -1;
        state->power_state = STATE_UNKNOWN;
    }
    return state;
}

STATUS dbus_initialize(Dbus_Handle* state)
{
    STATUS result = ST_ERR;
    const char* match = MATCH_STRING_CHASSIS;
    if (state)
    {
        /* Connect to the system bus */
        int retcode = sd_bus_open_system(&state->bus);
        if (retcode >= 0)
        {
            state->fd = sd_bus_get_fd(state->bus);
            if (state->fd < 0)
            {
                ASD_log(ASD_LogLevel_Error, stream, option,
                        "sd_bus_get_fd failed: %d", retcode);
                result = ST_ERR;
            }
            else
            {
                retcode = sd_bus_add_match(state->bus, NULL, match,
                                           match_callback, state);
                if (retcode < 0)
                {
                    ASD_log(ASD_LogLevel_Error, stream, option,
                            "sd_bus_add_match function failed: %d", retcode);
                    result = ST_ERR;
                }
                else
                {
                    result = dbus_get_powerstate(state, &state->power_state);
                    if (result == ST_ERR)
                    {
                        ASD_log(ASD_LogLevel_Error, stream, option,
                                "dbus_get_powerstate failed");
                    }
                }
            }
            if (result == ST_ERR)
            {
                dbus_deinitialize(state);
            }
        }
        else
        {
            ASD_log(ASD_LogLevel_Error, stream, option,
                    "sd_bus_open_system failed: %d", retcode);
            result = ST_ERR;
        }
    }
    return result;
}

STATUS dbus_deinitialize(Dbus_Handle* state)
{
    STATUS result = ST_ERR;
    if (state && state->bus)
    {
        sd_bus_unref(state->bus);
        result = ST_OK;
    }
    return result;
}

STATUS dbus_power_reset(Dbus_Handle* state)
{
    STATUS result = ST_ERR;
    if (state && state->bus)
    {
        result = dbus_call_set_property_async(
            state, POWER_SERVICE_CHASSIS, POWER_OBJECT_PATH_CHASSIS,
            POWER_INTERFACE_NAME_CHASSIS, SET_POWER_STATE_METHOD_CHASSIS,
            POWER_RESET_ARGUMENT_CHASSIS);
    }
    return result;
}

STATUS dbus_power_off(Dbus_Handle* state)
{
    STATUS result = ST_ERR;

    if (state && state->bus)
    {
        result = dbus_call_set_property_async(
            state, POWER_SERVICE_CHASSIS, POWER_OBJECT_PATH_CHASSIS,
            POWER_INTERFACE_NAME_CHASSIS, SET_POWER_STATE_METHOD_CHASSIS,
            POWER_OFF_ARGUMENT_CHASSIS);
    }
    return result;
}

STATUS dbus_power_on(Dbus_Handle* state)
{
    STATUS result = ST_ERR;

    if (state && state->bus)
    {
        result = dbus_call_set_property_async(
            state, POWER_SERVICE_CHASSIS, POWER_OBJECT_PATH_CHASSIS,
            POWER_INTERFACE_NAME_CHASSIS, SET_POWER_STATE_METHOD_CHASSIS,
            POWER_ON_ARGUMENT_CHASSIS);
    }
    return result;
}

STATUS dbus_power_toggle(Dbus_Handle* state)
{
    STATUS result = ST_ERR;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = NULL;
    const char* value;

    if (state && state->bus)
    {
        int retcode = sd_bus_get_property(
            state->bus, POWER_SERVICE_CHASSIS, POWER_OBJECT_PATH_CHASSIS,
            POWER_INTERFACE_NAME_CHASSIS, GET_POWER_STATE_PROPERTY_CHASSIS,
            &error, &reply, "s");
        if (retcode >= 0)
        {
            sd_bus_message_read(reply, "s", &value);
            if (strcmp(value, POWER_ON_PROPERTY_CHASSIS) == 0)
                result = dbus_power_off(state);
            else
                result = dbus_power_on(state);
        }
        sd_bus_error_free(&error);
        sd_bus_message_unref(reply);
        reply = NULL;
    }

    return result;
}

STATUS dbus_get_powerstate(Dbus_Handle* state, int* value)
{
    STATUS result = ST_ERR;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = NULL;
    const char* value_string;
    if (state && state->bus)
    {
        if (state->power_state != STATE_UNKNOWN)
        {
            *value = state->power_state;
            result = ST_OK;
        }
        else
        {
            int retcode = sd_bus_get_property(
                state->bus, POWER_SERVICE_CHASSIS, POWER_OBJECT_PATH_CHASSIS,
                POWER_INTERFACE_NAME_CHASSIS, GET_POWER_STATE_PROPERTY_CHASSIS,
                &error, &reply, "s");
            if (retcode >= 0)
            {
                sd_bus_message_read(reply, "s", &value_string);
                if (strncmp(value_string, POWER_ON_PROPERTY_CHASSIS,
                            strlen(POWER_ON_PROPERTY_CHASSIS)) == 0)
                    *value = STATE_ON;
                else
                    *value = STATE_OFF;
                result = ST_OK;
            }
            else
            {
                ASD_log(ASD_LogLevel_Error, stream, option,
                        "sd_bus_get_property: %d", retcode);
                result = ST_ERR;
            }
            sd_bus_error_free(&error);
            sd_bus_message_unref(reply);
            reply = NULL;
        }
    }
    return result;
}

STATUS dbus_call_set_property_async(const Dbus_Handle* state,
                                    const char* service, const char* object,
                                    const char* interface_name,
                                    const char* method, const char* argument)
{
    STATUS result = ST_ERR;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    callb = false;
    sd_bus_message* m = NULL;
    const char* type = "s";
    int retcode = sd_bus_message_new_method_call(
        state->bus, &m, service, object, DBUS_PROPERTIES, DBUS_SET_METHOD);
    if (retcode >= 0)
        retcode = sd_bus_message_append(m, "ss", interface_name, method);
    if (retcode >= 0)
        retcode = sd_bus_message_open_container(m, 'v', type);
    if (retcode >= 0)
        retcode = sd_bus_message_append(m, type, argument);
    if (retcode >= 0)
        retcode = sd_bus_message_close_container(m);
    if (retcode >= 0)
        retcode = sd_bus_call_async(state->bus, NULL, m, sdbus_callback, &callb,
                                    SD_BUS_ASYNC_TIMEOUT);

    if (retcode >= 0)
    {
        result = ST_OK;
    }
    else
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "dbus_call_set_property_async failed: %d\n", retcode);
        sd_bus_error_set_errno(&error, retcode);
    }
    sd_bus_error_free(&error);
    return result;
}

int sdbus_callback(sd_bus_message* reply, void* userdata, sd_bus_error* error)
{
    bool* x = userdata;
    *x = 1;
    return ST_OK;
}

STATUS dbus_process_event(Dbus_Handle* state, ASD_EVENT* event)
{
    int result = ST_OK;
    int retcode;
    int old_power_state = state->power_state;
    do
    {
        retcode = sd_bus_process(state->bus, NULL);
        if (retcode < 0)
        {
            ASD_log(ASD_LogLevel_Error, stream, option,
                    "Failed sd_bus_process: %d", retcode);
            result = ST_ERR;
        }
    } while (retcode > 0);
    if (result == ST_OK)
    {
        if (state->power_state != old_power_state)
        {
            if (state->power_state == STATE_ON)
            {
                *event = ASD_EVENT_PWRRESTORE;
            }
            else
            {
                *event = ASD_EVENT_PWRFAIL;
            }
        }
    }
    return result;
}

int match_callback(sd_bus_message* msg, void* userdata, sd_bus_error* error)
{
    Dbus_Handle* state = (Dbus_Handle*)userdata;
    int result = ST_ERR;
    const char* str;
    //(s interface, a{sv}
    int retcode = sd_bus_message_skip(msg, "s");
    if (retcode < 0)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "Failed sd_bus_message_skip: %d", retcode);
        result = ST_ERR;
    }
    else
    {
        retcode =
            sd_bus_message_enter_container(msg, SD_BUS_TYPE_ARRAY, "{sv}");
        if (retcode < 0)
        {
            ASD_log(ASD_LogLevel_Error, stream, option,
                    "Failed to enter container: %d", retcode);
            result = ST_ERR;
        }
        else
        {
            while ((retcode = sd_bus_message_enter_container(
                        msg, SD_BUS_TYPE_DICT_ENTRY, "sv")) > 0)
            {
                retcode = sd_bus_message_read(msg, "s", &str);
                if (retcode < 0)
                {
                    ASD_log(ASD_LogLevel_Error, stream, option,
                            "Failed to read string inside dictionary: %d",
                            retcode);
                    result = ST_ERR;
                    break;
                }
                //  Focus in CurrentPowerState messages only,
                //  discard LastStateChangeTime and
                //  RequestedPowerTransition
                if (strcmp(str, GET_POWER_STATE_PROPERTY_CHASSIS) != 0)
                {
                    result = ST_ERR;
                    continue;
                }
                retcode = sd_bus_message_enter_container(
                    msg, SD_BUS_TYPE_VARIANT, "s");
                if (retcode < 0)
                {
                    ASD_log(ASD_LogLevel_Error, stream, option,
                            "Failed to enter container for variant: %d",
                            retcode);
                    result = ST_ERR;
                    break;
                }
                retcode = sd_bus_message_read(msg, "s", &str);
                if (retcode < 0)
                {
                    ASD_log(ASD_LogLevel_Error, stream, option,
                            "Failed to read variant: %d", retcode);
                    result = ST_ERR;
                    break;
                }
                else
                {
                    result = ST_OK;
                }
            }
            if (result == ST_OK)
            {
                if (strcmp(str, POWER_OFF_PROPERTY_CHASSIS) == 0)
                {
                    state->power_state = STATE_OFF;
                }
                else
                {
                    state->power_state = STATE_ON;
                }
                retcode = sd_bus_message_exit_container(msg);
                if (retcode < 0)
                {
                    ASD_log(ASD_LogLevel_Error, stream, option,
                            "Failed to exit container: %d", retcode);
                    result = ST_ERR;
                }
                else
                {
                    result == ST_OK;
                }
            }
        }
    }
    return result;
}
