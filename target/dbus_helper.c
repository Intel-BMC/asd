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

#include "dbus_helper.h"

#include <safe_mem_lib.h>
#include <safe_str_lib.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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
        sd_bus_flush_close_unrefp(&state->bus);
        if (close(state->fd) == 0)
        {
            state->fd = -1;
            result = ST_OK;
        }
        state->power_state = STATE_UNKNOWN;
        state->bus = NULL;
    }
    return result;
}

STATUS dbus_power_reset(Dbus_Handle* state)
{
    STATUS result = ST_ERR;
    if (state && state->bus)
    {
        result = dbus_call_set_property_async(
            state, POWER_SERVICE_HOST, POWER_OBJECT_PATH_HOST,
            POWER_INTERFACE_NAME_HOST, HOST_TRANSITION_PROPERTY,
            RESET_ARGUMENT_HOST);
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
    int cmp = 0;

    if (state && state->bus)
    {
        int retcode = sd_bus_get_property(
            state->bus, POWER_SERVICE_CHASSIS, POWER_OBJECT_PATH_CHASSIS,
            POWER_INTERFACE_NAME_CHASSIS, GET_POWER_STATE_PROPERTY_CHASSIS,
            &error, &reply, "s");
        if (retcode >= 0)
        {
            sd_bus_message_read(reply, "s", &value);
            strcmp_s(value, MAX_PLATFORM_PATH_SIZE, POWER_ON_PROPERTY_CHASSIS,
                     &cmp);
            if (cmp == 0)
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
                            strnlen_s(POWER_ON_PROPERTY_CHASSIS,
                                      MAX_PLATFORM_PATH_SIZE)) == 0)
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
    sd_bus_message_unref(m);
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
    int cmp = 0;

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
                strcmp_s(str, MAX_PLATFORM_PATH_SIZE,
                         GET_POWER_STATE_PROPERTY_CHASSIS, &cmp);
                if (cmp != 0)
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
                strcmp_s(str, MAX_PLATFORM_PATH_SIZE,
                         POWER_OFF_PROPERTY_CHASSIS, &cmp);
                if (cmp == 0)
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
            }
        }
    }
    return result;
}

STATUS dbus_get_path(const Dbus_Handle* state, char* name, char* path)
{
    struct sd_bus_message* reply = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int retcode;
    char type;
    const char* str = NULL;
    const char* contents = NULL;

    if ((state == NULL) || (name == NULL) || (path == NULL))
    {
        return ST_ERR;
    }

    int scan_depth = 0;
    int array_param_size = 1; // Only Identifier

    retcode = sd_bus_call_method(state->bus, OBJECT_MAPPER_SERVICE,
                                 OBJECT_MAPPER_PATH, OBJECT_MAPPER_INTERFACE,
                                 "GetSubTreePaths", &error, &reply, "sias", "",
                                 scan_depth, array_param_size, name);
    if (retcode < 0)
    {
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Error, stream, option, "sd_bus_call failed: %d",
                retcode);
#endif
        sd_bus_error_free(&error);
        sd_bus_message_unref(reply);
        return ST_ERR;
    }

    retcode = sd_bus_message_peek_type(reply, &type, &contents);
    if (retcode < 0)
    {
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Error, stream, option,
                "Failed to get peek type: %d", retcode);
#endif
        sd_bus_error_free(&error);
        sd_bus_message_unref(reply);
        return ST_ERR;
    }

    retcode = sd_bus_message_enter_container(reply, type, contents);
    if (retcode < 0)
    {
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Error, stream, option,
                "Failed to enter container: %d", retcode);
#endif
        sd_bus_error_free(&error);
        sd_bus_message_unref(reply);
        return ST_ERR;
    }

    retcode = sd_bus_message_read(reply, "s", &str);
    if (retcode < 0)
    {
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Error, stream, option, "Failed to read string: %d",
                retcode);
#endif
        sd_bus_error_free(&error);
        sd_bus_message_unref(reply);
        return ST_ERR;
    }

    if (str == NULL)
    {
        sd_bus_error_free(&error);
        sd_bus_message_unref(reply);
        return ST_ERR;
    }

#ifdef ENABLE_DEBUG_LOGGING
    ASD_log(ASD_LogLevel_Debug, stream, option, "Read string: %s", str);
#endif

    if (strcpy_s(path, MAX_PLATFORM_PATH_SIZE, str))
    {
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Error, stream, option,
                "strcpy_s: platform path failed");
