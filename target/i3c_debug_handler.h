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

#ifndef I3C_DEBUG_HANDLER_H
#define I3C_DEBUG_HANDLER_H
#define HEADER_SIZE 4

#include <errno.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>
#include <stddef.h>
#include "logging.h"
#include <stdint.h>
#include <safe_mem_lib.h>
#include <safe_str_lib.h>

#include "spp_handler.h"

struct i3c_debug_opcode_ccc {
    __u8 opcode;
    __u16 write_len;
    ___u64 write_ptr;
    __u16 read_len;
    ___u64 read_ptr;
};

struct i3c_debug_action_ccc {
    __u8 action;
};
#define TIMEOUT_I3C_DEBUG_RX 1000   // milliseconds

#define I3C_DEBUG_IOCTL_BASE    0x79

#define I3C_DEBUG_IOCTL_DEBUG_OPCODE_CCC \
	_IOWR(I3C_DEBUG_IOCTL_BASE, 0x41, struct i3c_debug_opcode_ccc)

#define I3C_DEBUG_IOCTL_DEBUG_ACTION_CCC \
	_IOW(I3C_DEBUG_IOCTL_BASE, 0x42, struct i3c_debug_action_ccc)

#define I3C_DEBUG_IOCTL_GET_EVENT_DATA \
	_IOWR(I3C_DEBUG_IOCTL_BASE, 0x43, struct i3c_get_event_data)

struct i3c_get_event_data {
	__u16 data_len;
	___u64 data_ptr;
};


enum i3cMsgType
{
    opcode,
    action,
    sppPayload,
};

typedef struct i3c_cmd
{
    int i3cFd;
    enum i3cMsgType msgType;
    uint8_t opcode;
    uint8_t action;
    uint16_t write_len;
    uint16_t read_len;
    uint8_t* tx_buffer;
    uint8_t* rx_buffer;
} i3c_cmd;

STATUS init_i3c_fd( SPP_Handler* state);
STATUS init_i3c( SPP_Handler* state);
STATUS send_i3c_action(SPP_Handler* state, i3c_cmd *cmd);
STATUS send_i3c_opcode(SPP_Handler* state, i3c_cmd *cmd);
ssize_t receive_i3c(SPP_Handler* state, i3c_cmd *cmd);
void debug_rx(i3c_cmd* cmd);
void debug_i3c_tx(i3c_cmd* cmd);
STATUS send_i3c_cmd(SPP_Handler* state, i3c_cmd *cmd);
STATUS i3c_ibi_handler(int fd, uint8_t* ibi_buffer, uint16_t* ibi_len);


#endif // I3C_DEBUG_HANDLER_H