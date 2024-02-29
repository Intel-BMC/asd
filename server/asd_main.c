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

#include "asd_main.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <getopt.h>
#include <netdb.h>
#include <pthread.h>
#include <safe_str_lib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <systemd/sd-journal.h>
#include <unistd.h>

#include "asd_target_interface.h"
asd_state main_state = {};
extnet_conn_t* p_extconn = NULL;
bool b_data_pending = false;

static void send_remote_log_message(ASD_LogLevel asd_level,
                                    ASD_LogStream asd_stream,
                                    const char* message);

#ifndef UNIT_TEST_MAIN
int main(int argc, char** argv)
{
    return asd_main(argc, argv);
}
#endif

int asd_main(int argc, char** argv)
{
    STATUS result = ST_ERR;

    ASD_initialize_log_settings(DEFAULT_LOG_LEVEL, DEFAULT_LOG_STREAMS, false,
                                NULL, NULL);

    if (process_command_line(argc, argv, &main_state.args))
    {
        result = init_asd_state();

        if (result == ST_OK)
        {
            result = request_processing_loop(&main_state);
            ASD_log(ASD_LogLevel_Warning, ASD_LogStream_Daemon,
                    ASD_LogOption_None, "ASD server closing.");
        }
        deinit_asd_state(&main_state);
        free(main_state.extnet);
        free(main_state.session);
    }
    return result == ST_OK ? 0 : 1;
}

bool validateCharInputs(char* input, char* bad_char, bool alphabetUpper,
                        bool alphabetLower, bool numerics,
                        bool extraChars, bool comma, bool dash)
{
    uint16_t strSize = strlen(input);
    bool goodValues=false;
    if (strSize > 0 && strSize < MAX_INPUT_SIZE)
    {
        for (size_t i = 0; i < strSize; i++)
        {
            if (alphabetUpper && (input[i] >= 'A' && input[i] <= 'Z'))
            {
                goodValues = true;
            }
            else if (alphabetLower && (input[i] >= 'a' && input[i] <= 'z'))
            {
                goodValues = true;
            }
            else if (numerics && (input[i] >= '0' && input[i] <= '9'))
            {
                goodValues = true;
            }
            else if (extraChars && (input[i] == '.' || input[i] == '/'))
            {
                goodValues = true;
            }
            else if (comma && (input[i] == ','))
            {
                goodValues = true;
            }
            else if (dash && (input[i] == '-'))
            {
                goodValues = true;
            }
            else
            {
                *bad_char = input[i];
                goodValues = false;
                break;
            }
        }
    }
    return goodValues;
}