#endif
        sd_bus_error_free(&error);
        sd_bus_message_unref(reply);
        return ST_ERR;
    }

    sd_bus_message_exit_container(reply);
    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    return ST_OK;
}

STATUS dbus_get_platform_path(const Dbus_Handle* state, char* path)
{
    STATUS result = ST_ERR;
    static char platform_path[MAX_PLATFORM_PATH_SIZE] = {0};
    // If we already have the path don't go to dbus and return saved value
    if (platform_path[0] != '\0')
    {
        if (strcpy_s(path, MAX_PLATFORM_PATH_SIZE, platform_path))
        {
#ifdef ENABLE_DEBUG_LOGGING
            ASD_log(ASD_LogLevel_Error, stream, option,
                    "strcpy_s: platform path failed");
#endif
            return ST_ERR;
        }
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Info, stream, option, "Return saved path: %s",
                platform_path);
#endif
        return ST_OK;
    }
    result = dbus_get_path(state, MOTHERBOARD_IDENTIFIER, path);
    if (result == ST_OK)
    {
        if (strcpy_s(platform_path, MAX_PLATFORM_PATH_SIZE, path))
        {
#ifdef ENABLE_DEBUG_LOGGING
            ASD_log(ASD_LogLevel_Error, stream, option,
                    "strcpy_s: platform path failed");
#endif
            platform_path[0] = '\0';
        }
    }
    return result;
}
STATUS dbus_get_platform_id(const Dbus_Handle* state, uint64_t* pid)
{
    STATUS result;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int retcode = 0;
    char path[MAX_PLATFORM_PATH_SIZE] = {0};
    static uint64_t platform_id = 0;
    static bool read_from_dbus = true;

    if (state == NULL)
    {
        return ST_ERR;
    }

    // If we already have the pid don't go to dbus and return saved value
    if (read_from_dbus == false)
    {
        *pid = platform_id;
    }

    result = dbus_get_platform_path(state, path);
    if (result != ST_OK)
    {
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Error, stream, option,
                "Failed to dbus_get_platform_path: %d", result);
#endif
        return result;
    }

#ifdef ENABLE_DEBUG_LOGGING
    ASD_log(ASD_LogLevel_Debug, stream, option, "path is: %s", path);
#endif

    retcode = sd_bus_get_property_trivial(state->bus, ENTITY_MANAGER_SERVICE,
                                          path, MOTHERBOARD_IDENTIFIER,
                                          "ProductId", &error, 't', pid);
    if (retcode < 0)
    {
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Error, stream, option,
                "sd_bus_get_property_trivial failed %d", retcode);
#endif
        sd_bus_error_free(&error);
        return ST_ERR;
    }
    read_from_dbus = false;
    platform_id = *pid;
    sd_bus_error_free(&error);
    return result;
}

STATUS dbus_read_asd_config(const Dbus_Handle* state, const char* interface,
                            const char* name, char type, void* var)
{
    STATUS result = ST_OK;
    int retcode;
    static char path[MAX_PLATFORM_PATH_SIZE] = {0};
    bool* bval = NULL;
    char* str = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;

    if ((state == NULL) || (name == NULL) || (interface == NULL) ||
        (var == NULL))
    {
        return ST_ERR;
    }

    if (path[0] == '\0')
    {
        result = dbus_get_path(state, ASD_CONFIG_PATH, path);
    }

    if (result != ST_OK)
    {
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Error, stream, option,
                "Failed to dbus_get_path: %d", result);
#endif
        return result;
    }

    switch (type)
    {
        case 'b':
            bval = (bool*)var;
            retcode = sd_bus_get_property_trivial(
                state->bus, ENTITY_MANAGER_SERVICE, path, interface, name,
                &error, type, bval);
            if (retcode < 0)
            {
#ifdef ENABLE_DEBUG_LOGGING
                ASD_log(ASD_LogLevel_Trace, stream, option,
                        "sd_bus_get_property_trivial can't be found or read %d",
                        retcode);
#endif
                result = ST_ERR;
            }
            break;
        case 's':
            retcode =
                sd_bus_get_property_string(state->bus, ENTITY_MANAGER_SERVICE,
                                           path, interface, name, &error, &str);
            if (retcode < 0)
            {
#ifdef ENABLE_DEBUG_LOGGING
                ASD_log(ASD_LogLevel_Trace, stream, option,
                        "sd_bus_get_property_string can't be found or read %d",
                        retcode);
#endif
                result = ST_ERR;
            }
            else
            {
                if (str != NULL)
                {
                    strcpy_s(var, MAX_PLATFORM_PATH_SIZE, str);
                    free(str);
                    str = NULL;
                }
            }
            break;
        default:
            result = ST_ERR;
            break;
    }
    sd_bus_error_free(&error);
    return result;
}

