/*
Copyright (c) 2024, Intel Corporation

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
#include "debug-over-i3c.h"

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define VERSION_MAJOR 1
#define VERSION_MINOR 0

#define FRAME_TOTAL_LIMIT 512

enum verbose_level
{
    verbose_error = 0,
    verbose_warn = 1,
    verbose_info = 2,
    verbose_debug = 3,
};

const char *sopts = "d:r:w:o:a:b:nexvh";
static const struct option lopts[] = {
    {"device",     required_argument,  NULL,   'd' },
    {"read",       required_argument,  NULL,   'r' },
    {"write",      required_argument,  NULL,   'w' },
    {"opcode",     required_argument,  NULL,   'o' },
    {"action",     required_argument,  NULL,   'a' },
    {"broadcast",  required_argument,  NULL,   'b' },
    {"nopoll",     no_argument,        NULL,   'n' },
    {"event",      no_argument,        NULL,   'e' },
    {"verbose",    no_argument,        NULL,   'x' },
    {"version",    no_argument,        NULL,   'v' },
    {"help",       no_argument,        NULL,   'h' },
    {0, 0, 0, 0}
};

enum verbose_level verbose_level;
#define trace(level, ...)       \
{                               \
    if (level <= verbose_level) \
    {                           \
        printf(__VA_ARGS__);    \
    }                           \
}                               \

static void print_help()
{
    printf("This tools can be used to write or/and read data over I3C DBG binding to check Debug for I3C feature.\n");
    printf("\n");
    printf("Options:\n");
    printf("   --device (-d): path to the debug for I3C handle, e.g. /dev/i3c-debug-0\n");
    printf("   --read (-r): number of byte to read, maximal possible value shall be provided to be sure whole received message is read\n");
    printf("   --write (-w): list of byte to write\n");
    printf("   --opcode (-o): opcode value for Debug Opcode CCC, could be used along with -w and/or -r if additional data shall be write and/or read\n");
    printf("   --action (-a): action value for Debug Action CCC\n");
    printf("   --broadcast (-b): broadcast value for Broadcast CCC\n");
    printf("   --nopoll (-n): do not run poll() while reading data\n");
    printf("   --event (-e): run get event ioctl and print data if any\n");
    printf("   --verbose (-x): verbosity level, more 'x' - more verbose\n");
    printf("   --version (-v): print tool version\n");
    printf("   --help (-h): print this help\n");
    printf("\n");
    printf("Usage examples:\n");
    printf("   write request: ./debug-over-i3c -d /dev/i3c-debug-0 -w 0x22,0x30,0x00,0x00,0x11,0xEE,0x77,0x88,0xA5,0xC3,0xC3,0xA5\n");
    printf("   write request and read response: ./debug-over-i3c -d /dev/i3c-debug-0 -w 0x22,0x30,0x00,0x00,0x11,0xEE,0x77,0x88,0xA5,0xC3,0xC3,0xA5 -r 255\n");
    printf("   send Debug Opcode CCC and read response: ./debug-over-i3c -d /dev/i3c-debug-0 -o 0x00 -r 4\n");
    printf("   send Debug Opcode CCC with extra data: ./debug-over-i3c -d /dev/i3c-debug-0 -o 0x02 -w 0x00\n");
    printf("   send Debug Action CCC: ./debug-over-i3c -d /dev/i3c-debug-0 -a 0xFD\n");
    printf("   send Broadcast Action 0xA0: ./debug-over-i3c -xxx -b 10\n");

}

static void print_version()
{
    printf("Debug over I3C Utility. Version %i.%i\n", VERSION_MAJOR, VERSION_MINOR);
}

char data_str_buffer[2048];
static bool get_write_data(const char * const  optarg, uint8_t data[], size_t *len)
{
    size_t input_str_len = strlen(optarg);
    char *byte_str;
    size_t index = 0;

    if (input_str_len >= sizeof(data_str_buffer))
    {
        input_str_len = sizeof(data_str_buffer) - 1;
    }
    memcpy(data_str_buffer, optarg, input_str_len);
    data_str_buffer[input_str_len] = 0;

    byte_str = strtok(data_str_buffer, ",");
    while(byte_str != NULL)
    {
        data[index++] = (uint8_t)strtol(byte_str, NULL, 0);
        byte_str = strtok(NULL, ",");
    }

    if (index > 0)
    {
        *len = index;
        return true;
    }
    else
    {
        return false;
    }
}

int main(int argc, char* argv[])
{
    uint8_t write_buffer[FRAME_TOTAL_LIMIT] = {0};
    uint8_t read_buffer[FRAME_TOTAL_LIMIT] = {0};
    uint8_t event_buffer[FRAME_TOTAL_LIMIT] = {0};
    bool do_read = false, do_write = false, do_event = false;
    struct pollfd debug_poll_fd;
    char* device_path = NULL;
    size_t read_len = 0;
    size_t write_len = 0;
    int debug_fd = -1;
    int opcode = -1;
    int action = -1;
    bool do_broadcast = false;
    uint8_t ba_action = 0;
    bool nopoll = false;
    int opt;
    int ret;
    size_t i;

    while ((opt = getopt_long(argc, argv, sopts, lopts, NULL)) != EOF)
    {
        switch (opt)
        {
            case 'h':
                print_help();
                return 0;
            case 'v':
                print_version();
                return 0;
            case 'd':
                device_path = optarg;
                break;
            case 'r':
                read_len = (size_t)strtol(optarg, NULL, 0);
                if (read_len > 0)
                {
                    do_read = true;
                }
                break;
            case 'w':
                do_write = get_write_data(optarg, write_buffer, &write_len);
                break;
            case 'x':
                verbose_level++;
                break;
            case 'o':
                opcode = (int)strtol(optarg, NULL, 0);
                break;
            case 'a':
                action = (int)strtol(optarg, NULL, 0);
                break;
            case 'b':
                do_broadcast = true;
                ba_action = (uint8_t)strtol(optarg, NULL, 0);
                break;
            case 'n':
                nopoll = true;
                break;
            case 'e':
                do_event = true;
                break;
            default:
                print_help();
                return -1;
        }
    }

    if (do_read || do_write || opcode >= 0 || action >= 0 || do_event)
    {
        if (!device_path)
        {
            trace(verbose_error, "Device path not provided!\n");
            print_help();
            return -1;
        }
    }

    if (do_broadcast)
    {
        ssize_t write_ret;
        /*the i3c-3 might change in future platforms and will need to get updated.*/
        const char *filepath = "/sys/bus/i3c/devices/i3c-3/dbgaction_broadcast";
        char dbg_byte[5]={0};
        int fd = open(filepath, O_WRONLY );
        if (fd == -1) {
            trace(verbose_error, "Error opening file %s: %s\n", filepath, strerror(errno));
            return -1;
        }
        trace(verbose_info, "Debug Action Byte = 0x%x\n", ba_action);
        sprintf(dbg_byte, "%x", ba_action);
        write_ret = write(fd, dbg_byte, 5);
        trace(verbose_info, "Write status: %zi, errno=%i\n", write_ret, errno);
        if (write_ret < 0)
        {
            trace(verbose_error, "Failed to send debug action\n");
        }
        if (close(fd) == -1) {
            trace(verbose_error, "Error closing to file: %s\ns", strerror(errno));
            return -1;
        }
        return write_ret;
    }

    if (read_len > sizeof(read_buffer) || write_len > sizeof(write_buffer))
    {
        trace(verbose_error,
              "Invalid read or write length - larger than internal buffer size (%d bytes)\n",
              FRAME_TOTAL_LIMIT);
        return -1;
    }

    debug_fd = open(device_path, O_RDWR);
    if (debug_fd < 0)
    {
        trace(verbose_error, "Failed to open device path: %s, errno=%i\n",
              device_path, errno);
        return debug_fd;
    }

    if (opcode >= 0)
    {
        struct i3c_debug_opcode_ccc ocpode_ccc = {0};

        ocpode_ccc.opcode = opcode;
        if (write_len)
        {
            ocpode_ccc.write_len = write_len;
            ocpode_ccc.write_ptr = (uintptr_t)write_buffer;
        }
        if (read_len)
        {
            ocpode_ccc.read_len = read_len;
            ocpode_ccc.read_ptr = (uintptr_t)read_buffer;
        }

        ret = ioctl(debug_fd, I3C_DEBUG_IOCTL_DEBUG_OPCODE_CCC,
                    (int32_t*)&ocpode_ccc);
        trace(verbose_info, "Ioctl debug opcode status: %i, errno=%i\n", ret,
              errno);
        if (ret < 0)
        {
            trace(verbose_error, "Failed to send Debug Opcode ioctl\n");
        }
        else
        {
            if (read_len > 0)
            {
                printf("Data: ");
                for (i = 0; i < read_len; ++i)
                {
                    printf(" %02X", read_buffer[i]);
                }
                printf("\n");
            }
        }
        return ret;
    }

    if (action >= 0)
    {
        struct i3c_debug_action_ccc action_ccc;

        action_ccc.action = action;

        ret = ioctl(debug_fd, I3C_DEBUG_IOCTL_DEBUG_ACTION_CCC,
                    (int32_t*)&action_ccc);
        trace(verbose_info, "Ioctl debug action status: %i, errno=%i\n", ret,
              errno);
        if (ret < 0)
        {
            trace(verbose_error, "Failed to send Debug Action ioctl\n");
        }
        return ret;
    }

    if (do_write)
    {
        ssize_t write_ret;

        trace(verbose_info, "Writing data..., write length = %zu\n", write_len);
        write_ret = write(debug_fd, write_buffer, write_len);
        trace(verbose_info, "Write status: %zi, errno=%i\n", write_ret, errno);
        if (write_ret < 0)
        {
            trace(verbose_error, "Failed to write data\n");
        }
    }

    if (do_event)
    {
        struct i3c_get_event_data event_data;

        debug_poll_fd.fd = debug_fd;
        debug_poll_fd.events = POLLIN;

        if (!nopoll)
        {
            trace(verbose_info, "Starting poll\n");
        }

        for (;;)
        {
            if (!nopoll)
            {
                ret = poll(&debug_poll_fd, 1, 1000);
                if (ret < 0)
                {
                    trace(verbose_error, "Error while polling\n");
                    return ret;
                }
            }

            if (nopoll || ((debug_poll_fd.revents & POLLIN) == POLLIN))
            {
                event_data.data_len = (uint16_t)(sizeof(event_buffer));
                event_data.data_ptr = (uintptr_t)event_buffer;
                ret = ioctl(debug_fd, I3C_DEBUG_IOCTL_GET_EVENT_DATA,
                            (int32_t*)&event_data);
                trace(verbose_info,
                      "Ioctl get event data status: %i, errno=%i\n", ret,
                      errno);
                if (ret < 0)
                {
                    trace(verbose_error,
                          "Failed to send Get Event Data ioctl\n");
                }
                else
                {
                    trace(verbose_info, "Event data length = %d",
                          event_data.data_len);
                    printf(", data:");
                    for (i = 0; i < (size_t)event_data.data_len; ++i)
                    {
                        printf(" %02X", event_buffer[i]);
                    }
                    printf("\n");
                }
                break;
            }
        }
    }

    if (do_read)
    {
        ssize_t read_ret;

        trace(verbose_info, "Reading data...\n");

        memset(read_buffer, 0, sizeof(read_buffer));
        read_ret = read(debug_fd, read_buffer, read_len);
        trace(verbose_info, "Read status: %zi, errno=%i\n", read_ret, errno);
        if (read_ret < 0)
        {
            trace(verbose_error, "Failed to read data, read_ret=%zd\n",
                  read_ret);
            return (int)read_ret;
        }

        printf("Data: ");
        for (i = 0; i < (size_t)read_ret; ++i)
        {
            printf(" %02X", read_buffer[i]);
        }
        printf("\n");
    }

    return 0;
}