bool process_command_line(int argc, char** argv, asd_args* args)
{
    int c = 0;
    opterr = 0;              // prevent getopt_long from printing shell messages
    uint8_t bus_counter = 0; // Up to 4 buses
    uint8_t spp_counter = 0; // Up to 8 buses

    // Set Default argument values.

    args->busopt.enable_i2c = DEFAULT_I2C_ENABLE;
    args->busopt.enable_i3c = DEFAULT_I3C_ENABLE;
    args->busopt.enable_spp = DEFAULT_SPP_ENABLE;
    args->busopt.bus = DEFAULT_I2C_BUS;
    args->use_syslog = DEFAULT_LOG_TO_SYSLOG;
    args->log_level = DEFAULT_LOG_LEVEL;
    args->log_streams = DEFAULT_LOG_STREAMS;
    args->session.n_port_number = DEFAULT_PORT;
    args->session.cp_certkeyfile = DEFAULT_CERT_FILE;
    args->session.cp_net_bind_device = NULL;
    args->session.e_extnet_type = EXTNET_HDLR_TLS;
    args->session.e_auth_type = AUTH_HDLR_PAM;
    args->xdp_fail_enable = DEFAULT_XDP_FAIL_ENABLE;
    main_state.config.jtag.xdp_fail_enable = DEFAULT_XDP_FAIL_ENABLE;

    enum
    {
        ARG_LOG_LEVEL = 256,
        ARG_LOG_STREAMS,
        ARG_HELP,
        ARG_XDP
    };

    struct option opts[] = {
        {"xdp-ignore", 0, NULL, ARG_XDP},
        {"log-level", 1, NULL, ARG_LOG_LEVEL},
        {"log-streams", 1, NULL, ARG_LOG_STREAMS},
        {"help", 0, NULL, ARG_HELP},
        {NULL, 0, NULL, 0},
    };

    while ((c = getopt_long(argc, argv, "p:uk:n:si:c:d:", opts, NULL)) != -1)
    {
        switch (c)
        {
            case 'p':
            {
                char ch=0;
                if(validateCharInputs(optarg, &ch, false, false, true, false,
                                       false, false))
                {
                    uint16_t port = (uint16_t)strtol(optarg, NULL, 10);
                    fprintf(stderr, "Setting Port: %d\n", port);
                    args->session.n_port_number = port;
                }
                else
                {
                    fprintf(stderr,
                            "Invalid character in port: %c.\n",
                            ch);
                    showUsage(argv);
                    return false;
                }
                break;
            }
            case 's':
            {
                args->use_syslog = true;
                break;
            }
            case 'u':
            {
                args->session.e_extnet_type = EXTNET_HDLR_NON_ENCRYPT;
                args->session.e_auth_type = AUTH_HDLR_NONE;
                break;
            }
            case 'k':
            {
                char ch=0;
                if(validateCharInputs(optarg, &ch, true, true, true, true,
                                       false, false))
                {
                    args->session.cp_certkeyfile = optarg;
                }
                else
                {
                    fprintf(stderr,
                            "Invalid character in certificate filename: %c.\n",
                            ch);
                    showUsage(argv);
                    return false;
                }
                break;
            }
            case 'n':
            {
                char ch=0;
                if(validateCharInputs(optarg, &ch, true, true, true, false,
                                       false, false))
                {
                    args->session.cp_net_bind_device = optarg;
                }
                else
                {
                    fprintf(stderr,
                            "Invalid character in network bind device: %c.\n",
                            ch);
                    showUsage(argv);
                    return false;
                }
                break;
            }
            case 'i':
            {
                char* pch;
                uint8_t bus;
                bool first_i2c = true;
                char* endptr;
                args->busopt.enable_i2c = true;
                char ch=0;
                if(!validateCharInputs(optarg, &ch, false, false, true, false,
                                        true, false))
                {
                    fprintf(stderr,
                            "Invalid character in i2c bus: %c.\n",
                            ch);
                    showUsage(argv);
                    return false;
                }
                pch = strtok(optarg, ",");
                while (pch != NULL)
                {
                    errno = 0;
                    bus = (uint8_t)strtol(pch, &endptr, 10);
                    if ((errno == ERANGE) || (endptr == pch))
                    {
                        fprintf(stderr, "Wrong I2C bus list arguments(-i)\n");
                        break;
                    }
                    if (bus_counter >= MAX_IxC_BUSES)
                    {
                        fprintf(stderr, "Discard I2C bus: %d\n", bus);
                    }
                    else
                    {
                        if (first_i2c)
                        {
                            args->busopt.bus = bus;
                            first_i2c = false;
                        }
                        fprintf(stderr, "Enabling I2C bus: %d\n", bus);
                        args->busopt.bus_config_type[bus_counter] =
                            BUS_CONFIG_I2C;
                        args->busopt.bus_config_map[bus_counter] = bus;
                    }
                    pch = strtok(NULL, ",");
                    bus_counter++;
                }
                break;
            }
            case 'c':
            {
                char* pch;
                uint8_t bus;
                bool first_i3c = true;
                char* endptr;
                args->busopt.enable_i3c = true;
                char ch = 0;
                if(!validateCharInputs(optarg, &ch, false, false, true, false,
                                        true, false))
                {
                    fprintf(stderr,
                            "Invalid character in i3c bus list: %c.\n",
                            ch);
                    showUsage(argv);
                    return false;
                }
                pch = strtok(optarg, ",");
                while (pch != NULL)
                {
                    errno = 0;
                    bus = (uint8_t)strtol(pch, &endptr, 10);
                    if ((errno == ERANGE) || (endptr == pch))
                    {
                        fprintf(stderr, "Wrong I3C bus list arguments(-c)\n");
                        break;
                    }
                    if (bus_counter >= MAX_IxC_BUSES)
                    {
                        fprintf(stderr, "Discard I3C bus: %d\n", bus);
                    }
                    else
                    {
                        if (first_i3c)
                        {
                            args->busopt.bus = bus;
                            first_i3c = false;
                        }
                        fprintf(stderr, "Enabling I3C bus: %d\n", bus);
                        args->busopt.bus_config_type[bus_counter] =
                            BUS_CONFIG_I3C;
                        args->busopt.bus_config_map[bus_counter] = bus;
                    }
                    pch = strtok(NULL, ",");
                    bus_counter++;
                }
                break;
            }
            case 'd':
            {
                char* pch;
                uint8_t bus;
                uint8_t spp_bus_index = 0;
                bool first_spp = true;
                char* endptr;
                args->busopt.enable_spp = true;
                char ch = 0;
                if(!validateCharInputs(optarg, &ch, false, false, true, false,
                                        true, false))
                {
                    fprintf(stderr,
                            "Invalid character in spp bus list: %c.\n",
                            ch);
                    showUsage(argv);
                    return false;
                }
                pch = strtok(optarg, ",");
                while (pch != NULL)
                {
                    errno = 0;
                    bus = (uint8_t)strtol(pch, &endptr, 10);
                    if ((errno == ERANGE) || (endptr == pch))
                    {
                        fprintf(stderr, "Wrong SPP bus list arguments(-d)\n");
                        break;
                    }
                    if (spp_counter >= MAX_SPP_BUSES)
                    {
                        fprintf(stderr, "Discard SPP bus: %d\n", bus);
                    }
                    else
                    {
                        if (first_spp)
                        {
                            args->busopt.bus = bus;
                            first_spp = false;
                        }
                        fprintf(stderr, "Enabling I3C(SPP) bus: %d\n", bus);
                        spp_bus_index = MAX_IxC_BUSES + spp_counter;
                        args->busopt.bus_config_type[spp_bus_index] =
                            BUS_CONFIG_SPP;
                        args->busopt.bus_config_map[spp_bus_index] = bus;
                    }
                    pch = strtok(NULL, ",");
                    spp_counter++;
                }
                break;
            }
            case ARG_XDP:
            {
                args->xdp_fail_enable = false;
                main_state.config.jtag.xdp_fail_enable = false;
                fprintf(stderr, "Ignore XDP presence\n");
                break;
            }
            case ARG_LOG_LEVEL:
            {
                char ch=0;
                if(!validateCharInputs(optarg, &ch, true, true, false, false,
                                        false, true))
                {
                    fprintf(stderr,
                            "Invalid character in log level: %c.\n",
                            ch);
                    showUsage(argv);
                    return false;
                }
                if (!strtolevel(optarg, &args->log_level))
                {
                    showUsage(argv);
                    return false;
                }
                break;
            }
            case ARG_LOG_STREAMS:
            {
                char ch = 0;
                if(!validateCharInputs(optarg, &ch, true, true, false, false,
                                        true, true))
                {
                    fprintf(stderr,
                            "Invalid character in log streams: %c.\n",
                            ch);
                    showUsage(argv);
                    return false;
                }
                if (!strtostreams(optarg, &args->log_streams))
                {
                    showUsage(argv);
                    return false;
                }
                break;
            }
            case ARG_HELP:
            default:
            {
                showUsage(argv);
                return false;
            }
        }
    }
    return true;
}