STATUS dbus_get_asd_interface_paths(const Dbus_Handle* state,
                                    const char* names[],
                                    char interfaces[][MAX_PLATFORM_PATH_SIZE],
                                    int arr_size)
{
    STATUS result = ST_OK;
    struct sd_bus_message* reply = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int retcode;
    char type;
    const char* str;
    const char* contents = NULL;
    static char interface[MAX_PLATFORM_PATH_SIZE] = {0};

    if ((state == NULL) || (names == NULL) || (interfaces == NULL))
    {
        return ST_ERR;
    }

    int scan_depth = 0;
    int array_param_size = 1;

    for (int i = 0; i < arr_size; i++)
    {
        explicit_bzero(interface, sizeof(interface));
        str = NULL;
        sprintf_s(interface, MAX_PLATFORM_PATH_SIZE, "%s.%s", ASD_CONFIG_PATH,
                  names[i]);

        retcode = sd_bus_call_method(
            state->bus, OBJECT_MAPPER_SERVICE, OBJECT_MAPPER_PATH,
            OBJECT_MAPPER_INTERFACE, "GetSubTreePaths", &error, &reply, "sias",
            "", scan_depth, array_param_size, interface);
        if (retcode < 0)
        {
#ifdef ENABLE_DEBUG_LOGGING
            ASD_log(ASD_LogLevel_Error, stream, option,
                    "sd_bus_call failed: %d", retcode);
#endif
            return ST_ERR;
        }

        retcode = sd_bus_message_peek_type(reply, &type, &contents);
        if (retcode < 0)
        {
#ifdef ENABLE_DEBUG_LOGGING
            ASD_log(ASD_LogLevel_Error, stream, option,
                    "Failed to get peek type: %d", retcode);
#endif
            return ST_ERR;
        }

        retcode = sd_bus_message_enter_container(reply, type, contents);
        if (retcode < 0)
        {
#ifdef ENABLE_DEBUG_LOGGING
            ASD_log(ASD_LogLevel_Error, stream, option,
                    "Failed to enter container: %d", retcode);
#endif
            return ST_ERR;
        }
        retcode = sd_bus_message_read(reply, "s", &str);
        if (retcode < 0)
        {
            ASD_log(ASD_LogLevel_Error, stream, option,
                    "Failed to read string inside dictionary: %d", retcode);
            return ST_ERR;
        }
        if (str != NULL)
        {
            strcpy_s((char*)&interfaces[i], MAX_PLATFORM_PATH_SIZE, interface);
            ASD_log(ASD_LogLevel_Info, stream, option, "found interface[%d] %s",
                    i, interfaces[i]);
        }
        retcode = sd_bus_message_exit_container(reply);
        if (retcode < 0)
        {
            ASD_log(ASD_LogLevel_Error, stream, option,
                "Failed to exit container: %d", retcode);
            return ST_ERR;
        }
    }

    return ST_OK;
}