void showUsage(char** argv)
{
    ASD_log(
        ASD_LogLevel_Error, ASD_LogStream_Daemon, ASD_LogOption_No_Remote,
        "\nUsage: %s [option]\n\n"
        "  -p <number> Port number (default=%d)\n\n"
        "  -s          Route log messages to the system log\n"
        "  -u          Run in plain TCP, no SSL (default: SSL/Auth Mode)\n"
        "  -k <file>   Specify SSL Certificate/Key file (default: %s)\n"
        "  -n <device> Bind only to specific network device (eth0, etc)\n"
        "  -i <buses>  Decimal i2c allowed bus list(default: none)\n"
        "              Use comma to enable multiple i2c buses: -i 2,9\n"
        "              The first bus will be used as default bus.\n"
        "              The total number of i2c/i3c bus assignments cannot\n"
        "              exceed %d buses.\n"
        "  -c <buses>  Decimal i3c allowed bus list(default: none)\n"
        "              Use comma to enable multiple i3c buses: -c 0,1,2,3\n"
        "              The first bus will be used as default bus.\n"
        "              The total number of i2c/i3c bus assignments cannot\n"
        "              exceed %d buses.\n"
        "  -d <buses>  Decimal i3c debug(SPP) allowed bus list(default: none)\n"
        "              Use comma to enable multiple i3c buses: -d 0,1,2,3\n"
        "              The first bus will be used as default bus.\n"
        "              The total number of i3c bus assignments cannot exceed\n"
        "              8 buses.\n"
        "  --xdp-ignore               Connect ASD even with XDP connected\n"
        "                             Warning: Driving signals from both\n"
        "                             ASD and XDP may cause electrical issues\n"
        "                             or lead into a HW damage.\n"
        "  --log-level=<level>        Specify Logging Level (default: %s)\n"
        "                             Levels:\n"
        "                               %s\n"
        "                               %s\n"
        "                               %s\n"
        "                               %s\n"
        "                               %s\n"
        "                               %s\n"
        "  --log-streams=<streams>    Specify Logging Streams (default: %s)\n"
        "                             Multiple streams can be comma "
        "separated.\n"
        "                             Streams:\n"
        "                               %s\n"
        "                               %s\n"
        "                               %s\n"
        "                               %s\n"
        "                               %s\n"
        "                               %s\n"
        "                               %s\n"
        "                               %s\n"
        "                               %s\n"
        "  --help                     Show this list\n"
        "\n"
        "Examples:\n"
        "\n"
        "Log from the daemon and jtag at trace level.\n"
        "     asd --log-level=trace --log-streams=daemon,jtag\n"
        "Enable i2c bus 2 and bus 9.\n"
        "     asd -i 2,9\n"
        "\n"
        "Default logging, only listen on eth0.\n"
        "     asd -n eth0\n"
        "\n",
        argv[0], DEFAULT_PORT, DEFAULT_CERT_FILE,
        MAX_IxC_BUSES, MAX_IxC_BUSES,
        ASD_LogLevelString[DEFAULT_LOG_LEVEL],
        ASD_LogLevelString[ASD_LogLevel_Off],
        ASD_LogLevelString[ASD_LogLevel_Error],
        ASD_LogLevelString[ASD_LogLevel_Warning],
        ASD_LogLevelString[ASD_LogLevel_Info],
        ASD_LogLevelString[ASD_LogLevel_Debug],
        ASD_LogLevelString[ASD_LogLevel_Trace],
        streamtostring(DEFAULT_LOG_STREAMS), streamtostring(ASD_LogStream_All),
        streamtostring(ASD_LogStream_Test), streamtostring(ASD_LogStream_I2C),
        streamtostring(ASD_LogStream_Pins), streamtostring(ASD_LogStream_JTAG),
        streamtostring(ASD_LogStream_Network),
        streamtostring(ASD_LogStream_Daemon),
        streamtostring(ASD_LogStream_SDK),
        streamtostring(ASD_LogStream_SPP));
}

// This function maps the open ipc log levels to the levels
// we have already defined in this codebase.
void init_logging_map(void)
{
    main_state.config.ipc_asd_log_map[ASD_LogLevel_Off] = IPC_LogType_Off;
    main_state.config.ipc_asd_log_map[ASD_LogLevel_Debug] = IPC_LogType_Debug;
    main_state.config.ipc_asd_log_map[ASD_LogLevel_Info] = IPC_LogType_Info;
    main_state.config.ipc_asd_log_map[ASD_LogLevel_Warning] = IPC_LogType_Warning;
    main_state.config.ipc_asd_log_map[ASD_LogLevel_Error] = IPC_LogType_Error;
    main_state.config.ipc_asd_log_map[ASD_LogLevel_Trace] = IPC_LogType_Trace;
}

bool main_should_remote_log(ASD_LogLevel asd_level, ASD_LogStream asd_stream)
{
    (void)asd_stream;
    bool result = false;
    if (main_state.config.remote_logging.logging_level != IPC_LogType_Off)
    {
        if (main_state.config.remote_logging.logging_level <=
            main_state.config.ipc_asd_log_map[asd_level])
            result = true;
    }
    return result;
}

STATUS init_asd_state(void)
{

    STATUS result = set_config_defaults(&main_state.config, &main_state.args.busopt);

    if (result == ST_OK)
    {
        ASD_initialize_log_settings(main_state.args.log_level,
                                    main_state.args.log_streams,
                                    main_state.args.use_syslog, NULL, NULL);

        main_state.extnet =
            extnet_init(main_state.args.session.e_extnet_type,
                        main_state.args.session.cp_certkeyfile, MAX_SESSIONS);
        if (!main_state.extnet)
        {
            result = ST_ERR;
        }
    }

    if (result == ST_OK)
    {
        result = auth_init(main_state.args.session.e_auth_type, NULL);
    }

    if (result == ST_OK)
    {
        main_state.session = session_init(main_state.extnet);
        if (!main_state.session)
            result = ST_ERR;
        else
        {
            main_state.event_fd = eventfd(0, O_NONBLOCK);
            if (main_state.event_fd == -1)
            {
                ASD_log(ASD_LogLevel_Error, ASD_LogStream_Daemon,
                        ASD_LogOption_None,
                        "Could not setup event file descriptor.");
                result = ST_ERR;
            }
            else
            {
                result = extnet_open_external_socket(
                    main_state.extnet, main_state.args.session.cp_net_bind_device,
                    main_state.args.session.n_port_number, &main_state.host_fd);
                if (result != ST_OK)
                    ASD_log(ASD_LogLevel_Error, ASD_LogStream_Daemon,
                            ASD_LogOption_None,
                            "Could not open the external socket");
            }
        }
    }

    return result;
}