STATUS dbus_get_platform_bus_config(const Dbus_Handle* state,
                                    bus_options* busopt)
{
    STATUS status = ST_OK;
    static char path[MAX_PLATFORM_PATH_SIZE] = {0};
    char* str = NULL;
    uint64_t bus = 0;
    int retcode = 0;
    int cmp = 0;
    sd_bus_error error = SD_BUS_ERROR_NULL;

    if ((state == NULL) || (busopt == NULL))
    {
        return ST_ERR;
    }

    // Load bus default config with all buses disabled
    busopt->enable_i2c = false;
    busopt->enable_i3c = false;
    busopt->enable_spp = false;

    for (int i = 0; i < MAX_IxC_BUSES + MAX_SPP_BUSES; i++)
    {
        busopt->bus_config_type[i] = BUS_CONFIG_NOT_ALLOWED;
        busopt->bus_config_map[i] = 0;
    }

    // Find ASD object path
    if (path[0] == '\0')
    {
        status = dbus_get_path(state, ASD_CONFIG_PATH, path);
    }

    if (status != ST_OK)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "Failed to dbus_get_path: %d", status);
        return status;
    }

    ASD_log(ASD_LogLevel_Debug, stream, option, "ASD Path: %s", path);

    static char interface[MAX_PLATFORM_PATH_SIZE] = {0};
    explicit_bzero(interface, sizeof(interface));

    // Read i2c/i3c/spp bus config from BusConfig entry inside ASD object
    for (int i = 0, j = 0, k = 0; i < MAX_IxC_BUSES + MAX_SPP_BUSES; i++)
    {
        str = NULL;
        sprintf_s(interface, MAX_PLATFORM_PATH_SIZE, "%s.%s%d", ASD_CONFIG_PATH,
                  "BusConfig", i);

        ASD_log(ASD_LogLevel_Debug, stream, option, "Bus Config interface: %s",
                interface);

        retcode = sd_bus_get_property_trivial(
            state->bus, ENTITY_MANAGER_SERVICE, path, interface, "BusNum",
            &error, 't', &bus);
        if (retcode < 0)
        {
            ASD_log(ASD_LogLevel_Debug, stream, option,
                    "sd_bus_get_property_trivial can't be found or read %d",
                    retcode);
            break;
        }

        ASD_log(ASD_LogLevel_Debug, stream, option, "BusNum read: %d", bus);

        retcode =
            sd_bus_get_property_string(state->bus, ENTITY_MANAGER_SERVICE, path,
                                       interface, "BusType", &error, &str);
        if (retcode < 0)
        {
            ASD_log(ASD_LogLevel_Error, stream, option,
                    "sd_bus_get_property_string can't be found or read %d",
                    retcode);
            status = ST_ERR;
            break;
        }

        ASD_log(ASD_LogLevel_Debug, stream, option, "BusType read: %s", str);

        strcmp_s(str, MAX_FIELD_NAME_SIZE,
                 BUS_CONFIG_TYPE_STRINGS[BUS_CONFIG_NOT_ALLOWED], &cmp);
        if (cmp == 0)
        {
            busopt->bus_config_map[j] = (uint8_t)bus;
            busopt->bus_config_type[j++] = BUS_CONFIG_NOT_ALLOWED;
        }
        else
        {
            if (j >= MAX_IxC_BUSES || k >= MAX_SPP_BUSES)
            {
                ASD_log(ASD_LogLevel_Error, stream, option,
                        "Max number of bus configs reached");
                        status = ST_OK;
                        break;
            }
            strcmp_s(str, MAX_FIELD_NAME_SIZE,
                     BUS_CONFIG_TYPE_STRINGS[BUS_CONFIG_I2C], &cmp);
            if (cmp == 0)
            {
                busopt->bus_config_map[j] = (uint8_t)bus;
                busopt->bus_config_type[j++] = BUS_CONFIG_I2C;
                if (!busopt->enable_i2c)
                {
                    busopt->enable_i2c = true;
                    busopt->bus = (uint8_t)bus;
                }
            }
            else
            {
                strcmp_s(str, MAX_FIELD_NAME_SIZE,
                         BUS_CONFIG_TYPE_STRINGS[BUS_CONFIG_I3C], &cmp);
                if (cmp == 0)
                {
                    busopt->bus_config_map[j] = (uint8_t)bus;
                    busopt->bus_config_type[j++] = BUS_CONFIG_I3C;
                    if (!busopt->enable_i3c)
                    {
                        busopt->enable_i3c = true;
                        busopt->bus = (uint8_t)bus;
                    }
                }
                else
                {
                    strcmp_s(str, MAX_FIELD_NAME_SIZE,
                             BUS_CONFIG_TYPE_STRINGS[BUS_CONFIG_SPP], &cmp);
                    if (cmp == 0)
                    {
                        busopt->bus_config_map[MAX_IxC_BUSES + k] = (uint8_t)bus;
                        busopt->bus_config_type[MAX_IxC_BUSES + k++] = BUS_CONFIG_SPP;
                        if (!busopt->enable_spp)
                        {
                            busopt->enable_spp = true;
                            busopt->bus = (uint8_t)bus;
                        }
                    }
                    else
                    {
                        ASD_log(ASD_LogLevel_Debug, stream, option,
                                "Unknown bus config type");
                        status = ST_ERR;
                        break;
                    }
                }
            }
        }
        free(str);
        str = NULL;
    }

    // If config read fails, then return all buses disabled
    if (status != ST_OK)
    {
        busopt->enable_i2c = false;
        busopt->enable_i3c = false;

        for (int i = 0; i < MAX_IxC_BUSES; i++)
        {
            busopt->bus_config_type[i] = BUS_CONFIG_NOT_ALLOWED;
            busopt->bus_config_map[i] = 0;
        }
    }
    sd_bus_error_free(&error);
    return status;
}