void deinit_asd_state(asd_state* state)
{
    session_close_all(state->session);
    if (state->host_fd != 0)
        close(state->host_fd);

    if (asd_api_target_deinit() != ST_OK)
    {
        ASD_log(ASD_LogLevel_Error, ASD_LogStream_Daemon,
                ASD_LogOption_None, "Failed to de-initialize the asd_msg");
    }
}

STATUS send_out_msg_on_socket(unsigned char* buffer, size_t length)
{
    extnet_conn_t authd_conn;

    int cnt = 0;
    STATUS result = ST_ERR;

    if (buffer)
    {
        result = ST_OK;
        if (session_get_authenticated_conn(main_state.session,
                                           &authd_conn) != ST_OK)
        {
            result = ST_ERR;
        }

        if (result == ST_OK)
        {
            cnt = extnet_send(main_state.extnet, &authd_conn, buffer,
                              length);
            if (cnt != length)
            {
                ASD_log(ASD_LogLevel_Error, ASD_LogStream_Daemon,
                        ASD_LogOption_No_Remote,
                        "Failed to write to the socket: %d", cnt);
                result = ST_ERR;
            }
        }
    }

    return result;
}

STATUS request_processing_loop(asd_state* state)
{
    STATUS result = ST_OK;
    struct pollfd poll_fds[MAX_FDS] = {{0}};

    poll_fds[HOST_FD_INDEX].fd = state->host_fd;
    poll_fds[HOST_FD_INDEX].events = POLLIN;
    while (1)
    {
        session_fdarr_t session_fds = {-1};
        int n_clients = 0, i;
        int n_gpios = 0;
        int n_poll_ret = -1;
        int n_timeout = -1;       // infinite
        int n_poll_timeout = 10;  // 10 milliseconds
        int client_fd_index = 0;
        asd_target_interface_events target_events;

        if (asd_api_target_ioctl(NULL, &target_events,
                                 IOCTL_TARGET_GET_PIN_FDS) == ST_OK)
        {
            n_gpios = target_events.num_fds;
            for (i = 0; i < n_gpios; i++)
            {
                poll_fds[GPIO_FD_INDEX + i] = target_events.fds[i];
            }
        }
        if (result == ST_OK)
        {
            client_fd_index = GPIO_FD_INDEX + n_gpios;
            if (session_getfds(state->session, &session_fds, &n_clients,
                               &n_timeout) != ST_OK)
            {
#ifdef ENABLE_DEBUG_LOGGING
                ASD_log(ASD_LogLevel_Debug, ASD_LogStream_Daemon,
                        ASD_LogOption_None, "Cannot get client session fds!");
#endif
                result = ST_ERR;
            }
            else
            {
                for (i = 0; i < n_clients; i++)
                {
                    poll_fds[client_fd_index + i].fd = session_fds[i];
                    poll_fds[client_fd_index + i].events = POLLIN;
                }
            }
        }
        if (result == ST_OK)
        {
            // Processing network events
            n_poll_ret = poll(poll_fds, (nfds_t)(client_fd_index + n_clients),
                              n_poll_timeout);

            if (n_poll_ret == -1)      // poll error
            {
                result = ST_ERR;
            }
            else if (n_poll_ret > 0)   // poll returned with network events
            {
                if (poll_fds[HOST_FD_INDEX].revents & POLLIN)
                {
                    process_new_client(state, poll_fds, MAX_FDS, &n_clients,
                                       client_fd_index);
                }
                process_all_client_messages(
                    state, (const struct pollfd*)(&poll_fds[client_fd_index]),
                    (size_t)n_clients);
            }
        }
        if (result == ST_OK)
        {
            if (n_gpios > 0)
            {
                poll_asd_target_interface_events poll_target_fds;
                poll_target_fds.poll_fds = &poll_fds[GPIO_FD_INDEX];
                poll_target_fds.num_fds = n_gpios;

                if (asd_api_target_ioctl(
                        &poll_target_fds, NULL,
                        IOCTL_TARGET_PROCESS_ALL_PIN_EVENTS) != ST_OK)
                {
                    close_connection(state);
                    continue;
                }
            }
        }
        if (result != ST_OK)
            break;
    }
    return result;
}