STATUS dbus_read_i3c_ownership(const Dbus_Handle* state, I3c_Ownership* owner)
{
    int retcode = 0;
    sd_bus_error error = SD_BUS_ERROR_NULL;

    if ((state == NULL) || (owner == NULL))
    {
        return ST_ERR;
    }

    retcode = sd_bus_get_property_trivial(state->bus, CLTT_SERVICE, CLTT_PATH,
                                          CLTT_INTERFACE, "IsBmcOwner", &error,
                                          'b', owner);
    if (retcode < 0)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "sd_bus_get_property_trivial failed to read IsBmcOwner %d",
                retcode);
        return ST_ERR;
    }
    ASD_log(ASD_LogLevel_Debug, stream, option,
            "dbus_get_i3c_ownership IsBmcOwner %s", *owner ? "true" : "false");
    sd_bus_error_free(&error);
    return ST_OK;
}

STATUS dbus_req_i3c_ownership(const Dbus_Handle* state, int* token)
{
    struct sd_bus_message* reply = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int retcode;
    bool isOwner;
    char type;
    const char* contents = NULL;

    if ((state == NULL) || (token == NULL))
    {
        return ST_ERR;
    }

    retcode =
        sd_bus_call_method(state->bus, CLTT_SERVICE, CLTT_PATH, CLTT_INTERFACE,
                           "RequestOwnership", &error, &reply, "s", "Forced");
    if (retcode < 0)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "sd_bus_call_method RequestOwnership failed: %d", retcode);
        sd_bus_error_free(&error);
        sd_bus_message_unref(reply);
        return ST_ERR;
    }

    retcode = sd_bus_message_peek_type(reply, &type, &contents);
    if (retcode < 0)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "Failed to get peek type: %d", retcode);
        sd_bus_error_free(&error);
        sd_bus_message_unref(reply);
        return ST_ERR;
    }

    retcode = sd_bus_message_enter_container(reply, type, contents);
    if (retcode < 0)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "Failed to enter container: %d", retcode);
        sd_bus_error_free(&error);
        sd_bus_message_unref(reply);
        return ST_ERR;
    }

    retcode = sd_bus_message_read(reply, "i", token);
    if (retcode < 0)
    {
        ASD_log(ASD_LogLevel_Error, stream, option, "Failed to read token: %d",
                retcode);
        sd_bus_error_free(&error);
        sd_bus_message_unref(reply);
        return ST_ERR;
    }
    ASD_log(ASD_LogLevel_Debug, stream, option, "Read token: %d", *token);

    retcode = sd_bus_message_read(reply, "b", &isOwner);
    if (retcode < 0)
    {
        ASD_log(ASD_LogLevel_Error, stream, option, "Failed to read bool: %d",
                retcode);
        sd_bus_error_free(&error);
        sd_bus_message_unref(reply);
        return ST_ERR;
    }
    ASD_log(ASD_LogLevel_Debug, stream, option, "isOwner: %d %s", isOwner,
            isOwner ? "true" : "false");

    sd_bus_message_exit_container(reply);
    sd_bus_error_free(&error);
    if (isOwner == false)
        return ST_ERR;

    return ST_OK;
}

STATUS dbus_rel_i3c_ownership(const Dbus_Handle* state, int token)
{
    struct sd_bus_message* reply = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int retcode;

    if (state == NULL)
    {
        return ST_ERR;
    }

    retcode = sd_bus_call_method(state->bus, CLTT_SERVICE, CLTT_PATH,
                                 CLTT_INTERFACE, "ReleaseOwnership", &error,
                                 &reply, "si", "Forced", token);
    if (retcode < 0)
    {
        ASD_log(ASD_LogLevel_Error, stream, option,
                "sd_bus_call_method ReleaseOwnership failed: %d", retcode);
        sd_bus_error_free(&error);
        sd_bus_message_unref(reply);
        return ST_ERR;
    }
    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    return ST_OK;
}