STATUS close_connection(asd_state* state)
{
    STATUS result = ST_OK;
    extnet_conn_t authd_conn;

    if (session_get_authenticated_conn(state->session, &authd_conn) != ST_OK)
    {
        ASD_log(ASD_LogLevel_Error, ASD_LogStream_Daemon, ASD_LogOption_None,
                "Authorized client already disconnected.");
    }
    else
    {
        ASD_log(ASD_LogLevel_Warning, ASD_LogStream_Daemon, ASD_LogOption_None,
                "Disconnecting client.");
        result = on_client_disconnect(state);
        if (result == ST_OK)
            session_close(state->session, &authd_conn);
    }
    return result;
}

STATUS process_new_client(asd_state* state, struct pollfd* poll_fds,
                          size_t num_fds, int* num_clients, int client_index)
{
    ASD_log(ASD_LogLevel_Warning, ASD_LogStream_Daemon, ASD_LogOption_None,
            "Client Connecting.");
    extnet_conn_t new_extconn;
    STATUS result = ST_OK;

    if (!state || !poll_fds || !num_clients)
        result = ST_ERR;

    if (result == ST_OK)
    {
        result = extnet_accept_connection(state->extnet, state->host_fd,
                                          &new_extconn);
        if (result != ST_OK)
        {
            ASD_log(ASD_LogLevel_Error, ASD_LogStream_Daemon,
                    ASD_LogOption_None,
                    "Failed to accept incoming connection.");
            on_connection_aborted();
        }
    }

    if (result == ST_OK)
    {
        result = session_open(state->session, &new_extconn);
        if (result != ST_OK)
        {
            ASD_log(ASD_LogLevel_Error, ASD_LogStream_Daemon,
                    ASD_LogOption_None,
                    "Unable to add session for new connection fd %d",
                    new_extconn.sockfd);
            extnet_close_client(state->extnet, &new_extconn);
        }
    }

    if (result == ST_OK && state->args.session.e_auth_type == AUTH_HDLR_NONE)
    {
        /* Special case where auth is not required.
         * Stuff fd to the poll_fds array to immediately
         * process the connection. Otherwise it may be
         * timed out as unauthenticated. */
        if (client_index + (*num_clients) < num_fds)
        {
            poll_fds[client_index + *num_clients].fd = new_extconn.sockfd;
            poll_fds[client_index + *num_clients].revents |= POLLIN;
            (*num_clients)++;
        }
    }
    return result;
}

STATUS process_all_client_messages(asd_state* state,
                                   const struct pollfd* poll_fds,
                                   size_t num_fds)
{
    STATUS result = ST_OK;
    if (!state || !poll_fds)
    {
        result = ST_ERR;
    }
    else
    {
        session_close_expired_unauth(state->session);

        for (int i = 0; i < num_fds; i++)
        {
            struct pollfd poll_fd = poll_fds[i];
            if ((poll_fd.revents & POLLIN) == POLLIN)
            {
                STATUS client_result = process_client_message(state, poll_fd);

                // If we error processing 1 client, we will
                // still process the others
                if (client_result != ST_OK)
                    result = client_result;
            }
        }
    }
    return result;
}

bool is_data_pending(void)
{
    return b_data_pending;
}

STATUS process_client_message(asd_state* state, const struct pollfd poll_fd)
{
    STATUS result = ST_OK;
    b_data_pending = false;

    if (!state)
        result = ST_ERR;

    if (result == ST_OK)
    {
        p_extconn = session_lookup_conn(state->session, poll_fd.fd);
        if (!p_extconn)
        {
            ASD_log(ASD_LogLevel_Error, ASD_LogStream_Daemon,
                    ASD_LogOption_None, "Session for fd %d vanished!",
                    poll_fd.fd);
            result = ST_ERR;
        }
    }

    if (result == ST_OK)
    {
        result = session_get_data_pending(state->session, p_extconn,
                                          &b_data_pending);
        if (result != ST_OK)
        {
            ASD_log(ASD_LogLevel_Error, ASD_LogStream_Daemon,
                    ASD_LogOption_None,
                    "Cannot get session data pending for fd %d!", poll_fd.fd);
        }
    }

    if (result == ST_OK && (b_data_pending || poll_fd.revents & POLLIN))
    {
        result = ensure_client_authenticated(state, p_extconn);

        if (result == ST_OK)
        {
            result = asd_api_target_ioctl(NULL, NULL, IOCTL_TARGET_PROCESS_MSG);
            if (result != ST_OK)
            {
                on_client_disconnect(state);
                session_close(state->session, p_extconn);
            }
            else
            {
                result = session_set_data_pending(state->session, p_extconn,
                                                  b_data_pending);
            }
        }
    }
    return result;
}

size_t read_data(void* buffer, size_t length)
{
    size_t size = 0;
    if (p_extconn && buffer)
    {

        int cnt = extnet_recv(main_state.extnet, p_extconn, buffer, length,
                              &b_data_pending);

        if (cnt < 1)
        {
            if (cnt == 0)
                ASD_log(ASD_LogLevel_Error, ASD_LogStream_Daemon,
                        ASD_LogOption_None, "Client disconnected");
            else
                ASD_log(ASD_LogLevel_Error, ASD_LogStream_Daemon,
                        ASD_LogOption_None, "Socket buffer receive failed: %d",
                        cnt);
        }
        else
        {
            size = (size_t) cnt;
        }
    }
    return size;
}

static void send_remote_log_message(ASD_LogLevel asd_level,
                                    ASD_LogStream asd_stream,
                                    const char* message)
{
    asd_target_interface_remote_log remote_log = {
        asd_level,
        asd_stream,
        message
    };
    asd_api_target_ioctl(&remote_log, NULL, IOCTL_TARGET_SEND_REMOTE_LOG_MSG);
}

STATUS ensure_client_authenticated(asd_state* state, extnet_conn_t* p_extconn)
{
    STATUS result = ST_ERR;
    if (state && p_extconn)
    {
        result = session_already_authenticated(state->session, p_extconn);
        if (result != ST_OK)
        {
            // Authenticate the client
            result =
                auth_client_handshake(state->session, state->extnet, p_extconn);
            if (result == ST_OK)
            {
                result = session_auth_complete(state->session, p_extconn);
            }
            if (result == ST_OK)
            {
#ifdef ENABLE_DEBUG_LOGGING
                ASD_log(ASD_LogLevel_Debug, ASD_LogStream_Daemon,
                        ASD_LogOption_None,
                        "Session on fd %d now authenticated",
                        p_extconn->sockfd);
#endif

                result = on_client_connect(state, p_extconn);
                if (result != ST_OK)
                {
                    ASD_log(ASD_LogLevel_Error, ASD_LogStream_Daemon,
                            ASD_LogOption_None, "Connection attempt failed.");
                    on_client_disconnect(state);
                }
            }

            if (result != ST_OK)
            {
                on_connection_aborted();
                session_close(state->session, p_extconn);
            }
        }
    }
    return result;
}

STATUS on_client_connect(asd_state* state, extnet_conn_t* p_extcon)
{
    STATUS result = ST_OK;
    static bus_options target_bus_options;

    if (!state || !p_extcon)
    {
        result = ST_ERR;
    }

    if (result == ST_OK)
    {
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Debug, ASD_LogStream_Daemon, ASD_LogOption_None,
                "Preparing for client connection");
#endif

        log_client_address(p_extcon);

        result = asd_api_target_ioctl(NULL, &target_bus_options,
                                      IOCTL_TARGET_GET_I2C_I3C_BUS_CONFIG);

#ifdef ENABLE_DEBUG_LOGGING
        if (result != ST_OK)
        {
            ASD_log(ASD_LogLevel_Warning, ASD_LogStream_Daemon,
                    ASD_LogOption_No_Remote,
                    "Failed to read i2c/i3c platform config");
        }
#endif
        if (state->args.busopt.enable_i2c || state->args.busopt.enable_i3c ||
            state->args.busopt.enable_spp)
        {
            result = set_config_defaults(&state->config, &state->args.busopt);
        }
        else
        {
            for (int i = 0; i < MAX_IxC_BUSES + MAX_SPP_BUSES; i++)
            {
                if (target_bus_options.bus_config_type[i] == BUS_CONFIG_I2C)
                {
                    ASD_log(ASD_LogLevel_Error, ASD_LogStream_Daemon,
                            ASD_LogOption_No_Remote, "Enabling I2C bus: %d",
                            target_bus_options.bus_config_map[i]);
                }
                if (target_bus_options.bus_config_type[i] == BUS_CONFIG_I3C)
                {
                    ASD_log(ASD_LogLevel_Error, ASD_LogStream_Daemon,
                            ASD_LogOption_No_Remote, "Enabling I3C bus: %d",
                            target_bus_options.bus_config_map[i]);
                }
                if (target_bus_options.bus_config_type[i] == BUS_CONFIG_SPP)
                {
                    ASD_log(ASD_LogLevel_Error, ASD_LogStream_Daemon,
                            ASD_LogOption_No_Remote, "Enabling SPP bus: %d",
                            target_bus_options.bus_config_map[i]);
                }
            }
            result = set_config_defaults(&state->config, &target_bus_options);
        }
    }

    if (result == ST_OK)
    {
        result = asd_api_target_init(&state->config);
        if (result != ST_OK)
        {
            ASD_log(ASD_LogLevel_Error, ASD_LogStream_Daemon,
                    ASD_LogOption_None, "Failed to init asd_msg.");
        }
    }

    if (result == ST_OK)
    {
        init_logging_map();
        ASD_initialize_log_settings(
            state->args.log_level, state->args.log_streams,
            state->args.use_syslog, main_should_remote_log,
            send_remote_log_message);
    }

    return result;
}

void log_client_address(const extnet_conn_t* p_extcon)
{
    struct sockaddr_in6 addr;
    uint8_t client_addr[INET6_ADDRSTRLEN];
    socklen_t addr_sz = sizeof(addr);
    int retcode = 0;

    if (!getpeername(p_extcon->sockfd, (struct sockaddr*)&addr, &addr_sz))
    {
        if (inet_ntop(AF_INET6, &addr.sin6_addr, client_addr,
                      sizeof(client_addr)))
        {
            ASD_log(ASD_LogLevel_Info, ASD_LogStream_Daemon, ASD_LogOption_None,
                    "client %s connected", client_addr);
        }
        else
        {
            if (strcpy_s(client_addr, INET6_ADDRSTRLEN, "address unknown"))
            {
                ASD_log(ASD_LogLevel_Error, ASD_LogStream_Daemon,
                        ASD_LogOption_None, "strcpy_safe: address unknown");
            }
        }
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Info, ASD_LogStream_Daemon, ASD_LogOption_None,
                "ASD is now connected %s", client_addr);
#endif
        // Log ASD connection event into the systems logs
        retcode = sd_journal_send(
            "MESSAGE=At-Scale-Debug is now connected", "PRIORITY=%i", LOG_INFO,
            "REDFISH_MESSAGE_ID=%s", "OpenBMC.0.1.AtScaleDebugConnected",
            "REDFISH_MESSAGE_ARGS=%s", client_addr, NULL);
        if (retcode < 0)
        {
            ASD_log(ASD_LogLevel_Error, ASD_LogStream_Daemon,
                    ASD_LogOption_None, "sd_journal_send failed %d", retcode);
        }
    }
}

STATUS on_client_disconnect(asd_state* state)
{
    STATUS result = ST_OK;
    int retcode;

    if (!state)
    {
        result = ST_ERR;
    }

    if (result == ST_OK)
    {
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Debug, ASD_LogStream_Daemon, ASD_LogOption_None,
                "Cleaning up after client connection");
#endif

        result = set_config_defaults(&state->config, &state->args.busopt);
    }

    if (result == ST_OK)
    {
        ASD_initialize_log_settings(state->args.log_level,
                                    state->args.log_streams,
                                    state->args.use_syslog, NULL, NULL);

        if (asd_api_target_deinit() != ST_OK)
        {
            ASD_log(ASD_LogLevel_Error, ASD_LogStream_Daemon,
                    ASD_LogOption_None,
                    "Failed to de-initialize the asd_msg");
            result = ST_ERR;
        }
    }

    if (result == ST_OK)
    {
#ifdef ENABLE_DEBUG_LOGGING
        ASD_log(ASD_LogLevel_Info, ASD_LogStream_Daemon, ASD_LogOption_None,
                "ASD is now disconnected");
#endif
        // Log ASD connection event into the systems logs
        retcode =
            sd_journal_send("MESSAGE=At-Scale-Debug is now disconnected",
                            "PRIORITY=%i", LOG_INFO, "REDFISH_MESSAGE_ID=%s",
                            "OpenBMC.0.1.AtScaleDebugDisconnected", NULL);

        if (retcode < 0)
        {
            ASD_log(ASD_LogLevel_Error, ASD_LogStream_Daemon,
                    ASD_LogOption_None, "sd_journal_send failed %d", retcode);
        }
    }
    return result;
}

void on_connection_aborted(void)
{
    int retcode = 0;
    // log connection aborted
#ifdef ENABLE_DEBUG_LOGGING
    ASD_log(ASD_LogLevel_Error, ASD_LogStream_Daemon, ASD_LogOption_None,
            "ASD connection aborted");
#endif
    retcode = sd_journal_send("MESSAGE=At-Scale-Debug connection failed",
                              "PRIORITY=%i", LOG_INFO, "REDFISH_MESSAGE_ID=%s",
                              "OpenBMC.0.1.AtScaleDebugConnectionFailed", NULL);

    if (retcode < 0)
    {
        ASD_log(ASD_LogLevel_Error, ASD_LogStream_Daemon, ASD_LogOption_None,
                "sd_journal_send failed %d", retcode);
    }
}
